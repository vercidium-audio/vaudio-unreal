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
	return vaMatrixCreateTranslation((float)P.X, (float)P.Z, -(float)P.Y);
}

static VAMatrix MakeRotTransMatrix(const FTransform& T)
{
	FQuat Q = T.GetRotation();
	FVector P = T.GetTranslation();

	FVector AxX = Q.GetAxisX();
	FVector AxY = Q.GetAxisY();
	FVector AxZ = Q.GetAxisZ();

	// UE(X,Y,Z) → VA(X,Z,-Y)
	float c0x =  (float)AxX.X, c0y =  (float)AxX.Z, c0z = -(float)AxX.Y;
	float c1x =  (float)AxZ.X, c1y =  (float)AxZ.Z, c1z = -(float)AxZ.Y;
	float c2x = -(float)AxY.X, c2y = -(float)AxY.Z, c2z =  (float)AxY.Y;

	return vaMatrixCreate(
		c0x, c0y, c0z, 0.f,
		c1x, c1y, c1z, 0.f,
		c2x, c2y, c2z, 0.f,
		(float)P.X, (float)P.Z, -(float)P.Y, 1.f
	);
}

// ---------------------------------------------------------------------------

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
	// VA coord system: X=UE_X, Y=UE_Z, Z=-UE_Y
	// The Z axis is negated, so the min corner's Z = -(UE_Y_max) = -(WorldPosition.Y + WorldSize.Y)
	vaWorldSetPosition(World, vaVectorCreate(
		(float)WorldPosition.X,
		(float)WorldPosition.Z,
		-(float)(WorldPosition.Y + WorldSize.Y)));
	vaWorldSetSize(World, vaVectorCreate(
		(float)WorldSize.X,
		(float)WorldSize.Z,
		(float)WorldSize.Y));
	vaWorldSetInverseSpeedOfSound(World, 1.0f / SpeedOfSound);
	vaWorldSetMetersPerUnit(World, MetersPerUnit);
	vaWorldSetWorldIsIndoors(World, bIsIndoors);
	vaWorldSetEpsilon(World, Epsilon);
	vaWorldSetEmittersOutsideTheWorldAreMuffled(World, bEmittersOutsideTheWorldAreMuffled);
	vaWorldSetWorkItemCount(World, FMath::Max(1, WorkItemCount));
	vaWorldSetMaximumConcurrencyLevel(World, FMath::Max(1, MaximumConcurrencyLevel));
	if (bPendingShutdown)
		vaWorldSetPendingShutdown(World, true);
	vaWorldSetReferenceFrequencyLF(World, ReferenceFrequencyLF);
	vaWorldSetReferenceFrequencyHF(World, ReferenceFrequencyHF);
	vaWorldSetAirAbsorptionHumidity(World, Humidity);
	vaWorldSetAirAbsorptionTemperature(World, Temperature);
	vaWorldSetAirAbsorptionPressure(World, Pressure);

	int32 GroupedEAXCount = FMath::Max(2, GroupedEAXSubmixes.Num());
	vaWorldSetMaximumGroupedEAXCount(World, GroupedEAXCount);
	UE_LOG(LogTemp, Log, TEXT("VA World: MaximumGroupedEAXCount=%d (submix slots=%d)"), GroupedEAXCount, GroupedEAXSubmixes.Num());

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

		bool bDryEnabled = !bReverbOnly;
		for (AVAudioEmitter* E : RegisteredEmitters)
		{
			if (E) E->SetDryOutputEnabled(bDryEnabled);
		}

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1001, 0.0f, FColor::Cyan,
				FString::Printf(TEXT("VA Raytracing: %.3f ms"), vaWorldGetRaytracingTime(World)));

			for (AVAudioEmitter* E : RegisteredEmitters)
			{
				if (!E || !E->bIsMainListener) continue;

				FVector P = E->GetActorLocation();
				GEngine->AddOnScreenDebugMessage(1002, 0.0f, FColor::Green,
					FString::Printf(TEXT("VA Listener pos: (%.1f, %.1f, %.1f)"), P.X, P.Y, P.Z));

				VALowPassFilter* F = vaEmitterGetAmbientFilter(E->GetVAEmitter());
				if (F)
				{
					GEngine->AddOnScreenDebugMessage(1003, 0.0f, FColor::Yellow,
						FString::Printf(TEXT("VA Ambient LPF: gainLF=%.3f  gainHF=%.3f"), F->gainLF, F->gainHF));
				}
				break;
			}

			// Per-emitter position and world-bounds check
			const FVector BoundsMin = WorldPosition;
			const FVector BoundsMax = WorldPosition + WorldSize;
			for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
			{
				AVAudioEmitter* E = RegisteredEmitters[i];
				if (!E) continue;

				FVector P = E->GetActorLocation();
				bool bInBounds = P.X >= BoundsMin.X && P.X <= BoundsMax.X
				              && P.Y >= BoundsMin.Y && P.Y <= BoundsMax.Y
				              && P.Z >= BoundsMin.Z && P.Z <= BoundsMax.Z;

				FColor Color = bInBounds ? FColor::Green : FColor::Red;
				GEngine->AddOnScreenDebugMessage(2000 + i, 0.0f, Color,
					FString::Printf(TEXT("VA Emitter[%d] '%s': (%.1f, %.1f, %.1f) %s"),
						i, *E->GetActorLabel(), P.X, P.Y, P.Z,
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
	UE_LOG(LogTemp, Log, TEXT("VA exported world to: %s"), *Path);
}

// ---------------------------------------------------------------------------
// Child material actors — AVAudioMaterial children of this world actor
// ---------------------------------------------------------------------------

void AVAudioWorld::ApplyChildMaterials()
{
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);

	for (AActor* Child : AttachedActors)
	{
		AVAudioMaterial* Mat = Cast<AVAudioMaterial>(Child);
		if (Mat)
			Mat->ApplyToWorld(World);
	}
}

// ---------------------------------------------------------------------------
// Primitive scan — finds every actor with UVAudioMaterialComponent
// ---------------------------------------------------------------------------

// Walk the attach-parent chain to find the nearest UVAudioMaterialComponent.
static UVAudioMaterialComponent* FindMaterialInChain(AActor* Actor)
{
	for (AActor* A = Actor; A != nullptr; A = A->GetAttachParentActor())
	{
		UVAudioMaterialComponent* C = A->FindComponentByClass<UVAudioMaterialComponent>();
		if (C)
		{
			if (A != Actor)
				UE_LOG(LogTemp, Log, TEXT("VA:   '%s' inherits material from parent '%s'"),
					*Actor->GetActorLabel(), *A->GetActorLabel());
			return C;
		}
	}
	return nullptr;
}

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
				*Actor->GetActorLabel());
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("VA: actor '%s' -> material %d (%d mesh components)"),
			*Actor->GetActorLabel(), (int32)Material, MeshComps.Num());

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
					vaSpherePrimitiveSetCenter(Sp, vaVectorCreate((float)Center.X, (float)Center.Z, -(float)Center.Y));
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
					vaPrismPrimitiveSetSize(Prism, vaVectorCreate(Box.X * Scale.X, Box.Z * Scale.Z, Box.Y * Scale.Y));
					vaPrismPrimitiveSetMaterial(Prism, Material);
					vaPrismPrimitiveSetTransform(Prism, &Mat);
					vaWorldAddPrimitive_(World, Prism);
					PrismPrimitives.Add(Prism);
					bAddedSimple = true;
					++SimpleCount;
				}
			}

			if (bAddedSimple) continue;

			// Fall back to triangle mesh
			if (!Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty()) continue;

			FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
			FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;

			TArray<uint32> Indices;
			LOD.IndexBuffer.GetCopy(Indices);
			if (Indices.IsEmpty()) continue;

			TArray<VAVector> VAVerts;
			VAVerts.Reserve(Indices.Num());
			VAVector MinB = VECTOR_MAX;
			VAVector MaxB = VECTOR_MIN;

			for (uint32 Idx : Indices)
			{
				FVector3f LocalPos = PosBuffer.VertexPosition(Idx);
				FVector RotScaled  = CompTransform.GetRotation().RotateVector(Scale * FVector(LocalPos));
				VAVector V = vaVectorCreate((float)RotScaled.X, (float)RotScaled.Z, -(float)RotScaled.Y);
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

			UE_LOG(LogTemp, Log, TEXT("VA:   mesh '%s' tris=%d"), *Mesh->GetName(), Indices.Num() / 3);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("VA: added %d simple + %d mesh primitives (%d actors skipped, no material)"), SimpleCount, MeshCount, SkippedCount);
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
