#include "VAudioWorld.h"
#include "VAudioMaterialComponent.h"
#include "VAConstants.h"
#include "VARawLog.h"

#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"

extern "C" {
#include "vaudio.h"
}

static VAMatrix MakeTranslationMatrix(const FVector& P)
{
	return vaMatrixCreateTranslation((float)P.X, (float)P.Y, (float)P.Z);
}

// Rotation + translation only, no scale - used for primitives whose SetTransform doc explicitly disallows scale components (capsule/prism/etc - see vaudio.h).
// Non-uniform scale on these shapes is instead applied via their own dedicated SetRadius/SetLength/SetSize calls.
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

// Scale + rotation + translation - only VAMeshPrimitive's SetTransform supports a scale  component (see vaMatrixCreateScale's comment in vaudio.h), so this is not safe to use for any other primitive type.
static VAMatrix MakeScaleRotTransMatrix(const FTransform& T)
{
	VAMatrix RotTrans = MakeRotTransMatrix(T);
	FVector Scale = T.GetScale3D();
	VAMatrix ScaleMat = vaMatrixCreateScale((float)Scale.X, (float)Scale.Y, (float)Scale.Z);
	return vaMatrixMultiply(&ScaleMat, &RotTrans);
}

// vaudio.h defines VAResult codes as plain #defines (not an enum), so there's no reflection -
// only the codes vaWorldAddPrimitive_ can actually return are named here.
static const TCHAR* VAResultToString(VAResult Result)
{
	switch (Result)
	{
		case VA_SUCCESS:                  return TEXT("VA_SUCCESS");
		case VA_INVALID_MATERIAL:         return TEXT("VA_INVALID_MATERIAL (primitive's material is VAMaterialAir)");
		case VA_MATERIAL_DOES_NOT_EXIST:  return TEXT("VA_MATERIAL_DOES_NOT_EXIST (primitive's material does not exist)");
		case VA_ALREADY_EXISTS:           return TEXT("VA_ALREADY_EXISTS (primitive already added to this or another world)");
		case VA_WORLD_CONFLICT:           return TEXT("VA_WORLD_CONFLICT (mesh already in use by a different world)");
		default:                          return TEXT("unknown VAResult");
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
		VALog(L"No Unreal World found (open a level first).");
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
				VALog(L"Mesh '%s' on '%s' has no render data in-editor, skipping", *Mesh->GetName(), *Actor->GetActorNameOrLabel());
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

			VALog(L"Baked '%s'.'%s' tris=%d", *Actor->GetActorNameOrLabel(), *MeshComp->GetName(), Indices.Num() / 3);
		}
	}

	MarkPackageDirty();
	VALog(L"Baked %d mesh component(s). Save the level to persist.", BakedCount);
}
#endif

bool AVAudioWorld::TryAddPrimitive(void* Primitive, const TCHAR* PrimitiveTypeName, const FString& ActorName)
{
	VAResult Result = vaWorldAddPrimitive_(GetVAWorld(), Primitive);

	if (Result == VA_SUCCESS)
		return true;

	VALog(L"'%s': failed to add %s primitive to raytracing - %s.", *ActorName, PrimitiveTypeName, VAResultToString(Result));
	ActorsWithInvalidMaterials.AddUnique(ActorName);
	return false;
}

void AVAudioWorld::ScanAndAddPrimitives()
{
	UWorld* UEWorld = GetWorld();

	// Null if this actor isn't in a live level (e.g. called outside BeginPlay/PIE).
	if (!UEWorld)
		return;

	int32 SimpleCount = 0;
	int32 MeshCount = 0;
	int32 SkippedCount = 0;

	ActorsWithInvalidMaterials.Empty();

	for (TActorIterator<AActor> ActorIt(UEWorld); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;

		UVAudioMaterialComponent* MatComp = FindMaterialInChain(Actor);
		if (!MatComp)
		{
			++SkippedCount;
			continue;
		}

		FString ActorName = Actor->GetActorNameOrLabel();

		// Unassigned AudioWorld is a config problem worth its own warning, same as a wrong-world
		// assignment below - both mean this actor's geometry won't be added to raytracing.
		if (!MatComp->AudioWorld)
		{
			VALog(L"'%s' has a VAudioMaterialComponent with no AudioWorld assigned - its geometry will not be added to raytracing until one is set.", *ActorName);
			ActorsWithInvalidMaterials.AddUnique(ActorName);
			++SkippedCount;
			continue;
		}

		if (MatComp->AudioWorld != this)
		{
			++SkippedCount;
			continue;
		}

		int32 MaterialId;
		if (!MatComp->GetMaterialId(MaterialId))
		{
			// GetMaterialId() already logged the specific reason (e.g. MaterialAsset isn't in
			// this world's Materials array) - this actor is a config problem, not a normal
			// "no material component" skip, so it gets its own on-screen warning (see Tick()).
			ActorsWithInvalidMaterials.AddUnique(ActorName);
			++SkippedCount;
			continue;
		}

		VAMaterialType Material = (VAMaterialType)MaterialId;

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

				if (!TryAddPrimitive(Cap, TEXT("capsule"), ActorName))
				{
					vaCapsulePrimitiveDestroy(Cap);
					continue;
				}

				CapsulePrimitives.Add(Cap);
				BindPrimitiveToComponent(Cap, EVAudioPrimitiveKind::Capsule, ShapeComp);
				++SimpleCount;
			}
			else if (USphereComponent* Sphere = Cast<USphereComponent>(ShapeComp))
			{
				FVector Center = CompTransform.GetTranslation();

				VASpherePrimitive* Sp = vaSpherePrimitiveCreate();
				vaSpherePrimitiveSetCenterUnreal(Sp, Center);
				vaSpherePrimitiveSetRadius(Sp,   Sphere->GetScaledSphereRadius());
				vaSpherePrimitiveSetMaterial(Sp, Material);

				if (!TryAddPrimitive(Sp, TEXT("sphere"), ActorName))
				{
					vaSpherePrimitiveDestroy(Sp);
					continue;
				}

				SpherePrimitives.Add(Sp);
				BindPrimitiveToComponent(Sp, EVAudioPrimitiveKind::Sphere, ShapeComp);
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

				if (!TryAddPrimitive(Prism, TEXT("box"), ActorName))
				{
					vaPrismPrimitiveDestroy(Prism);
					continue;
				}

				PrismPrimitives.Add(Prism);
				BindPrimitiveToComponent(Prism, EVAudioPrimitiveKind::Prism, ShapeComp);
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

					if (!TryAddPrimitive(Cap, TEXT("capsule"), ActorName))
					{
						vaCapsulePrimitiveDestroy(Cap);
						continue;
					}

					CapsulePrimitives.Add(Cap);
					BindPrimitiveToComponent(Cap, EVAudioPrimitiveKind::CapsuleFromMesh, MeshComp,
						FTransform(Sphyl.GetTransform().GetRotation(), Sphyl.GetTransform().GetTranslation()),
						FVector(Sphyl.Radius, 0.f, Sphyl.Length));
					bAddedSimple = true;
					++SimpleCount;
				}

				for (const FKSphereElem& Sphere : Agg.SphereElems)
				{
					FVector Center = CompTransform.TransformPosition(Sphere.GetTransform().GetTranslation());
					float   Radius = Sphere.Radius * Scale.GetAbsMax();

					VASpherePrimitive* Sp = vaSpherePrimitiveCreate();
					vaSpherePrimitiveSetCenterUnreal(Sp, Center);
					vaSpherePrimitiveSetRadius(Sp,   Radius);
					vaSpherePrimitiveSetMaterial(Sp, Material);

					if (!TryAddPrimitive(Sp, TEXT("sphere"), ActorName))
					{
						vaSpherePrimitiveDestroy(Sp);
						continue;
					}

					SpherePrimitives.Add(Sp);
					BindPrimitiveToComponent(Sp, EVAudioPrimitiveKind::SphereFromMesh, MeshComp,
						FTransform(Sphere.GetTransform().GetTranslation()),
						FVector(Sphere.Radius, 0.f, 0.f));
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

					if (!TryAddPrimitive(Prism, TEXT("box"), ActorName))
					{
						vaPrismPrimitiveDestroy(Prism);
						continue;
					}

					PrismPrimitives.Add(Prism);
					BindPrimitiveToComponent(Prism, EVAudioPrimitiveKind::PrismFromMesh, MeshComp,
						FTransform(Box.GetTransform().GetRotation(), Box.GetTransform().GetTranslation()),
						FVector(Box.X, Box.Y, Box.Z));
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
					VALog(L"Mesh '%s' has no render data and no baked geometry, skipping. Run 'Bake Geometry For Shipping' on the VA World and save the level.", *Mesh->GetName());
					continue;
				}

				FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
				FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;

				TArray<uint32> Indices;
				LOD.IndexBuffer.GetCopy(Indices);
				if (Indices.IsEmpty())
				{
					VALog(L"Mesh '%s' has no index data and no baked geometry, skipping. Run 'Bake Geometry For Shipping' on the VA World and save the level.", *Mesh->GetName());
					continue;
				}

				LocalPositions.Reserve(Indices.Num());
				for (uint32 Idx : Indices)
					LocalPositions.Add(PosBuffer.VertexPosition(Idx));
			}

			// Kept in pure local (component) space, with no rotation/scale baked in - unlike the
			// simple-collision primitives above, VAMeshPrimitive's transform matrix supports a
			// full scale component (see MakeScaleRotTransMatrix), so rotation/scale/translation
			// can all be driven live through vaMeshPrimitiveSetTransform instead of requiring the
			// vertex buffer to be rebuilt whenever the actor moves.
			TArray<VAVector> VAVerts;
			VAVerts.Reserve(LocalPositions.Num());
			VAVector MinB = VECTOR_MAX;
			VAVector MaxB = VECTOR_MIN;

			for (const FVector3f& LocalPos : LocalPositions)
			{
				VAVector V = vaVectorCreate(LocalPos.X, LocalPos.Y, LocalPos.Z);
				VAVerts.Add(V);
				MinB = vaVectorMin(MinB, V);
				MaxB = vaVectorMax(MaxB, V);
			}

			VAMatrix Transform = MakeScaleRotTransMatrix(CompTransform);
			VAMeshPrimitive* MeshPrim;

			VAResult result = vaMeshPrimitiveCreate(
				Material, VAVerts.GetData(), VAVerts.Num(), MinB, MaxB, &Transform, &MeshPrim
			);

			vaMeshPrimitiveSetSupports3DPermeation(MeshPrim, MatComp->bSupports3DPermeation);

			if (!TryAddPrimitive(MeshPrim, TEXT("mesh"), ActorName))
			{
				vaMeshPrimitiveDestroy(MeshPrim);
				continue;
			}

			MeshPrimitives.Add(MeshPrim);
			BindPrimitiveToComponent(MeshPrim, EVAudioPrimitiveKind::Mesh, MeshComp);
			++MeshCount;
		}
	}

	VALog(L"Added %d simple + %d mesh primitives (%d actors skipped, no material)", SimpleCount, MeshCount, SkippedCount);
}
