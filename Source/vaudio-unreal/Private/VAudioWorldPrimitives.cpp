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

// True if this mesh would use its simple collision (sphyl/vaSphere/box) rather than the
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
		if (!MatComp || MatComp->GetVAWorld() != this) continue;

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
	UWorld* ueWorld = GetWorld();

	// Null if this actor isn't in a live level (e.g. called outside BeginPlay/PIE).
	if (!ueWorld)
		return;

	int32 meshCount = 0;
	int32 skippedCount = 0;

	ActorsWithInvalidMaterials.Empty();

	for (TActorIterator<AActor> actorIterator(ueWorld); actorIterator; ++actorIterator)
	{
		AActor* actor = *actorIterator;

		UVAudioMaterialComponent* materialComp = FindMaterialInChain(actor);
		if (!materialComp)
		{
			++skippedCount;
			continue;
		}

		FString actorName = actor->GetActorNameOrLabel();

		// Validate materials
		if (!materialComp->GetVAWorld())
		{
			ActorsWithInvalidMaterials.AddUnique(actorName);
			++skippedCount;
			continue;
		}

		// Ignore materials assigned to other worlds
		if (materialComp->GetVAWorld() != this)
		{
			++skippedCount;
			continue;
		}

		int32 MaterialId;
		if (!materialComp->GetMaterialId(MaterialId))
		{
			ActorsWithInvalidMaterials.AddUnique(actorName);
			++skippedCount;
			continue;
		}

		VAMaterialType vaMaterialType = (VAMaterialType)MaterialId;

		TArray<UShapeComponent*> shapeComponents;
		actor->GetComponents<UShapeComponent>(shapeComponents);

		for (UShapeComponent* shapeComp : shapeComponents)
		{
			FTransform shapeCompTransform = shapeComp->GetComponentTransform();

			if (USphereComponent* sphereComp = Cast<USphereComponent>(shapeComp))
			{
				VASpherePrimitive* vaSphere = vaSpherePrimitiveCreate();

				vaSpherePrimitiveSetCenterUnreal(vaSphere, shapeCompTransform.GetTranslation());
				vaSpherePrimitiveSetRadius(vaSphere, sphereComp->GetScaledSphereRadius());
				vaSpherePrimitiveSetMaterial(vaSphere, vaMaterialType);

				if (!TryAddPrimitive(vaSphere, TEXT("sphere"), actorName))
				{
					vaSpherePrimitiveDestroy(vaSphere);
					continue;
				}

				SpherePrimitives.Add(vaSphere);
				BindPrimitiveToComponent(vaSphere, EVAudioPrimitiveKind::Sphere, shapeComp);
			}
			else if (UCapsuleComponent* capsuleComp = Cast<UCapsuleComponent>(shapeComp))
			{
				VACapsulePrimitive* vaCapsule = vaCapsulePrimitiveCreate();

				vaCapsulePrimitiveSetRadius(vaCapsule, capsuleComp->GetScaledCapsuleRadius());
				vaCapsulePrimitiveSetLength(vaCapsule, capsuleComp->GetScaledCapsuleHalfHeight_WithoutHemisphere() * 2.0f);
				vaCapsulePrimitiveSetMaterial(vaCapsule, vaMaterialType);
				vaCapsulePrimitiveSetTransformUnreal(vaCapsule, shapeCompTransform);

				if (!TryAddPrimitive(vaCapsule, TEXT("capsule"), actorName))
				{
					vaCapsulePrimitiveDestroy(vaCapsule);
					continue;
				}

				CapsulePrimitives.Add(vaCapsule);
				BindPrimitiveToComponent(vaCapsule, EVAudioPrimitiveKind::Capsule, shapeComp);
			}			 
			else if (UBoxComponent* boxComp = Cast<UBoxComponent>(shapeComp))
			{
				FVector extents = boxComp->GetScaledBoxExtent();

				VAPrismPrimitive* vaPrism = vaPrismPrimitiveCreate();
				vaPrismPrimitiveSetSize(vaPrism, vaVectorCreate(extents.X * 2.0f, extents.Y * 2.0f, extents.Z * 2.0f));
				vaPrismPrimitiveSetMaterial(vaPrism, vaMaterialType);
				vaPrismPrimitiveSetTransformUnreal(vaPrism, shapeCompTransform);

				if (!TryAddPrimitive(vaPrism, TEXT("box"), actorName))
				{
					vaPrismPrimitiveDestroy(vaPrism);
					continue;
				}

				PrismPrimitives.Add(vaPrism);
				BindPrimitiveToComponent(vaPrism, EVAudioPrimitiveKind::Prism, shapeComp);
			}
		}

		TArray<UStaticMeshComponent*> meshComps;
		actor->GetComponents<UStaticMeshComponent>(meshComps);

		for (UStaticMeshComponent* meshComp : meshComps)
		{
			UStaticMesh* staticMesh = meshComp->GetStaticMesh();

			// Ignore components with no meshes
			if (!staticMesh)
				continue;

			FTransform meshCompTransform = meshComp->GetComponentTransform();
			FVector scale = meshCompTransform.GetScale3D();

			bool bAddedSimple = false;
			UBodySetup* bodySetup = staticMesh->GetBodySetup();

			// If this mesh is composed of multiple prisms/capsules/spheres, add them all
			if (bodySetup)
			{
				const FKAggregateGeom& agg = bodySetup->AggGeom;

				for (const FKSphereElem& sphereElem : agg.SphereElems)
				{
					FVector center = meshCompTransform.TransformPosition(sphereElem.GetTransform().GetTranslation());
					float radius = sphereElem.Radius * scale.GetAbsMax();

					VASpherePrimitive* vaSphere = vaSpherePrimitiveCreate();
					vaSpherePrimitiveSetCenterUnreal(vaSphere, center);
					vaSpherePrimitiveSetRadius(vaSphere, radius);
					vaSpherePrimitiveSetMaterial(vaSphere, vaMaterialType);

					if (!TryAddPrimitive(vaSphere, TEXT("sphere"), actorName))
					{
						vaSpherePrimitiveDestroy(vaSphere);
						continue;
					}

					bAddedSimple = true;
					SpherePrimitives.Add(vaSphere);

					BindPrimitiveToComponent(vaSphere, EVAudioPrimitiveKind::SphereFromMesh, meshComp,
						FTransform(sphereElem.GetTransform().GetTranslation()),
						FVector(sphereElem.Radius, 0.f, 0.f));
				}

				for (const FKBoxElem& boxElem : agg.BoxElems)
				{
					FQuat rot = boxElem.GetTransform().GetRotation() * meshCompTransform.GetRotation();
					FVector center = meshCompTransform.TransformPosition(boxElem.GetTransform().GetTranslation());
					FTransform worldTransform(rot, center, FVector::OneVector);

					VAPrismPrimitive* Prism = vaPrismPrimitiveCreate();
					vaPrismPrimitiveSetSize(Prism, vaVectorCreate(boxElem.X * scale.X, boxElem.Y * scale.Y, boxElem.Z * scale.Z));
					vaPrismPrimitiveSetMaterial(Prism, vaMaterialType);
					vaPrismPrimitiveSetTransformUnreal(Prism, worldTransform);

					if (!TryAddPrimitive(Prism, TEXT("box"), actorName))
					{
						vaPrismPrimitiveDestroy(Prism);
						continue;
					}

					bAddedSimple = true;
					PrismPrimitives.Add(Prism);

					BindPrimitiveToComponent(Prism, EVAudioPrimitiveKind::PrismFromMesh, meshComp,
						FTransform(boxElem.GetTransform().GetRotation(), boxElem.GetTransform().GetTranslation()),
						FVector(boxElem.X, boxElem.Y, boxElem.Z));
				}
				for (const FKSphylElem& capsuleElem : agg.SphylElems)
				{
					FQuat rot = capsuleElem.GetTransform().GetRotation() * meshCompTransform.GetRotation();
					FVector center = meshCompTransform.TransformPosition(capsuleElem.GetTransform().GetTranslation());
					FTransform worldTransform(rot, center, FVector::OneVector);

					VACapsulePrimitive* vaCapsule = vaCapsulePrimitiveCreate();

					vaCapsulePrimitiveSetRadius(vaCapsule, capsuleElem.Radius * FMath::Max(scale.X, scale.Y));
					vaCapsulePrimitiveSetLength(vaCapsule, capsuleElem.Length * scale.Z);
					vaCapsulePrimitiveSetMaterial(vaCapsule, vaMaterialType);
					vaCapsulePrimitiveSetTransformUnreal(vaCapsule, worldTransform);

					if (!TryAddPrimitive(vaCapsule, TEXT("capsule"), actorName))
					{
						vaCapsulePrimitiveDestroy(vaCapsule);
						continue;
					}

					bAddedSimple = true;
					CapsulePrimitives.Add(vaCapsule);

					BindPrimitiveToComponent(vaCapsule, EVAudioPrimitiveKind::CapsuleFromMesh, meshComp,
						FTransform(capsuleElem.GetTransform().GetRotation(), capsuleElem.GetTransform().GetTranslation()),
						FVector(capsuleElem.Radius, 0.f, capsuleElem.Length));
				}

			}

			if (bAddedSimple)
				continue;

			// Attempt to use baked geometry. Fall back to mesh data (may be unavailable in cooked builds)
			const FVAudioBakedMesh* bakedMesh = nullptr;

			for (const FVAudioBakedMesh& bakedMeshTemp : BakedMeshes)
			{
				if (bakedMeshTemp.ComponentName == meshComp->GetFName() && bakedMeshTemp.ActorName == actor->GetName())
				{
					bakedMesh = &bakedMeshTemp;
					break;
				}
			}

			TArray<FVector3f> localVertices;

			if (bakedMesh)
			{
				localVertices = bakedMesh->Vertices;
			}
			else
			{
				if (!staticMesh->GetRenderData() || staticMesh->GetRenderData()->LODResources.IsEmpty())
				{
					VALog(L"Mesh '%s' will not affect raytracing as it has no baked geometry and no render mesh data. Run 'Bake Geometry For Shipping' on the VA World and save the level.", *staticMesh->GetName());
					continue;
				}

				// Attempt to access index and position data
				// TODO - which LOD to use? we don't need full-quality meshes for audio raytracing. Maybe let the user decide? Also need to check baking - need to let the user decide which LOD to use for each mesh, or set a default LOD for all meshes
				FStaticMeshLODResources& lod = staticMesh->GetRenderData()->LODResources[0];
				FPositionVertexBuffer& positionBuffer = lod.VertexBuffers.PositionVertexBuffer;

				TArray<uint32> indices;
				lod.IndexBuffer.GetCopy(indices);

				if (indices.IsEmpty())
				{
					VALog(L"Mesh '%s' will not affect raytracing as it has no baked geometry and no render mesh data. Run 'Bake Geometry For Shipping' on the VA World and save the level.", *staticMesh->GetName());
					continue;
				}

				localVertices.Reserve(indices.Num());

				for (uint32 i : indices)
					localVertices.Add(positionBuffer.VertexPosition(i));
			}

			// Iterate over all vertices and extract min/max bounds
			TArray<VAVector> vaVertices;
			vaVertices.Reserve(localVertices.Num());

			VAVector minBounds = VECTOR_MAX;
			VAVector maxBounds = VECTOR_MIN;

			for (const FVector3f& localPosition : localVertices)
			{
				VAVector vaPosition = vaVectorCreate(localPosition.X, localPosition.Y, localPosition.Z);
				vaVertices.Add(vaPosition);

				minBounds = vaVectorMin(minBounds, vaPosition);
				maxBounds = vaVectorMax(maxBounds, vaPosition);
			}

			// Create the prism
			VAMatrix vaTransform = MakeScaleRotTransMatrix(meshCompTransform);
			VAMeshPrimitive* vaMeshPrimitive;

			VAResult result = vaMeshPrimitiveCreate(vaMaterialType, vaVertices.GetData(), vaVertices.Num(), minBounds, maxBounds, &vaTransform, &vaMeshPrimitive);

			if (result == VA_SUCCESS)
			{
				// TODO - supports3DPermeation should be a per-mesh thing? e.g. a rock terrain heightmap doesnt support permeation, but a 3D watertight rock mesh does
				vaMeshPrimitiveSetSupports3DPermeation(vaMeshPrimitive, materialComp->bSupports3DPermeation);

				if (!TryAddPrimitive(vaMeshPrimitive, TEXT("mesh"), actorName))
				{
					vaMeshPrimitiveDestroy(vaMeshPrimitive);
					continue;
				}

				MeshPrimitives.Add(vaMeshPrimitive);
				BindPrimitiveToComponent(vaMeshPrimitive, EVAudioPrimitiveKind::Mesh, meshComp);
				meshCount++;
			}
			else
			{
				// TODO - error codes
			}
		}
	}

	int32 simpleCount = PrismPrimitives.Num() + CapsulePrimitives.Num() + SpherePrimitives.Num();

	VALog(L"Added %d simple + %d mesh primitives (%d actors skipped, no material)", simpleCount, meshCount, skippedCount);
}
