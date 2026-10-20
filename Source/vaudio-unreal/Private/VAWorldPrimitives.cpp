#include "VAWorld.h"
#include "VAMaterialComponent.h"
#include "VAConstants.h"
#include "VALog.h"

#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "Engine/StaticMesh.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeDataAccess.h"
#include "Chaos/HeightField.h"
#include "UObject/ObjectSaveContext.h"

extern "C" {
#include "vaudio.h"
}

// Walk the attach-parent chain to find the nearest UVAMaterialComponent.
static UVAMaterialComponent* FindMaterialInChain(AActor* Actor)
{
	for (AActor* actor = Actor; actor != nullptr; actor = actor->GetAttachParentActor())
	{
		UVAMaterialComponent* materialComponent = actor->FindComponentByClass<UVAMaterialComponent>();

		if (materialComponent)
			return materialComponent;
	}
	return nullptr;
}

// How far below its lowest point each landscape component's block of ground extends, in cm
static const double LandscapeThickness = 100.0;

// True if this mesh would use its simple collision rather than its render triangles
static bool HasSimpleCollision(UStaticMesh* Mesh)
{
	UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;

	if (!BodySetup)
		return false;

	const FKAggregateGeom& Agg = BodySetup->AggGeom;
	return !Agg.SphylElems.IsEmpty() || !Agg.SphereElems.IsEmpty() || !Agg.BoxElems.IsEmpty() || !Agg.ConvexElems.IsEmpty();
}

// One LOD's triangle-list vertices in mesh space, i.e. vertices[i] is triangle-list index i
static bool GetRenderVertices(UStaticMesh* mesh, int32 lod, TArray<FVector3f>& vertices)
{
	FStaticMeshRenderData* renderData = mesh->GetRenderData();

	if (!renderData || renderData->LODResources.IsEmpty())
		return false;

	FStaticMeshLODResources& lodResources = renderData->LODResources[FMath::Clamp(lod, 0, renderData->LODResources.Num() - 1)];

	TArray<uint32> indices;
	lodResources.IndexBuffer.GetCopy(indices);

	if (indices.IsEmpty())
		return false;

	vertices.Reset(indices.Num());

	for (uint32 index : indices)
		vertices.Add(lodResources.VertexBuffers.PositionVertexBuffer.VertexPosition(index));

	return true;
}

#if WITH_EDITOR
// Calls visit for every mesh component BakeGeometry bakes, i.e. the ones that add their render triangles, with the LOD they use
static void ForEachBakeTarget(UWorld* UEWorld, TFunctionRef<void(AActor*, UStaticMeshComponent*, UStaticMesh*, int32)> visit)
{
	for (TActorIterator<AActor> ActorIt(UEWorld); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;

		// Null if this actor (or its attach-parent chain) has no UVAMaterialComponent. A level has one VAWorld, so every material component belongs to this one
		UVAMaterialComponent* MatComp = FindMaterialInChain(Actor);
		if (!MatComp || MatComp->PropagateMode == EVAPropagateMode::Colliders) continue;

		TArray<UStaticMeshComponent*> MeshComps;
		Actor->GetComponents<UStaticMeshComponent>(MeshComps);

		for (UStaticMeshComponent* MeshComp : MeshComps)
		{
			UStaticMesh* Mesh = MeshComp->GetStaticMesh();

			// Meshes with simple collision use it instead of their triangles, unless only visuals are added
			if (!Mesh || (HasSimpleCollision(Mesh) && MatComp->PropagateMode != EVAPropagateMode::Visuals)) continue;

			visit(Actor, MeshComp, Mesh, MatComp->MeshLOD);
		}
	}
}

void AVAWorld::BakeGeometry()
{
	UWorld* UEWorld = GetWorld();

	if (!UEWorld)
	{
		VA_WARN_NAMED(TEXT("No Unreal World found (open a level first)."));
		return;
	}

	Modify();
	BakedMeshes.Reset();

	int32 BakedCount = 0;

	ForEachBakeTarget(UEWorld, [&](AActor* Actor, UStaticMeshComponent* MeshComp, UStaticMesh* Mesh, int32 LOD)
	{
		FVABakedMesh& Baked = BakedMeshes.AddDefaulted_GetRef();

		if (!GetRenderVertices(Mesh, LOD, Baked.Vertices))
		{
			VA_WARN_NAMED(TEXT("Mesh '%s' on '%s' has no render data in-editor, skipping"), *Mesh->GetName(), *Actor->GetActorNameOrLabel());
			BakedMeshes.Pop();
			return;
		}

		Baked.ActorName = Actor->GetName();
		Baked.ComponentName = MeshComp->GetFName();

		++BakedCount;

		VA_LOG_NAMED(TEXT("Baked '%s'.'%s' tris=%d"), *Actor->GetActorNameOrLabel(), *MeshComp->GetName(), Baked.Vertices.Num() / 3);
	});

	MarkPackageDirty();
	VA_LOG_NAMED(TEXT("Baked %d mesh component(s). Save the level to persist."), BakedCount);
}

bool AVAWorld::IsBakedGeometryStale(FString* OutReason) const
{
	UWorld* UEWorld = GetWorld();

	// Never baked, so there's nothing to go stale
	if (!UEWorld || BakedMeshes.IsEmpty())
		return false;

	TArray<bool> matched;
	matched.SetNumZeroed(BakedMeshes.Num());
	FString reason;

	ForEachBakeTarget(UEWorld, [&](AActor* actor, UStaticMeshComponent* meshComponent, UStaticMesh* mesh, int32 lod)
	{
		if (!reason.IsEmpty())
			return;

		int32 index = BakedMeshes.IndexOfByPredicate([&](const FVABakedMesh& baked) { return baked.ComponentName == meshComponent->GetFName() && baked.ActorName == actor->GetName(); });
		TArray<FVector3f> vertices;

		// A mesh with no render data can't be compared, and is what the bake is for
		if (!GetRenderVertices(mesh, lod, vertices))
		{
			if (index != INDEX_NONE)
				matched[index] = true;
		}
		else if (index == INDEX_NONE)
			reason = FString::Printf(TEXT("'%s'.'%s' isn't baked"), *actor->GetActorNameOrLabel(), *meshComponent->GetName());
		else if (BakedMeshes[index].Vertices != vertices)
			reason = FString::Printf(TEXT("'%s'.'%s' has changed since it was baked (%d baked triangles, %d now)"), *actor->GetActorNameOrLabel(), *meshComponent->GetName(), BakedMeshes[index].Vertices.Num() / 3, vertices.Num() / 3);
		else
			matched[index] = true;
	});

	// World Partition only bakes loaded actors, so a baked entry with no match may just be in an unloaded cell
	if (reason.IsEmpty() && !UEWorld->IsPartitionedWorld())
	{
		int32 unmatched = matched.Find(false);

		if (unmatched != INDEX_NONE)
			reason = FString::Printf(TEXT("'%s'.'%s' is baked but no longer adds its render triangles, or no longer exists"), *BakedMeshes[unmatched].ActorName, *BakedMeshes[unmatched].ComponentName.ToString());
	}

	if (OutReason)
		*OutReason = reason;

	return !reason.IsEmpty();
}

void AVAWorld::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

	// Covers saving the level and cooking it
	FString reason;

	if (IsBakedGeometryStale(&reason))
		VA_WARN_NAMED(TEXT("The baked geometry is out of date: %s. Run 'Bake Geometry For Shipping' on the VAWorld and save the level again."), *reason);
}
#endif

void AVAWorld::AddMaterialPrimitives(UVAMaterialComponent* source)
{
	AActor* owner = source->GetOwner();

	if (!owner)
		return;

	// Safe to call again, e.g. if the component is re-registered
	RemoveMaterialPrimitives(source);
	MaterialSources.Add(source);
	AddActorTree(owner, source);
}

void AVAWorld::RemoveMaterialPrimitives(UVAMaterialComponent* source)
{
	MaterialSources.Remove(source);
	RemoveBindings([source](const FVAPrimitiveBinding& binding) { return binding.Source.Get() == source; });
}

void AVAWorld::RebuildPrimitives()
{
	if (!World)
		return;

	RemoveBindings([](const FVAPrimitiveBinding&) { return true; });
	for (auto it = MaterialSources.CreateIterator(); it; ++it)
	{
		UVAMaterialComponent* source = it->Get();

		if (source && source->GetOwner())
			AddActorTree(source->GetOwner(), source);
		else
			it.RemoveCurrent();
	}
}

void AVAWorld::SetCollisionObjectTypes(const TArray<TEnumAsByte<ECollisionChannel>>& objectTypes)
{
	CollisionObjectTypes = objectTypes;
	RebuildPrimitives();
}

void AVAWorld::SyncPrimitive(AActor* actor)
{
	if (!actor || !World)
		return;

	// The actor, plus the attached children it would add, i.e. those without their own VAMaterialComponent (they manage their own geometry)
	TSet<AActor*> tree;
	TArray<AActor*> pending = { actor };

	while (pending.Num() > 0)
	{
		AActor* current = pending.Pop();
		tree.Add(current);

		TArray<AActor*> children;
		current->GetAttachedActors(children, true, false);

		for (AActor* child : children)
			if (!child->FindComponentByClass<UVAMaterialComponent>())
				pending.Add(child);
	}

	RemoveBindings([&tree](const FVAPrimitiveBinding& binding)
	{
		USceneComponent* component = binding.Component.Get();
		return component && tree.Contains(component->GetOwner());
	});

	UVAMaterialComponent* source = FindMaterialInChain(actor);

	if (source && source->GetAudioWorld() == this)
		AddActorTree(actor, source);
}

void AVAWorld::AddActorTree(AActor* actor, UVAMaterialComponent* source)
{
	AddActorPrimitives(actor, source);

	TArray<AActor*> children;
	actor->GetAttachedActors(children, true, false);

	for (AActor* child : children)
	{
		// Adds its own geometry from its own BeginPlay
		if (child->FindComponentByClass<UVAMaterialComponent>())
			continue;

		AddActorTree(child, source);
	}
}

void AVAWorld::AddActorPrimitives(AActor* actor, UVAMaterialComponent* source)
{
	// Destroyed or streamed-out geometry stops affecting raytracing, including inherited child actors that end play before their parent
	actor->OnEndPlay.AddUniqueDynamic(this, &AVAWorld::OnGeometryActorEndPlay);

	for (UActorComponent* component : actor->GetComponents())
	{
		int32 materialId;
		bool useFlatTransmission;

		if (UShapeComponent* shape = Cast<UShapeComponent>(component))
		{
			if (source->PropagateMode == EVAPropagateMode::Visuals || !PassesCollisionFilter(shape))
				continue;

			if (!source->GetMaterialFor(shape, materialId, useFlatTransmission))
			{
				ActorsWithInvalidMaterials.AddUnique(actor->GetActorNameOrLabel());
				continue;
			}

			AddShapePrimitive(shape, source, materialId);
		}
		else if (UStaticMeshComponent* meshComponent = Cast<UStaticMeshComponent>(component))
		{
			if (!source->GetMaterialFor(meshComponent, materialId, useFlatTransmission))
			{
				ActorsWithInvalidMaterials.AddUnique(actor->GetActorNameOrLabel());
				continue;
			}

			// Foliage, PCG and other instanced meshes add one copy of the mesh per instance
			if (UInstancedStaticMeshComponent* instanced = Cast<UInstancedStaticMeshComponent>(meshComponent))
			{
				for (int32 i = 0; i < instanced->GetInstanceCount(); i++)
				{
					FTransform instanceTransform;

					if (instanced->GetInstanceTransform(i, instanceTransform, false))
						AddStaticMeshPrimitives(instanced, source, instanceTransform, materialId, useFlatTransmission);
				}
			}
			else
			{
				AddStaticMeshPrimitives(meshComponent, source, FTransform::Identity, materialId, useFlatTransmission);
			}
		}
		else if (ULandscapeHeightfieldCollisionComponent* landscapeComponent = Cast<ULandscapeHeightfieldCollisionComponent>(component))
		{
			if (!source->GetMaterialFor(landscapeComponent, materialId, useFlatTransmission))
			{
				ActorsWithInvalidMaterials.AddUnique(actor->GetActorNameOrLabel());
				continue;
			}

			AddLandscapePrimitive(landscapeComponent, source, materialId, useFlatTransmission);
		}
	}
}

bool AVAWorld::PassesCollisionFilter(const UPrimitiveComponent* component) const
{
	return CollisionObjectTypes.IsEmpty() || CollisionObjectTypes.Contains(component->GetCollisionObjectType());
}

void AVAWorld::AddShapePrimitive(UShapeComponent* shape, UVAMaterialComponent* source, int32 materialId)
{
	VAMaterialType materialType = (VAMaterialType)materialId;

	FVAPrimitiveBinding binding;
	binding.Component = shape;
	binding.Source = source;

	// Sizes and transforms are set by RefreshPrimitiveTransform in AddBinding
	if (Cast<USphereComponent>(shape))
	{
		VASpherePrimitive* sphere = vaSpherePrimitiveCreate();
		vaSpherePrimitiveSetMaterial(sphere, materialType);
		binding.Primitive = sphere;
		binding.Kind = EVAPrimitiveKind::Sphere;
		AddBinding(binding, TEXT("sphere"));
	}
	else if (Cast<UCapsuleComponent>(shape))
	{
		VACapsulePrimitive* capsule = vaCapsulePrimitiveCreate();
		vaCapsulePrimitiveSetMaterial(capsule, materialType);
		binding.Primitive = capsule;
		binding.Kind = EVAPrimitiveKind::Capsule;
		AddBinding(binding, TEXT("capsule"));
	}
	else if (Cast<UBoxComponent>(shape))
	{
		VAPrismPrimitive* prism = vaPrismPrimitiveCreate();
		vaPrismPrimitiveSetMaterial(prism, materialType);
		binding.Primitive = prism;
		binding.Kind = EVAPrimitiveKind::Prism;
		AddBinding(binding, TEXT("box"));
	}
}

void AVAWorld::AddStaticMeshPrimitives(UStaticMeshComponent* meshComponent, UVAMaterialComponent* source, const FTransform& meshTransform, int32 materialId, bool useFlatTransmission)
{
	UStaticMesh* staticMesh = meshComponent->GetStaticMesh();

	// Ignore components with no meshes
	if (!staticMesh)
		return;

	VAMaterialType materialType = (VAMaterialType)materialId;
	AActor* actor = meshComponent->GetOwner();
	UBodySetup* bodySetup = staticMesh->GetBodySetup();
	bool addedSimple = false;

	auto makeBinding = [&](void* primitive, EVAPrimitiveKind kind, const FTransform& elementTransform, const FVector& extent)
	{
		FVAPrimitiveBinding binding;
		binding.Component = meshComponent;
		binding.Source = source;
		binding.Primitive = primitive;
		binding.Kind = kind;
		binding.MeshTransform = meshTransform;
		binding.ElementTransform = elementTransform;
		binding.LocalExtent = extent;
		return binding;
	};

	// If this mesh is composed of simple collision elements, add them all. A mesh whose collision is filtered out is skipped rather than falling back to its triangles, since it's a collider
	if (source->PropagateMode != EVAPropagateMode::Visuals && HasSimpleCollision(staticMesh))
	{
		if (!PassesCollisionFilter(meshComponent))
			return;

		const FKAggregateGeom& agg = bodySetup->AggGeom;

		for (const FKSphereElem& sphereElem : agg.SphereElems)
		{
			VASpherePrimitive* sphere = vaSpherePrimitiveCreate();
			vaSpherePrimitiveSetMaterial(sphere, materialType);
			addedSimple |= AddBinding(makeBinding(sphere, EVAPrimitiveKind::SphereFromMesh, FTransform(sphereElem.GetTransform().GetTranslation()), FVector(sphereElem.Radius, 0.0, 0.0)), TEXT("sphere"));
		}

		for (const FKBoxElem& boxElem : agg.BoxElems)
		{
			VAPrismPrimitive* prism = vaPrismPrimitiveCreate();
			vaPrismPrimitiveSetMaterial(prism, materialType);
			addedSimple |= AddBinding(makeBinding(prism, EVAPrimitiveKind::PrismFromMesh, FTransform(boxElem.GetTransform().GetRotation(), boxElem.GetTransform().GetTranslation()), FVector(boxElem.X, boxElem.Y, boxElem.Z)), TEXT("box"));
		}

		for (const FKSphylElem& capsuleElem : agg.SphylElems)
		{
			VACapsulePrimitive* capsule = vaCapsulePrimitiveCreate();
			vaCapsulePrimitiveSetMaterial(capsule, materialType);
			addedSimple |= AddBinding(makeBinding(capsule, EVAPrimitiveKind::CapsuleFromMesh, FTransform(capsuleElem.GetTransform().GetRotation(), capsuleElem.GetTransform().GetTranslation()), FVector(capsuleElem.Radius, 0.0, capsuleElem.Length)), TEXT("capsule"));
		}

		// Convex hulls are closed, so they become watertight mesh primitives with the element transform baked into mesh space
		for (int32 convexIndex = 0; convexIndex < agg.ConvexElems.Num(); convexIndex++)
		{
			auto readHull = [&](TArray<FVector>& vertices)
			{
				FKConvexElem convexElem = agg.ConvexElems[convexIndex];

				if (convexElem.IndexData.IsEmpty())
					convexElem.ComputeChaosConvexIndices();

				if (convexElem.IndexData.IsEmpty())
					return false;

				FTransform elementTransform = convexElem.GetTransform();
				vertices.Reserve(convexElem.IndexData.Num());

				// The SDK's triangle test is one-sided. Render triangles already have the winding it expects, but Chaos' hull indices are wound the other way, so each triangle's last two vertices are swapped. Otherwise rays pass into the hull from outside
				for (int32 i = 0; i + 2 < convexElem.IndexData.Num(); i += 3)
				{
					vertices.Add(elementTransform.TransformPosition(convexElem.VertexData[convexElem.IndexData[i]]));
					vertices.Add(elementTransform.TransformPosition(convexElem.VertexData[convexElem.IndexData[i + 2]]));
					vertices.Add(elementTransform.TransformPosition(convexElem.VertexData[convexElem.IndexData[i + 1]]));
				}

				return true;
			};

			addedSimple |= AddMeshPrimitive(FVAMeshKey{ staticMesh, 0, convexIndex }, readHull, meshComponent, source, meshTransform, materialId, false);
		}
	}

	if (addedSimple || source->PropagateMode == EVAPropagateMode::Colliders)
		return;

	// Attempt to use baked geometry. Fall back to mesh data (may be unavailable in cooked builds)
	auto readTriangles = [&](TArray<FVector>& vertices)
	{
		const FVABakedMesh* bakedMesh = BakedMeshes.FindByPredicate([&](const FVABakedMesh& baked) { return baked.ComponentName == meshComponent->GetFName() && baked.ActorName == actor->GetName(); });

		// Render vertices are float, both baked and live
		TArray<FVector3f> liveVertices;
		const TArray<FVector3f>* renderVertices = nullptr;

		if (bakedMesh)
			renderVertices = &bakedMesh->Vertices;
		else if (GetRenderVertices(staticMesh, source->MeshLOD, liveVertices))
			renderVertices = &liveVertices;

		if (!renderVertices)
		{
			VA_WARN_NAMED(TEXT("Mesh '%s' will not affect raytracing as it has no baked geometry and no render mesh data. Run 'Bake Geometry For Shipping' on the VAWorld and save the level."), *staticMesh->GetName());
			return false;
		}

		vertices.Reserve(renderVertices->Num());

		for (const FVector3f& vertex : *renderVertices)
			vertices.Add(FVector(vertex));

		return true;
	};

	AddMeshPrimitive(FVAMeshKey{ staticMesh, source->MeshLOD, INDEX_NONE }, readTriangles, meshComponent, source, meshTransform, materialId, useFlatTransmission);
}

void AVAWorld::AddLandscapePrimitive(ULandscapeHeightfieldCollisionComponent* landscapeComponent, UVAMaterialComponent* source, int32 materialId, bool useFlatTransmission)
{
	// A landscape is both a collider and a visual, so only the collision filter can leave it out
	if (source->PropagateMode != EVAPropagateMode::Visuals && !PassesCollisionFilter(landscapeComponent))
		return;

	const Chaos::FHeightField* heightField = nullptr;
	bool useSimple = false;

	if (IsValidRef(landscapeComponent->HeightfieldRef))
	{
		// MeshLOD above 0 uses the landscape's simple collision, if it has one
		useSimple = source->MeshLOD > 0 && landscapeComponent->HeightfieldRef->HeightfieldSimpleGeometry.IsValid();
		heightField = useSimple ? landscapeComponent->HeightfieldRef->HeightfieldSimpleGeometry.GetReference() : landscapeComponent->HeightfieldRef->HeightfieldGeometry.GetReference();
	}

	if (!heightField || heightField->GetNumCols() < 2 || heightField->GetNumRows() < 2)
	{
		VA_WARN_NAMED(TEXT("Landscape '%s' will not affect raytracing as its component '%s' has no collision. Enable collision on the landscape."), *landscapeComponent->GetOwner()->GetActorNameOrLabel(), *landscapeComponent->GetName());
		return;
	}

	// The collision heightfield exists in cooked builds, unlike the heightmap. Its triangles are in component space, with the landscape's scale left to the primitive's transform
	auto readHeightField = [&](TArray<FVector>& vertices)
	{
		int32 columns = heightField->GetNumCols();
		int32 rows = heightField->GetNumRows();
		double spacing = landscapeComponent->CollisionScale * landscapeComponent->CollisionSizeQuads / (columns - 1);

		auto top = [&](int32 x, int32 y) { return FVector(x * spacing, y * spacing, heightField->GetHeight(x, y) * LANDSCAPE_ZSCALE); };

		double lowest = TNumericLimits<double>::Max();

		for (int32 y = 0; y < rows; y++)
			for (int32 x = 0; x < columns; x++)
				lowest = FMath::Min(lowest, top(x, y).Z);

		// The SDK measures how far a ray travels between entering and leaving a mesh, so an open sheet wouldn't muffle anything. Each component is closed into a solid block of ground, down to LandscapeThickness below its lowest point
		double scaleZ = FMath::Abs(landscapeComponent->GetComponentTransform().GetScale3D().Z);
		double base = lowest - LandscapeThickness / FMath::Max(scaleZ, UE_DOUBLE_KINDA_SMALL_NUMBER);

		auto bottom = [&](int32 x, int32 y) { return FVector(x * spacing, y * spacing, base); };

		// The SDK's triangle test is one-sided, and hits the side that Unreal's render triangles face. That's the reverse of Chaos' winding
		auto addTriangle = [&](const FVector& a, const FVector& b, const FVector& c)
		{
			vertices.Add(a);
			vertices.Add(c);
			vertices.Add(b);
		};

		// The wall below one edge of the surface, facing right of the direction from (x0, y0) to (x1, y1)
		auto addSkirt = [&](int32 x0, int32 y0, int32 x1, int32 y1)
		{
			addTriangle(top(x0, y0), bottom(x0, y0), bottom(x1, y1));
			addTriangle(top(x0, y0), bottom(x1, y1), top(x1, y1));
		};

		vertices.Reserve((columns - 1) * (rows - 1) * 6 + (columns + rows - 2) * 12 + 6);

		for (int32 y = 0; y + 1 < rows; y++)
		{
			for (int32 x = 0; x + 1 < columns; x++)
			{
				// Painted with the visibility layer
				if (heightField->IsHole(x, y))
					continue;

				addTriangle(top(x, y), top(x + 1, y), top(x + 1, y + 1));
				addTriangle(top(x, y), top(x + 1, y + 1), top(x, y + 1));
			}
		}

		for (int32 x = 0; x + 1 < columns; x++)
		{
			addSkirt(x, 0, x + 1, 0);
			addSkirt(x + 1, rows - 1, x, rows - 1);
		}

		for (int32 y = 0; y + 1 < rows; y++)
		{
			addSkirt(0, y + 1, 0, y);
			addSkirt(columns - 1, y, columns - 1, y + 1);
		}

		addTriangle(bottom(0, 0), bottom(columns - 1, rows - 1), bottom(columns - 1, 0));
		addTriangle(bottom(0, 0), bottom(0, rows - 1), bottom(columns - 1, rows - 1));

		return true;
	};

	AddMeshPrimitive(FVAMeshKey{ landscapeComponent, useSimple ? 1 : 0, INDEX_NONE }, readHeightField, landscapeComponent, source, FTransform::Identity, materialId, useFlatTransmission);
}

VAMesh* AVAWorld::AcquireSharedMesh(const FVAMeshKey& key, TFunctionRef<bool(TArray<FVector>&)> readVertices, const UPrimitiveComponent* meshComponent)
{
	if (FVASharedMesh* shared = SharedMeshes.Find(key))
	{
		shared->ReferenceCount++;
		return shared->Mesh;
	}

	TArray<FVector> vertices;

	if (!readVertices(vertices))
		return nullptr;

	TArray<VAVector> vaVertices;
	vaVertices.Reserve(vertices.Num());

	VAVector minBounds = VECTOR_MAX;
	VAVector maxBounds = VECTOR_MIN;

	for (const FVector& localPosition : vertices)
	{
		VAVector vaPosition = vaVectorCreate(localPosition.X, localPosition.Y, localPosition.Z);
		vaVertices.Add(vaPosition);

		minBounds = vaVectorMin(minBounds, vaPosition);
		maxBounds = vaVectorMax(maxBounds, vaPosition);
	}

	VAMesh* mesh;
	VAResult result = vaMeshCreate(vaVertices.GetData(), vaVertices.Num(), minBounds, maxBounds, &mesh);

	if (result != VA_SUCCESS)
	{
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to create a mesh for '%s' on '%s'."), *meshComponent->GetName(), *meshComponent->GetOwner()->GetActorNameOrLabel());
		return nullptr;
	}

	SharedMeshes.Add(key, FVASharedMesh{ mesh, 1 });
	return mesh;
}

void AVAWorld::ReleaseSharedMesh(const FVAMeshKey& key)
{
	FVASharedMesh* shared = SharedMeshes.Find(key);

	if (!shared || --shared->ReferenceCount > 0)
		return;

	// The SDK frees it once its raytracing threads have finished with it
	VAResult result = vaMeshDestroy(shared->Mesh);

	if (result != VA_SUCCESS)
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to destroy a mesh."));

	SharedMeshes.Remove(key);
}

bool AVAWorld::AddMeshPrimitive(const FVAMeshKey& key, TFunctionRef<bool(TArray<FVector>&)> readVertices, UPrimitiveComponent* meshComponent, UVAMaterialComponent* source, const FTransform& meshTransform, int32 materialId, bool useFlatTransmission)
{
	VAMesh* sharedMesh = AcquireSharedMesh(key, readVertices, meshComponent);

	if (!sharedMesh)
		return false;

	// The vertices are in mesh space, so component scale and an instance's transform only go into the primitive's transform
	VAMatrix vaTransform = MakeScaleRotTransMatrix(meshTransform * meshComponent->GetComponentTransform());
	VAMeshPrimitive* mesh;

	VAResult result = vaMeshPrimitiveCreateFromMesh((VAMaterialType)materialId, sharedMesh, &vaTransform, &mesh);

	if (result != VA_SUCCESS)
	{
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to create a mesh primitive for '%s' on '%s'."), *meshComponent->GetName(), *meshComponent->GetOwner()->GetActorNameOrLabel());
		ReleaseSharedMesh(key);
		return false;
	}

	vaMeshPrimitiveSetUseFlatTransmission(mesh, useFlatTransmission);

	FVAPrimitiveBinding binding;
	binding.Component = meshComponent;
	binding.Source = source;
	binding.Primitive = mesh;
	binding.Kind = EVAPrimitiveKind::Mesh;
	binding.MeshKey = key;
	binding.MeshTransform = meshTransform;
	return AddBinding(binding, TEXT("mesh"));
}
