#include "VAudioWorld.h"
#include "VAudioEmitter.h"
#include "VAudioMaterial.h"
#include "VAudioMaterialComponent.h"
#include "VAudioMaterialConversion.h"
#include "Components/BillboardComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "AudioMixerBlueprintLibrary.h"

extern "C" {
#include "vaudio.h"
}

#include "VaRawLog.h"

// ---------------------------------------------------------------------------
// Helpers (identical coordinate remapping to the original BPLib)
// ---------------------------------------------------------------------------

static VAMatrix MakeTranslationMatrix(const FVector& P)
{
	return vaMatrixCreateTranslation((float)P.X, (float)P.Y, (float)P.Z);
}

// On-screen debug message keys (AddOnScreenDebugMessage slots), grouped here so the
// per-emitter bounds messages (VABoundsMessageBase + index) can't collide with them.
enum EVADebugMessageKey : uint32
{
	VARaytracingTimeMessage = 1001,
	VAListenerPosMessage    = 1002,
	VAAmbientFilterMessage  = 1003,
	VABoundsMessageBase     = 2000,
};

static VAMatrix MakeRotTransMatrix(const FTransform& T)
{
	FQuat Q = T.GetRotation();
	FVector P = T.GetTranslation();

	FVector AxX = Q.GetAxisX();
	FVector AxY = Q.GetAxisY();
	FVector AxZ = Q.GetAxisZ();

	return vaMatrixCreate(
		(float)AxX.X, (float)AxX.Y, (float)AxX.Z, 0.f,
		(float)AxY.X, (float)AxY.Y, (float)AxY.Z, 0.f,
		(float)AxZ.X, (float)AxZ.Y, (float)AxZ.Z, 0.f,
		(float)P.X,   (float)P.Y,   (float)P.Z,   1.f
	);
}

AVAudioWorld::AVAudioWorld()
{
	PrimaryActorTick.bCanEverTick = true;

	UBillboardComponent* Root = CreateDefaultSubobject<UBillboardComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AVAudioWorld::BeginPlay()
{
	Super::BeginPlay();

	World = vaWorldCreate();
	vaWorldSetLogMemoryAllocationWarnings(World, true);
	vaWorldSetCoordinateSystem(World, VACoordinateSystemUnreal);
	vaWorldSetLogCallback(World, &VaSdkLogCallback);
	vaWorldSetPosition(World, vaVectorCreate(
		(float)WorldPosition.X,
		(float)WorldPosition.Y,
		(float)WorldPosition.Z));
	vaWorldSetSize(World, vaVectorCreate(
		(float)WorldSize.X,
		(float)WorldSize.Y,
		(float)WorldSize.Z));
	vaWorldSetInverseSpeedOfSound(World, 1.0f / SpeedOfSound);
	vaWorldSetMetersPerUnit(World, MetersPerUnit);
	vaWorldSetWorldIsIndoors(World, bIsIndoors);
	vaWorldSetEpsilon(World, Epsilon);
	vaWorldSetEmittersOutsideTheWorldAreMuffled(World, bEmittersOutsideTheWorldAreMuffled);
	vaWorldSetWorkItemCount(World, FMath::Max(1, WorkItemCount));
	vaWorldSetMaximumConcurrencyLevel(World, FMath::Max(1, MaximumConcurrencyLevel));
	vaWorldSetSingleThreaded(World, bSingleThreaded);
	vaWorldSetPendingShutdown(World, bPendingShutdown);
	vaWorldSetReferenceFrequencyLF(World, ReferenceFrequencyLF);
	vaWorldSetReferenceFrequencyHF(World, ReferenceFrequencyHF);

	if (bAirAbsorptionEnabled)
	{
		vaWorldSetAirAbsorptionHumidity(World, Humidity);
		vaWorldSetAirAbsorptionTemperature(World, Temperature);
		vaWorldSetAirAbsorptionPressure(World, Pressure);
	}
	else
	{
		vaWorldSetAirAbsorption(World, nullptr);
	}

	int32 GroupedEAXCount = GroupedEAXSubmixes.Num();
	vaWorldSetMaximumGroupedEAXCount(World, GroupedEAXCount);

	for (int32 i = 0; i < GroupedEAXCount; i++)
	{
		USoundSubmix* Sub = GroupedEAXSubmixes[i];
		USubmixEffectReverbPreset* Preset = NewObject<USubmixEffectReverbPreset>(this);
		if (Sub)
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, Sub, Preset);
		GroupedEAXPresets.Add(Preset);
		VALog(L"GroupedEAX[%d] submix=%s", i, Sub ? *Sub->GetName() : TEXT("null"));
	}

	ApplyChildMaterials();
	ScanAndAddPrimitives();
}

void AVAudioWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	GroupedEAXPresets.Empty();

	if (World)
	{
		vaWorldWait(World);
		DestroyPrimitives();
		vaWorldDestroy(World);
		World = nullptr;
	}
}

void AVAudioWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (World)
	{
		vaWorldUpdate(World);

		static float TimeSinceHeartbeat = 0.0f;
		TimeSinceHeartbeat += DeltaTime;

		bool doHeartbeat = TimeSinceHeartbeat >= 1.0f;

		if (doHeartbeat)
		{
			VALog(L"RaytracingTime=%.3fms RegisteredEmitters=%d", vaWorldGetRaytracingTime(World), RegisteredEmitters.Num());
			TimeSinceHeartbeat = 0.0f;
		}

		if (bReverbOnly != bWasReverbOnly)
		{
			bWasReverbOnly = bReverbOnly;
			bool bDryEnabled = !bReverbOnly;

			for (AVAudioEmitter* Emitter : RegisteredEmitters)
				Emitter->SetDryOutputEnabled(bDryEnabled);
		}

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage((uint64)VARaytracingTimeMessage, 0.0f, FColor::Cyan, FString::Printf(TEXT("VA Raytracing: %.3f ms"), vaWorldGetRaytracingTime(World)));

			if (AVAudioEmitter* CurrentMainListener = GetMainListener())
			{
				FVector ListenerPos = CurrentMainListener->GetActorLocation();
				GEngine->AddOnScreenDebugMessage((uint64)VAListenerPosMessage, 0.0f, FColor::Green, FString::Printf(TEXT("VA Listener pos: (%.1f, %.1f, %.1f)"), ListenerPos.X, ListenerPos.Y, ListenerPos.Z));

				if (VALowPassFilter* AmbientFilter = vaEmitterGetAmbientFilter(CurrentMainListener->GetVAEmitter()))
					GEngine->AddOnScreenDebugMessage((uint64)VAAmbientFilterMessage, 0.0f, FColor::Yellow, FString::Printf(TEXT("VA Ambient LPF: gainLF=%.3f  gainHF=%.3f"), AmbientFilter->gainLF, AmbientFilter->gainHF));
			}

			// Per-emitter position and world-bounds check
			for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
			{
				AVAudioEmitter* emitter = RegisteredEmitters[i];
				VAEmitter* vaEmitter = emitter->GetVAEmitter();

				// Registered emitters can still have a null VAEmitter* if TryInitializeEmitter()
				// hasn't completed yet (e.g. this world's own BeginPlay ran after theirs) - skip
				// until it catches up on a later Tick.
				if (!vaEmitter)
					continue;

				bool bInBounds = vaEmitterWithinWorldBounds(vaEmitter);
				VAVector P = vaEmitterGetPosition(vaEmitter);

				FColor Color = bInBounds ? FColor::Green : FColor::Red;
				GEngine->AddOnScreenDebugMessage((uint64)VABoundsMessageBase + i, 0.0f, Color,
					FString::Printf(TEXT("VA Emitter[%d] '%s': (%.1f, %.1f, %.1f) %s"),
						i, *emitter->GetName(), P.x, P.y, P.z,
						bInBounds ? TEXT("[in bounds]") : TEXT("[OUT OF BOUNDS]")));
			}
		}
	}
}

USoundSubmix* AVAudioWorld::GetGroupedEAXSubmix(int32 Index) const
{
	return GroupedEAXSubmixes.IsValidIndex(Index) ? GroupedEAXSubmixes[Index] : nullptr;
}

USubmixEffectReverbPreset* AVAudioWorld::GetGroupedEAXPreset(int32 Index) const
{
	return GroupedEAXPresets.IsValidIndex(Index) ? GroupedEAXPresets[Index] : nullptr;
}

void AVAudioWorld::RegisterEmitter(AVAudioEmitter* Emitter)
{
	RegisteredEmitters.AddUnique(Emitter);

	if (Emitter->bIsMainListener)
	{
		if (MainListener.IsValid() && MainListener.Get() != Emitter)
		{
			VALog(L"'%s' registered as main listener, but '%s' is already the main listener - keeping the first one. Only one emitter should have bIsMainListener = true.",
				*Emitter->GetName(), *MainListener->GetName());
		}
		else
		{
			MainListener = Emitter;
		}
	}
}

void AVAudioWorld::UnregisterEmitter(AVAudioEmitter* Emitter)
{
	RegisteredEmitters.Remove(Emitter);

	if (MainListener.Get() == Emitter)
		MainListener = nullptr;
}

AVAudioEmitter* AVAudioWorld::GetMainListener() const
{
	return MainListener.Get();
}


void AVAudioWorld::ExportWorld()
{
	if (!World)
	{
		VALog(L"world is null (press Play first).");
		return;
	}

	FString Path = FPaths::ProjectDir() + TEXT("vaudio_export.va");
	vaWorldExport(World, TCHAR_TO_UTF8(*Path));
}

void AVAudioWorld::ImportWorld()
{
	if (!World)
	{
		VALog(L"world is null (press Play first).");
		return;
	}

	FString Path = FPaths::ProjectDir() + TEXT("vaudio_export.va");
	VAEmitter** ImportedEmitters = nullptr;
	int32 ImportedEmitterCount = 0;

	VAResult Result = vaWorldImport(World, TCHAR_TO_UTF8(*Path), &ImportedEmitters, &ImportedEmitterCount);

	if (Result != VA_SUCCESS)
	{
		VALog(L"import failed (result=%d) for '%s'.", Result, *Path);
		return;
	}

	VALog(L"imported %d emitter(s) from '%s'.", ImportedEmitterCount, *Path);
	free(ImportedEmitters);
}

// ---------------------------------------------------------------------------
// Child material actors — AVAudioMaterial children of this world actor
// ---------------------------------------------------------------------------

void AVAudioWorld::ApplyChildMaterials()
{
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);

	int32 AppliedCount = 0;

	for (AActor* Child : AttachedActors)
	{
		AVAudioMaterial* Mat = Cast<AVAudioMaterial>(Child);

		if (Mat)
		{
			Mat->ApplyToWorld(World);
			++AppliedCount;
		}
	}
}

// ---------------------------------------------------------------------------
// Primitive scan — finds every actor with UVAudioMaterialComponent
// ---------------------------------------------------------------------------

// Walk the attach-parent chain to find the nearest UVAudioMaterialComponent.
static UVAudioMaterialComponent* FindMaterialInChain(AActor* Actor)
{
	for (AActor* actor = Actor; actor != nullptr; actor = actor->GetAttachParentActor())
	{
		UVAudioMaterialComponent* materialComponent = actor->FindComponentByClass<UVAudioMaterialComponent>();

		if (materialComponent)
			return materialComponent;
	}
	return nullptr;
}

// True if this mesh would use its simple collision (sphyl/sphere/box) rather than the
// triangle-mesh fallback, matching the bAddedSimple check in ScanAndAddPrimitives.
static bool HasSimpleCollision(UStaticMesh* Mesh)
{
	UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;

	if (!BodySetup)
		return false;

	const FKAggregateGeom& Agg = BodySetup->AggGeom;
	return !Agg.SphylElems.IsEmpty() || !Agg.SphereElems.IsEmpty() || !Agg.BoxElems.IsEmpty();
}

#if WITH_EDITOR
void AVAudioWorld::BakeGeometry()
{
	UWorld* UEWorld = GetWorld();

	if (!UEWorld)
	{
		VALog(L"no world (open a level first).");
		return;
	}

	Modify();
	BakedMeshes.Reset();

	int32 BakedCount = 0;

	for (TActorIterator<AActor> ActorIt(UEWorld); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;

		// Null if this actor (or its attach-parent chain) has no UVAudioMaterialComponent, or has
		// one that belongs to a different VAudioWorld - not baked geometry for this world.
		UVAudioMaterialComponent* MatComp = FindMaterialInChain(Actor);
		if (!MatComp || MatComp->AudioWorld != this) continue;

		TArray<UStaticMeshComponent*> MeshComps;
		Actor->GetComponents<UStaticMeshComponent>(MeshComps);

		for (UStaticMeshComponent* MeshComp : MeshComps)
		{
			// Null if the component has no mesh assigned. Simple-collision meshes are skipped
			// here too - ScanAndAddPrimitives() already picks up their live collision shapes
			// every run, so baking their triangle mesh as well would be redundant.
			UStaticMesh* Mesh = MeshComp->GetStaticMesh();
			if (!Mesh || HasSimpleCollision(Mesh)) continue;

			if (!Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty())
			{
				VALog(L"mesh '%s' on '%s' has no render data in-editor, skipping", *Mesh->GetName(), *Actor->GetName());
				continue;
			}

			FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
			FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;

			TArray<uint32> Indices;
			LOD.IndexBuffer.GetCopy(Indices);
			if (Indices.IsEmpty())
				continue;

			FVAudioBakedMesh& Baked = BakedMeshes.AddDefaulted_GetRef();
			Baked.ActorName = Actor->GetName();
			Baked.ComponentName = MeshComp->GetFName();
			Baked.Vertices.Reserve(Indices.Num());

			for (uint32 Idx : Indices)
				Baked.Vertices.Add(PosBuffer.VertexPosition(Idx));

			++BakedCount;

			VALog(L"baked '%s'.'%s' tris=%d", *Actor->GetName(), *MeshComp->GetName(), Indices.Num() / 3);
		}
	}

	MarkPackageDirty();
	VALog(L"baked %d mesh component(s). Save the level to persist.", BakedCount);
}
#endif

void AVAudioWorld::ScanAndAddPrimitives()
{
	UWorld* UEWorld = GetWorld();

	// Null if this actor isn't in a live level (e.g. called outside BeginPlay/PIE).
	if (!UEWorld)
		return;

	int32 SimpleCount = 0;
	int32 MeshCount = 0;
	int32 SkippedCount = 0;

	for (TActorIterator<AActor> ActorIt(UEWorld); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;

		UVAudioMaterialComponent* MatComp = FindMaterialInChain(Actor);
		if (!MatComp || MatComp->AudioWorld != this)
		{
			++SkippedCount;
			continue;
		}

		VAMaterialType Material = EVAudioMaterialToVA(MatComp->Material);

		TArray<UShapeComponent*> ShapeComps;
		Actor->GetComponents<UShapeComponent>(ShapeComps);

		for (UShapeComponent* ShapeComp : ShapeComps)
		{
			FTransform CompTransform = ShapeComp->GetComponentTransform();

			if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(ShapeComp))
			{
				VAMatrix Mat = MakeRotTransMatrix(CompTransform);

				VACapsulePrimitive* Cap = vaCapsulePrimitiveCreate();
				vaCapsulePrimitiveSetRadius(Cap,   Capsule->GetScaledCapsuleRadius());
				vaCapsulePrimitiveSetLength(Cap,   Capsule->GetScaledCapsuleHalfHeight_WithoutHemisphere() * 2.0f);
				vaCapsulePrimitiveSetMaterial(Cap, Material);
				vaCapsulePrimitiveSetTransform(Cap, &Mat);
				vaWorldAddPrimitive_(World, Cap);
				CapsulePrimitives.Add(Cap);
				++SimpleCount;
			}
			else if (USphereComponent* Sphere = Cast<USphereComponent>(ShapeComp))
			{
				FVector Center = CompTransform.GetTranslation();

				VASpherePrimitive* Sp = vaSpherePrimitiveCreate();
				vaSpherePrimitiveSetCenter(Sp, vaVectorCreate((float)Center.X, (float)Center.Y, (float)Center.Z));
				vaSpherePrimitiveSetRadius(Sp,   Sphere->GetScaledSphereRadius());
				vaSpherePrimitiveSetMaterial(Sp, Material);
				vaWorldAddPrimitive_(World, Sp);
				SpherePrimitives.Add(Sp);
				++SimpleCount;
			}
			else if (UBoxComponent* Box = Cast<UBoxComponent>(ShapeComp))
			{
				VAMatrix Mat = MakeRotTransMatrix(CompTransform);
				FVector Extent = Box->GetScaledBoxExtent();

				VAPrismPrimitive* Prism = vaPrismPrimitiveCreate();
				vaPrismPrimitiveSetSize(Prism, vaVectorCreate(Extent.X * 2.0f, Extent.Y * 2.0f, Extent.Z * 2.0f));
				vaPrismPrimitiveSetMaterial(Prism, Material);
				vaPrismPrimitiveSetTransform(Prism, &Mat);
				vaWorldAddPrimitive_(World, Prism);
				PrismPrimitives.Add(Prism);
				++SimpleCount;
			}
		}

		TArray<UStaticMeshComponent*> MeshComps;
		Actor->GetComponents<UStaticMeshComponent>(MeshComps);

		if (MeshComps.IsEmpty())
		{
			continue;
		}

		for (UStaticMeshComponent* MeshComp : MeshComps)
		{
			UStaticMesh* Mesh = MeshComp->GetStaticMesh();

			// Null if the component has no mesh assigned; simple collision shapes on
			// mesh-less actors are picked up separately via UShapeComponent above.
			if (!Mesh)
				continue;

			FTransform CompTransform = MeshComp->GetComponentTransform();
			FVector WorldPos = CompTransform.GetTranslation();
			FVector Scale = CompTransform.GetScale3D();

			bool bAddedSimple = false;
			UBodySetup* BodySetup = Mesh->GetBodySetup();

			if (BodySetup)
			{
				const FKAggregateGeom& Agg = BodySetup->AggGeom;

				for (const FKSphylElem& Sphyl : Agg.SphylElems)
				{
					FQuat   WorldRot    = Sphyl.GetTransform().GetRotation() * CompTransform.GetRotation();
					FVector WorldCenter = CompTransform.TransformPosition(Sphyl.GetTransform().GetTranslation());
					FTransform WT(WorldRot, WorldCenter, FVector::OneVector);
					VAMatrix Mat = MakeRotTransMatrix(WT);

					VACapsulePrimitive* Cap = vaCapsulePrimitiveCreate();
					vaCapsulePrimitiveSetRadius(Cap,   Sphyl.Radius * FMath::Max(Scale.X, Scale.Y));
					vaCapsulePrimitiveSetLength(Cap,   Sphyl.Length * Scale.Z);
					vaCapsulePrimitiveSetMaterial(Cap, Material);
					vaCapsulePrimitiveSetTransform(Cap, &Mat);
					vaWorldAddPrimitive_(World, Cap);
					CapsulePrimitives.Add(Cap);
					bAddedSimple = true;
					++SimpleCount;
				}

				for (const FKSphereElem& Sphere : Agg.SphereElems)
				{
					FVector Center = CompTransform.TransformPosition(Sphere.GetTransform().GetTranslation());
					float   Radius = Sphere.Radius * Scale.GetAbsMax();

					VASpherePrimitive* Sp = vaSpherePrimitiveCreate();
					vaSpherePrimitiveSetCenter(Sp, vaVectorCreate((float)Center.X, (float)Center.Y, (float)Center.Z));
					vaSpherePrimitiveSetRadius(Sp,   Radius);
					vaSpherePrimitiveSetMaterial(Sp, Material);
					vaWorldAddPrimitive_(World, Sp);
					SpherePrimitives.Add(Sp);
					bAddedSimple = true;
					++SimpleCount;
				}

				for (const FKBoxElem& Box : Agg.BoxElems)
				{
					FQuat   WorldRot    = Box.GetTransform().GetRotation() * CompTransform.GetRotation();
					FVector WorldCenter = CompTransform.TransformPosition(Box.GetTransform().GetTranslation());
					FTransform WT(WorldRot, WorldCenter, FVector::OneVector);
					VAMatrix Mat = MakeRotTransMatrix(WT);

					VAPrismPrimitive* Prism = vaPrismPrimitiveCreate();
					vaPrismPrimitiveSetSize(Prism, vaVectorCreate(Box.X * Scale.X, Box.Y * Scale.Y, Box.Z * Scale.Z));
					vaPrismPrimitiveSetMaterial(Prism, Material);
					vaPrismPrimitiveSetTransform(Prism, &Mat);
					vaWorldAddPrimitive_(World, Prism);
					PrismPrimitives.Add(Prism);
					bAddedSimple = true;
					++SimpleCount;
				}
			}

			if (bAddedSimple)
				continue;

			// Fall back to triangle mesh: prefer baked geometry (reliable in shipping builds
			// regardless of bAllowCPUAccess/cook quirks), otherwise use the mesh's live render
			// data (always up to date, but may be unavailable in cooked builds).
			const FVAudioBakedMesh* Baked = nullptr;

			for (const FVAudioBakedMesh& bakedMesh : BakedMeshes)
			{
				if (bakedMesh.ComponentName == MeshComp->GetFName() && bakedMesh.ActorName == Actor->GetName())
				{
					Baked = &bakedMesh;
					break;
				}
			}

			TArray<FVector3f> LocalPositions;
			if (Baked)
			{
				LocalPositions = Baked->Vertices;
			}
			else
			{
				if (!Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty())
				{
					VALog(L"mesh '%s' has no render data and no baked geometry, skipping. Run 'Bake Geometry For Shipping' on the VA Audio World and save the level.", *Mesh->GetName());
					continue;
				}

				FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
				FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;

				TArray<uint32> Indices;
				LOD.IndexBuffer.GetCopy(Indices);
				if (Indices.IsEmpty())
				{
					VALog(L"mesh '%s' has no index data and no baked geometry, skipping. Run 'Bake Geometry For Shipping' on the VA Audio World and save the level.", *Mesh->GetName());
					continue;
				}

				LocalPositions.Reserve(Indices.Num());
				for (uint32 Idx : Indices)
					LocalPositions.Add(PosBuffer.VertexPosition(Idx));
			}

			TArray<VAVector> VAVerts;
			VAVerts.Reserve(LocalPositions.Num());
			VAVector MinB = VECTOR_MAX;
			VAVector MaxB = VECTOR_MIN;

			for (const FVector3f& LocalPos : LocalPositions)
			{
				FVector RotScaled = CompTransform.GetRotation().RotateVector(Scale * FVector(LocalPos));
				VAVector V = vaVectorCreate((float)RotScaled.X, (float)RotScaled.Y, (float)RotScaled.Z);
				VAVerts.Add(V);
				MinB = vaVectorMin(MinB, V);
				MaxB = vaVectorMax(MaxB, V);
			}

			VAMatrix Transform = MakeTranslationMatrix(WorldPos);
			VAMeshPrimitive* MeshPrim = vaMeshPrimitiveCreate(
				Material, VAVerts.GetData(), VAVerts.Num(), MinB, MaxB, &Transform
			);

			vaMeshPrimitiveSetSupports3DPermeation(MeshPrim, MatComp->bSupports3DPermeation);

			vaWorldAddPrimitive_(World, MeshPrim);
			MeshPrimitives.Add(MeshPrim);
			++MeshCount;
		}
	}

	VALog(L"added %d simple + %d mesh primitives (%d actors skipped, no material)", SimpleCount, MeshCount, SkippedCount);
}

void AVAudioWorld::DestroyPrimitives()
{
	for (VAMeshPrimitive*    P : MeshPrimitives)    { vaWorldRemovePrimitive_(World, P); vaMeshPrimitiveDestroy(P); }
	for (VACapsulePrimitive* P : CapsulePrimitives) { vaWorldRemovePrimitive_(World, P); vaCapsulePrimitiveDestroy(P); }
	for (VASpherePrimitive*  P : SpherePrimitives)  { vaWorldRemovePrimitive_(World, P); vaSpherePrimitiveDestroy(P); }
	for (VAPrismPrimitive*   P : PrismPrimitives)   { vaWorldRemovePrimitive_(World, P); vaPrismPrimitiveDestroy(P); }

	MeshPrimitives.Empty();
	CapsulePrimitives.Empty();
	SpherePrimitives.Empty();
	PrismPrimitives.Empty();
}
