#include "VAudioWorld.h"
#include "VAudioEmitter.h"
#include "VAudioMaterial.h"
#include "VAudioMaterialComponent.h"
#include "Components/BillboardComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
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

static VAMaterialType EnumToVAMaterial(EVAudioMaterial M)
{
	switch (M)
	{
		case EVAudioMaterial::Brick:            return VAMaterialBrick;
		case EVAudioMaterial::Cloth:            return VAMaterialCloth;
		case EVAudioMaterial::ConcretePolished: return VAMaterialConcretePolished;
		case EVAudioMaterial::Dirt:             return VAMaterialDirt;
		case EVAudioMaterial::Glass:            return VAMaterialGlass;
		case EVAudioMaterial::Grass:            return VAMaterialGrass;
		case EVAudioMaterial::Gravel:           return VAMaterialGravel;
		case EVAudioMaterial::Gyprock:          return VAMaterialGyprock;
		case EVAudioMaterial::Ice:              return VAMaterialIce;
		case EVAudioMaterial::Leaf:             return VAMaterialLeaf;
		case EVAudioMaterial::Marble:           return VAMaterialMarble;
		case EVAudioMaterial::Metal:            return VAMaterialMetal;
		case EVAudioMaterial::Mud:              return VAMaterialMud;
		case EVAudioMaterial::Rock:             return VAMaterialRock;
		case EVAudioMaterial::Sand:             return VAMaterialSand;
		case EVAudioMaterial::Snow:             return VAMaterialSnow;
		case EVAudioMaterial::Tile:             return VAMaterialTile;
		case EVAudioMaterial::Tree:             return VAMaterialTree;
		case EVAudioMaterial::Water:            return VAMaterialWater;
		case EVAudioMaterial::WoodIndoor:       return VAMaterialWoodIndoor;
		case EVAudioMaterial::WoodOutdoor:      return VAMaterialWoodOutdoor;
		default:                                return VAMaterialConcrete;
	}
}

static VAMatrix MakeTranslationMatrix(const FVector& P)
{
	return vaMatrixCreateTranslation((float)P.X, (float)P.Y, (float)P.Z);
}

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
	vaWorldSetPendingShutdown(World, bPendingShutdown);
	vaWorldSetReferenceFrequencyLF(World, ReferenceFrequencyLF);
	vaWorldSetReferenceFrequencyHF(World, ReferenceFrequencyHF);
	vaWorldSetAirAbsorptionHumidity(World, Humidity);
	vaWorldSetAirAbsorptionTemperature(World, Temperature);
	vaWorldSetAirAbsorptionPressure(World, Pressure);

	int32 GroupedEAXCount = FMath::Max(2, GroupedEAXSubmixes.Num());
	vaWorldSetMaximumGroupedEAXCount(World, GroupedEAXCount);

	for (int32 i = 0; i < GroupedEAXSubmixes.Num(); ++i)
	{
		USoundSubmix* Sub = GroupedEAXSubmixes[i];
		USubmixEffectReverbPreset* Preset = NewObject<USubmixEffectReverbPreset>(this);
		if (Sub)
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, Sub, Preset);
		GroupedEAXPresets.Add(Preset);
		UE_LOG(LogTemp, Log, TEXT("VA World: GroupedEAX[%d] submix=%s"), i, Sub ? *Sub->GetName() : TEXT("null"));
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
			VaRawLog(L"AVAudioWorld '%s' RaytracingTime=%.3fms RegisteredEmitters=%d", *GetName(), vaWorldGetRaytracingTime(World), RegisteredEmitters.Num());
			TimeSinceHeartbeat = 0.0f;
		}

		bool bDryEnabled = !bReverbOnly;

		// TODO - Rather than doing this in a loop every tick, invoke SetDryOutputEnabled() whenever bReverbOnly actually changes
		for (AVAudioEmitter* emitter : RegisteredEmitters)
		{
			if (emitter)
				emitter->SetDryOutputEnabled(bDryEnabled);
		}

		if (GEngine)
		{
			// TODO - proper codes in an enum somewhere instead of 1001, 1002, etc.
			GEngine->AddOnScreenDebugMessage(1001, 0.0f, FColor::Cyan, FString::Printf(TEXT("VA Raytracing: %.3f ms"), vaWorldGetRaytracingTime(World)));

			for (AVAudioEmitter* emitter : RegisteredEmitters)
			{
				// TODO - rather than looping, store the main listener on this world. There can only be one main listener, so throw a warning/error if a 2nd emitter tries to set bIsMainListener to true
				if (!emitter || !emitter->bIsMainListener)
					continue;

				FVector actorPosition = emitter->GetActorLocation();
				// TODO - proper codes in an enum somewhere instead of 1001, 1002, etc.
				GEngine->AddOnScreenDebugMessage(1002, 0.0f, FColor::Green, FString::Printf(TEXT("VA Listener pos: (%.1f, %.1f, %.1f)"), actorPosition.X, actorPosition.Y, actorPosition.Z));

				VALowPassFilter* ambientFilter = vaEmitterGetAmbientFilter(emitter->GetVAEmitter());
				if (ambientFilter)
				{
					// TODO - proper codes in an enum somewhere instead of 1001, 1002, etc.
					GEngine->AddOnScreenDebugMessage(1003, 0.0f, FColor::Yellow, FString::Printf(TEXT("VA Ambient LPF: gainLF=%.3f  gainHF=%.3f"), ambientFilter->gainLF, ambientFilter->gainHF));
				}
				break;
			}

			// TODO - each emitter already has a vaEmitterWithinWorldBounds() call, use that here instead
			// Per-emitter position and world-bounds check
			const FVector BoundsMin = WorldPosition;
			const FVector BoundsMax = WorldPosition + WorldSize;
			for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
			{
				AVAudioEmitter* E = RegisteredEmitters[i];
				if (!E)
					continue;

				FVector P = E->GetActorLocation();
				bool bInBounds = P.X >= BoundsMin.X && P.X <= BoundsMax.X
				              && P.Y >= BoundsMin.Y && P.Y <= BoundsMax.Y
				              && P.Z >= BoundsMin.Z && P.Z <= BoundsMax.Z;

				FColor Color = bInBounds ? FColor::Green : FColor::Red;
				GEngine->AddOnScreenDebugMessage(2000 + i, 0.0f, Color,
					FString::Printf(TEXT("VA Emitter[%d] '%s': (%.1f, %.1f, %.1f) %s"),
						i, *E->GetName(), P.X, P.Y, P.Z,
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
}

void AVAudioWorld::UnregisterEmitter(AVAudioEmitter* Emitter)
{
	RegisteredEmitters.Remove(Emitter);
}

AVAudioEmitter* AVAudioWorld::GetMainListener() const
{
	// TODO - rather than looping, store the main listener on this world. There can only be one main listener, so throw a warning/error if a 2nd emitter tries to set bIsMainListener to true
	for (AVAudioEmitter* E : RegisteredEmitters)
	{
		if (E && E->bIsMainListener)
			return E;
	}

	return nullptr;
}


void AVAudioWorld::ExportWorld()
{
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("VA ExportWorld: world is null (press Play first)."));
		return;
	}

	FString Path = FPaths::ProjectDir() + TEXT("vaudio_export.va");
	vaWorldExport(World, TCHAR_TO_UTF8(*Path));
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
		UE_LOG(LogTemp, Warning, TEXT("VA BakeGeometry: no world (open a level first)."));
		return;
	}

	Modify();
	BakedMeshes.Reset();

	int32 BakedCount = 0;

	for (TActorIterator<AActor> ActorIt(UEWorld); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;

		UVAudioMaterialComponent* MatComp = FindMaterialInChain(Actor);
		if (!MatComp || MatComp->AudioWorld != this) continue;

		TArray<UStaticMeshComponent*> MeshComps;
		Actor->GetComponents<UStaticMeshComponent>(MeshComps);

		for (UStaticMeshComponent* MeshComp : MeshComps)
		{
			UStaticMesh* Mesh = MeshComp->GetStaticMesh();
			if (!Mesh || HasSimpleCollision(Mesh)) continue;

			if (!Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("VA BakeGeometry: mesh '%s' on '%s' has no render data in-editor, skipping"), *Mesh->GetName(), *Actor->GetName());
				continue;
			}

			FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
			FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;

			TArray<uint32> Indices;
			LOD.IndexBuffer.GetCopy(Indices);
			if (Indices.IsEmpty()) continue;

			FVAudioBakedMesh& Baked = BakedMeshes.AddDefaulted_GetRef();
			Baked.ActorName = Actor->GetName();
			Baked.ComponentName = MeshComp->GetFName();
			Baked.Vertices.Reserve(Indices.Num());

			for (uint32 Idx : Indices)
				Baked.Vertices.Add(PosBuffer.VertexPosition(Idx));

			++BakedCount;
			UE_LOG(LogTemp, Log, TEXT("VA BakeGeometry: baked '%s'.'%s' tris=%d"),
				*Actor->GetName(), *MeshComp->GetName(), Indices.Num() / 3);
		}
	}

	MarkPackageDirty();
	UE_LOG(LogTemp, Log, TEXT("VA BakeGeometry: baked %d mesh component(s). Save the level to persist."), BakedCount);
}
#endif

void AVAudioWorld::ScanAndAddPrimitives()
{
	UWorld* UEWorld = GetWorld();
	if (!UEWorld) return;

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

		VAMaterialType Material = EnumToVAMaterial(MatComp->Material);

		TArray<UStaticMeshComponent*> MeshComps;
		Actor->GetComponents<UStaticMeshComponent>(MeshComps);

		if (MeshComps.IsEmpty())
		{
			UE_LOG(LogTemp, Log, TEXT("VA: actor '%s' -> no static mesh components, skipping"),
				*Actor->GetName());
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("VA: actor '%s' -> material %d (%d mesh components)"),
			*Actor->GetName(), (int32)Material, MeshComps.Num());

		for (UStaticMeshComponent* MeshComp : MeshComps)
		{
			UStaticMesh* Mesh = MeshComp->GetStaticMesh();
			if (!Mesh) continue;

			FTransform CompTransform = MeshComp->GetComponentTransform();
			FVector WorldPos = CompTransform.GetTranslation();
			FVector Scale    = CompTransform.GetScale3D();

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

			if (bAddedSimple) continue;

			// Fall back to triangle mesh: prefer baked geometry (reliable in shipping builds
			// regardless of bAllowCPUAccess/cook quirks), otherwise use the mesh's live render
			// data (always up to date, but may be unavailable in cooked builds).
			const FVAudioBakedMesh* Baked = nullptr;
			for (const FVAudioBakedMesh& B : BakedMeshes)
			{
				if (B.ComponentName == MeshComp->GetFName() && B.ActorName == Actor->GetName())
				{
					Baked = &B;
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
					UE_LOG(LogTemp, Warning, TEXT("VA:   mesh '%s' has no render data and no baked geometry, skipping. Run 'Bake Geometry For Shipping' on the VA Audio World and save the level."), *Mesh->GetName());
					VaRawLog(L"VA: mesh '%s' has no render data and no baked geometry, skipping. Run 'Bake Geometry For Shipping' on the VA Audio World and save the level.", *Mesh->GetName());
					continue;
				}

				FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
				FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;

				TArray<uint32> Indices;
				LOD.IndexBuffer.GetCopy(Indices);
				if (Indices.IsEmpty())
				{
					UE_LOG(LogTemp, Warning, TEXT("VA:   mesh '%s' has no index data and no baked geometry, skipping. Run 'Bake Geometry For Shipping' on the VA Audio World and save the level."), *Mesh->GetName());
					VaRawLog(L"VA: mesh '%s' has no index data and no baked geometry, skipping. Run 'Bake Geometry For Shipping' on the VA Audio World and save the level.", *Mesh->GetName());
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
			if (!MatComp->bSupports3DPermeation)
				UE_LOG(LogTemp, Log, TEXT("VA:   mesh '%s' permeation disabled"), *Mesh->GetName());

			vaWorldAddPrimitive_(World, MeshPrim);
			MeshPrimitives.Add(MeshPrim);
			++MeshCount;

			UE_LOG(LogTemp, Log, TEXT("VA:   mesh '%s' tris=%d (%s)"), *Mesh->GetName(), LocalPositions.Num() / 3, Baked ? TEXT("baked") : TEXT("live"));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("VA: added %d simple + %d mesh primitives (%d actors skipped, no material)"), SimpleCount, MeshCount, SkippedCount);
}

void AVAudioWorld::DestroyPrimitives()
{
	for (VAMeshPrimitive*    actorPosition : MeshPrimitives)    { vaWorldRemovePrimitive_(World, P); vaMeshPrimitiveDestroy(P); }
	for (VACapsulePrimitive* actorPosition : CapsulePrimitives) { vaWorldRemovePrimitive_(World, P); vaCapsulePrimitiveDestroy(P); }
	for (VASpherePrimitive*  P : SpherePrimitives)  { vaWorldRemovePrimitive_(World, P); vaSpherePrimitiveDestroy(P); }
	for (VAPrismPrimitive*   P : PrismPrimitives)   { vaWorldRemovePrimitive_(World, P); vaPrismPrimitiveDestroy(P); }

	MeshPrimitives.Empty();
	CapsulePrimitives.Empty();
	SpherePrimitives.Empty();
	PrismPrimitives.Empty();
}
