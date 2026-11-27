// AI-created

#include "VAWorld.h"
#include "VAEngineCompat.h"
#include "VAMeshMaterialUserData.h"
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
#include "Engine/LevelStreaming.h"
#include "Materials/MaterialInterface.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeDataAccess.h"
#include "LandscapeProxy.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/Engine.h"

#if UE_VERSION_OLDER_THAN(5, 0, 0) && !WITH_CHAOS
#include "DynamicMeshBuilder.h"
#include "PhysXIncludes.h"
#else
#include "Chaos/HeightField.h"
#endif

// 4.27 has no World Partition, Level Instances or FObjectPreSaveContext, so their code compiles out
#if !UE_VERSION_OLDER_THAN(5, 0, 0)
#include "UObject/ObjectSaveContext.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceSubsystem.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#endif
#endif

extern "C" {
#include "vaudio.h"
}

// True if this mesh would use its simple collision rather than its render triangles
static bool HasSimpleCollision(UStaticMesh* Mesh)
{
	UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;

	if (!BodySetup)
		return false;

	const FKAggregateGeom& Agg = BodySetup->AggGeom;
	return Agg.SphylElems.Num() > 0 || Agg.SphereElems.Num() > 0 || Agg.BoxElems.Num() > 0 || Agg.ConvexElems.Num() > 0;
}

// One LOD's triangle-list vertices in mesh space, section by section, and the material slot (StaticMaterials index) of each triangle. False if the mesh has no CPU render data, e.g. in a cooked build
static bool GetRenderTriangles(UStaticMesh* mesh, int32 lod, TArray<FVAVector3f>& vertices, TArray<int32>& triangleSlots)
{
	FStaticMeshRenderData* renderData = mesh->GetRenderData();

	if (!renderData || renderData->LODResources.Num() == 0)
		return false;

	FStaticMeshLODResources& lodResources = renderData->LODResources[FMath::Clamp(lod, 0, renderData->LODResources.Num() - 1)];

	if (!lodResources.VertexBuffers.PositionVertexBuffer.GetVertexData())
		return false;

	TArray<uint32> indices;
	lodResources.IndexBuffer.GetCopy(indices);

	if (indices.Num() == 0)
		return false;

	vertices.Reset(indices.Num());
	triangleSlots.Reset(indices.Num() / 3);

	// Each section is one material slot's slice of the index buffer
	for (const FStaticMeshSection& section : lodResources.Sections)
	{
		int32 end = FMath::Min((int32)section.FirstIndex + (int32)section.NumTriangles * 3, indices.Num());

		for (int32 i = section.FirstIndex; i + 2 < end; i += 3)
		{
			for (int32 corner = 0; corner < 3; corner++)
				vertices.Add(lodResources.VertexBuffers.PositionVertexBuffer.VertexPosition(indices[i + corner]));

			triangleSlots.Add(section.MaterialIndex);
		}
	}

	return vertices.Num() > 0;
}

const FVABakedMesh* AVAWorld::FindBakedMesh(const UStaticMesh* mesh, int32 lod) const
{
	FSoftObjectPath path(mesh);
	return BakedMeshes.FindByPredicate([&](const FVABakedMesh& baked) { return baked.LOD == lod && baked.Mesh.ToSoftObjectPath() == path; });
}

#if WITH_EDITOR
using FVABakeVisitor = TFunctionRef<void(AActor*, UStaticMeshComponent*, UStaticMesh*, int32)>;

// Calls visit for each of the actor's mesh components that add their render triangles, with the LOD they use. Every triangle is baked, whichever slots are ignored, so cooked builds can resolve the slots again
static void ForEachBakeTargetOnActor(const AVAWorld* vaWorld, AActor* actor, FVABakeVisitor visit)
{
	TArray<UStaticMeshComponent*> meshComponents;
	actor->GetComponents<UStaticMeshComponent>(meshComponents);

	for (UStaticMeshComponent* meshComponent : meshComponents)
	{
		UStaticMesh* mesh = meshComponent->GetStaticMesh();
		FVAPrimitiveSettings settings;

		if (!vaWorld->ResolvePrimitiveSettings(meshComponent, settings) || settings.PropagateMode == EVAPropagateMode::Colliders)
			continue;

		// Meshes with simple collision use it instead of their triangles, unless only visuals are added
		if (HasSimpleCollision(mesh) && settings.PropagateMode != EVAPropagateMode::Visuals)
			continue;

		visit(actor, meshComponent, mesh, settings.MeshLOD);
	}
}

// Calls visit for every mesh component BakeGeometry bakes, in every loaded actor, including those in sublevels hidden in the editor. With loadAll, level instances that aren't loaded are loaded for the call and unloaded after, and so is every World Partition actor
static void ForEachBakeTarget(const AVAWorld* vaWorld, UWorld* world, bool loadAll, FVABakeVisitor visit)
{
#if !UE_VERSION_OLDER_THAN(5, 0, 0)
	ULevelInstanceSubsystem* levelInstances = world->GetSubsystem<ULevelInstanceSubsystem>();
#endif

	TFunction<void(AActor*)> visitActor = [&](AActor* actor)
	{
		ForEachBakeTargetOnActor(vaWorld, actor, visit);

#if !UE_VERSION_OLDER_THAN(5, 0, 0)
		// A loaded level instance's actors are already in the world's levels
		ILevelInstanceInterface* levelInstance = Cast<ILevelInstanceInterface>(actor);

		if (!loadAll || !levelInstance || !levelInstances || levelInstances->IsLoaded(levelInstance) || levelInstance->IsEditing())
			return;

		levelInstances->BlockLoadLevelInstance(levelInstance);
		levelInstances->ForEachActorInLevelInstance(levelInstance, [&](AActor* contained) { visitActor(contained); return true; });
		levelInstances->BlockUnloadLevelInstance(levelInstance);
#endif
	};

	// Copied, as loading a level instance adds actors to the world
	TArray<AActor*> actors;

	for (TActorIterator<AActor> it(world); it; ++it)
		actors.Add(*it);

	// A streaming sublevel hidden in the editor is still loaded, but TActorIterator skips levels that aren't visible
	for (ULevelStreaming* streaming : world->GetStreamingLevels())
	{
		ULevel* level = streaming ? streaming->GetLoadedLevel() : nullptr;

		if (!level || level->bIsVisible)
			continue;

		for (AActor* actor : level->Actors)
			if (IsValid(actor))
				actors.Add(actor);
	}

	for (AActor* actor : actors)
		visitActor(actor);

#if !UE_VERSION_OLDER_THAN(5, 0, 0)
	UWorldPartition* worldPartition = world->GetWorldPartition();

	// Loaded actors are visited again, which is harmless as entries are per mesh
	if (loadAll && worldPartition)
	{
		FWorldPartitionHelpers::ForEachActorWithLoading(worldPartition, [&](const FWorldPartitionActorDescInstance* actorDesc)
		{
			if (AActor* actor = actorDesc->GetActor())
				visitActor(actor);

			return true;
		});
	}
#endif
}

void AVAWorld::BakeGeometry()
{
	UWorld* world = GetWorld();

	if (!world)
	{
		VA_WARN_NAMED(TEXT("No Unreal World found (open a level first)."));
		return;
	}

	Modify();
	BakedMeshes.Reset();

	// Paths rather than pointers, as World Partition loading can collect garbage
	TSet<TPair<FSoftObjectPath, int32>> visited;
	int32 placements = 0;

	ForEachBakeTarget(this, world, true, [&](AActor* actor, UStaticMeshComponent* meshComponent, UStaticMesh* mesh, int32 lod)
	{
		placements++;

		bool alreadyVisited;
		visited.Add(MakeTuple(FSoftObjectPath(mesh), lod), &alreadyVisited);

		if (alreadyVisited)
			return;

		FVABakedMesh baked;
		baked.Mesh = mesh;
		baked.LOD = lod;

		if (!GetRenderTriangles(mesh, lod, baked.Vertices, baked.TriangleSlots))
		{
			VA_WARN_NAMED(TEXT("Mesh '%s' (first used by '%s') has no render data in-editor, skipping"), *mesh->GetPathName(), *VAActorName(actor));
			return;
		}

		VA_LOG_NAMED(TEXT("Baked '%s' LOD %d, %d triangles"), *mesh->GetPathName(), lod, baked.Vertices.Num() / 3);
		BakedMeshes.Add(MoveTemp(baked));
	});

	MarkPackageDirty();
	VA_LOG_NAMED(TEXT("Baked %d mesh(es) used by %d mesh component(s). Save the level to persist."), BakedMeshes.Num(), placements);
}

bool AVAWorld::IsBakedGeometryStale(FString* OutReason) const
{
	UWorld* world = GetWorld();

	// Never baked, so there's nothing to go stale
	if (!world || BakedMeshes.Num() == 0)
		return false;

	TSet<TPair<FSoftObjectPath, int32>> used;
	FString reason;

	ForEachBakeTarget(this, world, false, [&](AActor* actor, UStaticMeshComponent* meshComponent, UStaticMesh* mesh, int32 lod)
	{
		used.Add(MakeTuple(FSoftObjectPath(mesh), lod));
		TArray<FVAVector3f> vertices;
		TArray<int32> triangleSlots;

		// A mesh with no render data can't be baked or compared, and is what the bake is for
		if (reason.IsEmpty() && !FindBakedMesh(mesh, lod) && GetRenderTriangles(mesh, lod, vertices, triangleSlots))
			reason = FString::Printf(TEXT("'%s' LOD %d (used by '%s'.'%s') isn't baked"), *mesh->GetPathName(), lod, *VAActorName(actor), *meshComponent->GetName());
	});

	// Every entry whose mesh is loaded has to match its live triangles, whether a loaded actor uses it or not
	for (const FVABakedMesh& baked : BakedMeshes)
	{
		UStaticMesh* mesh = baked.Mesh.Get();
		TArray<FVAVector3f> vertices;
		TArray<int32> triangleSlots;

		if (reason.IsEmpty() && mesh && GetRenderTriangles(mesh, baked.LOD, vertices, triangleSlots) && (vertices != baked.Vertices || triangleSlots != baked.TriangleSlots))
			reason = FString::Printf(TEXT("'%s' LOD %d has changed since it was baked (%d baked triangles, %d now, or its material slots changed)"), *mesh->GetPathName(), baked.LOD, baked.Vertices.Num() / 3, vertices.Num() / 3);
	}

	// World Partition only checks loaded actors, so an unused entry may just be in an unloaded cell
#if UE_VERSION_OLDER_THAN(5, 0, 0)
	if (reason.IsEmpty())
#else
	if (reason.IsEmpty() && !world->IsPartitionedWorld())
#endif
	{
		const FVABakedMesh* unused = BakedMeshes.FindByPredicate([&](const FVABakedMesh& baked) { return !used.Contains(MakeTuple(baked.Mesh.ToSoftObjectPath(), baked.LOD)); });

		if (unused)
			reason = FString::Printf(TEXT("'%s' LOD %d is baked but no mesh component adds its render triangles any more"), *unused->Mesh.ToString(), unused->LOD);
	}

	if (OutReason)
		*OutReason = reason;

	return !reason.IsEmpty();
}

#if UE_VERSION_OLDER_THAN(5, 0, 0)
void AVAWorld::PreSave(const ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);
#else
void AVAWorld::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);
#endif

	// Covers saving the level and cooking it
	FString reason;

	if (IsBakedGeometryStale(&reason))
		VA_WARN_NAMED(TEXT("The baked geometry is out of date: %s. Run 'Bake Geometry For Shipping' on the VAWorld and save the level again."), *reason);
}
#endif

void AVAWorld::CollectActorTree(AActor* actor, TSet<AActor*>& tree)
{
	TArray<AActor*> pending = { actor };

	while (pending.Num() > 0)
	{
		AActor* current = pending.Pop();
		tree.Add(current);

		// 4.27's GetAttachedActors can't recurse
		TArray<AActor*> children;
		current->GetAttachedActors(children);
		pending.Append(children);
	}
}

void AVAWorld::RebuildPrimitives()
{
	if (!World)
		return;

	RemoveBindings([](const FVAPrimitiveBinding&) { return true; });
	ClearMaterialCache();
	AddWorldGeometry();
}

void AVAWorld::SetCollisionObjectTypes(const TArray<TEnumAsByte<ECollisionChannel>>& objectTypes)
{
	CollisionObjectTypes = objectTypes;
	RebuildPrimitives();
}

void AVAWorld::SetLandscapeLOD(int32 lod)
{
	LandscapeLOD = FMath::Clamp(lod, 0, 7);
	RebuildPrimitives();
}

int32 AVAWorld::GetSharedMeshTriangleCount() const
{
	int32 triangles = 0;

	for (const TPair<FVAMeshKey, FVASharedMesh>& shared : SharedMeshes)
		triangles += shared.Value.TriangleCount;

	return triangles;
}

void AVAWorld::SyncPrimitive(AActor* actor)
{
	if (!actor || !World)
		return;

	TSet<AActor*> tree;
	CollectActorTree(actor, tree);

	RemoveBindings([&tree](const FVAPrimitiveBinding& binding)
	{
		USceneComponent* component = binding.Component.Get();
		return component && tree.Contains(component->GetOwner());
	});

	// It's the explicit way to apply changes, e.g. a map entry set from a Blueprint, so nothing stale is reused
	ClearMaterialCache();

	for (AActor* member : tree)
		AddActorGeometry(member);
}

void AVAWorld::OnLevelAddedToWorld(ULevel* level, UWorld* world)
{
	// Streamed sublevels, World Partition cells and level instances
	if (world == GetWorld() && World)
		AddLevelGeometry(level);
}

void AVAWorld::OnActorSpawned(AActor* actor)
{
	PendingSpawnedActors.Add(actor);
}

void AVAWorld::AddSpawnedGeometry()
{
	if (PendingSpawnedActors.Num() == 0)
		return;

	TArray<TWeakObjectPtr<AActor>> spawned = MoveTemp(PendingSpawnedActors);

	for (const TWeakObjectPtr<AActor>& actor : spawned)
		if (AActor* alive = actor.Get())
			AddActorGeometry(alive);
}

void AVAWorld::AddWorldGeometry()
{
	if (!World)
		return;

	for (ULevel* level : GetWorld()->GetLevels())
		if (level && level->bIsVisible)
			AddLevelGeometry(level);
}

void AVAWorld::AddLevelGeometry(ULevel* level)
{
	for (AActor* actor : level->Actors)
		AddActorGeometry(actor);
}

void AVAWorld::AddActorGeometry(AActor* actor)
{
	if (!IsValid(actor) || actor->IsActorBeingDestroyed())
		return;

	bool hasGeometry = false;

	for (UActorComponent* component : actor->GetComponents())
	{
		UStaticMeshComponent* meshComponent = Cast<UStaticMeshComponent>(component);
		bool isGeometry = (meshComponent && meshComponent->GetStaticMesh()) || Cast<UShapeComponent>(component) || Cast<ULandscapeHeightfieldCollisionComponent>(component);

		if (!isGeometry)
			continue;

		// Already added by an earlier scan
		if (PrimitiveBindingsByComponent.Contains(Cast<USceneComponent>(component)))
			return;

		hasGeometry = true;
	}

	if (hasGeometry)
		AddActorPrimitives(actor);
}

void AVAWorld::AddActorPrimitives(AActor* actor)
{
	int32 previousCount = PrimitiveBindings.Num();

	for (UActorComponent* component : actor->GetComponents())
		AddComponentPrimitives(component);

	// Destroyed or streamed-out geometry stops affecting raytracing
	if (PrimitiveBindings.Num() > previousCount)
		actor->OnEndPlay.AddUniqueDynamic(this, &AVAWorld::OnGeometryActorEndPlay);
}

void AVAWorld::AddComponentPrimitives(UActorComponent* component)
{
	FVAPrimitiveSettings settings;

	// Shapes are colliders whose material is their physical material
	if (UShapeComponent* shape = Cast<UShapeComponent>(component))
	{
		if (PassesCollisionFilter(shape) && ResolveCollisionMaterial(shape->BodyInstance, shape->GetBodySetup(), settings.MaterialId))
			AddShapePrimitive(shape, settings);

		return;
	}

	// One heightmap per landscape component, with the landscape's default physical material, as a heightmap has one material and can't follow its painted layers
	if (ULandscapeHeightfieldCollisionComponent* landscapeComponent = Cast<ULandscapeHeightfieldCollisionComponent>(component))
	{
		ALandscapeProxy* landscape = landscapeComponent->GetLandscapeProxy();

		if (landscape && ResolveBodyMaterial(landscape->DefaultPhysMaterial, settings.MaterialId))
		{
			settings.PropagateMode = EVAPropagateMode::All;
			AddLandscapePrimitive(landscapeComponent, settings);
		}

		return;
	}

	UStaticMeshComponent* meshComponent = Cast<UStaticMeshComponent>(component);
	bool invalid = false;

	if (!meshComponent || !ResolvePrimitiveSettings(meshComponent, settings, &invalid))
	{
		if (invalid)
			ActorsWithInvalidMaterials.AddUnique(VAActorName(component->GetOwner()));

		return;
	}

	// Foliage, PCG and other instanced meshes add one copy of the mesh per instance
	if (UInstancedStaticMeshComponent* instanced = Cast<UInstancedStaticMeshComponent>(meshComponent))
	{
		for (int32 i = 0; i < instanced->GetInstanceCount(); i++)
		{
			FTransform instanceTransform;

			if (instanced->GetInstanceTransform(i, instanceTransform, false))
				AddStaticMeshPrimitives(instanced, settings, instanceTransform);
		}
	}
	else
	{
		AddStaticMeshPrimitives(meshComponent, settings, FTransform::Identity);
	}
}

void AVAWorld::RefreshMeshDefaults(const UStaticMesh* mesh)
{
	if (!World || !mesh)
		return;

	TArray<UStaticMeshComponent*> affected;

	for (ULevel* level : GetWorld()->GetLevels())
	{
		if (!level || !level->bIsVisible)
			continue;

		for (AActor* actor : level->Actors)
		{
			if (!IsValid(actor) || actor->IsActorBeingDestroyed())
				continue;

			for (UActorComponent* component : actor->GetComponents())
			{
				UStaticMeshComponent* meshComponent = Cast<UStaticMeshComponent>(component);

				if (meshComponent && meshComponent->GetStaticMesh() == mesh)
					affected.Add(meshComponent);
			}
		}
	}

	if (affected.Num() == 0)
		return;

	RemoveBindings([&affected](const FVAPrimitiveBinding& binding)
	{
		USceneComponent* component = binding.Component.Get();
		return component && affected.Contains(component);
	});

	// Added again with the mesh's current VAMeshMaterial and material slots, or not at all if nothing matches any more
	for (UStaticMeshComponent* meshComponent : affected)
	{
		int32 previousCount = PrimitiveBindings.Num();
		AddComponentPrimitives(meshComponent);

		if (PrimitiveBindings.Num() > previousCount)
			meshComponent->GetOwner()->OnEndPlay.AddUniqueDynamic(this, &AVAWorld::OnGeometryActorEndPlay);
	}
}

int32 AVAWorld::ResolveSlotMaterial(const UMaterialInterface* renderMaterial, int32 fallbackID) const
{
	if (!renderMaterial)
		return fallbackID;

	// Before play, custom materials only have a placeholder ID, so nothing is cached
	const int32* cached = World ? RenderMaterialCache.Find(TObjectKey<UMaterialInterface>(renderMaterial)) : nullptr;
	int32 materialID = INDEX_NONE;

	if (cached)
	{
		materialID = *cached;
	}
	else
	{
		// Instances without their own physical material use their parent's
		if (!ResolveRenderMaterial(renderMaterial, materialID) && !ResolveBodyMaterial(renderMaterial->GetPhysicalMaterial(), materialID))
			materialID = INDEX_NONE;

		if (World)
			RenderMaterialCache.Add(TObjectKey<UMaterialInterface>(renderMaterial), materialID);
	}

	return materialID != INDEX_NONE ? materialID : fallbackID;
}

bool AVAWorld::ResolveBodyMaterial(const UPhysicalMaterial* physicalMaterial, int32& outMaterialID) const
{
	if (!physicalMaterial)
		return false;

	const int32* cached = World ? PhysicalMaterialCache.Find(TObjectKey<UPhysicalMaterial>(physicalMaterial)) : nullptr;
	int32 materialID = INDEX_NONE;

	if (cached)
		materialID = *cached;
	else if (!ResolvePhysicalMaterial(physicalMaterial, materialID))
		materialID = INDEX_NONE;

	if (!cached && World)
		PhysicalMaterialCache.Add(TObjectKey<UPhysicalMaterial>(physicalMaterial), materialID);

	if (materialID == INDEX_NONE)
		return false;

	outMaterialID = materialID;
	return true;
}

bool AVAWorld::ResolveCollisionMaterial(const FBodyInstance& bodyInstance, const UBodySetup* bodySetup, int32& outMaterialID) const
{
#if UE_VERSION_OLDER_THAN(5, 0, 0)
	// 4.27 has no getter for the override. With no owner or body setup, the engine's lookup returns the override, else the default physical material, which is never matched
	UPhysicalMaterial* overrideMaterial = FBodyInstance::GetSimplePhysicalMaterial(&bodyInstance, nullptr, nullptr);

	if (GEngine && overrideMaterial == GEngine->DefaultPhysMaterial)
		overrideMaterial = nullptr;
#else
	UPhysicalMaterial* overrideMaterial = bodyInstance.GetPhysMaterialOverride();
#endif

	if (overrideMaterial)
		return ResolveBodyMaterial(overrideMaterial, outMaterialID);

	return bodySetup && ResolveBodyMaterial(bodySetup->PhysMaterial, outMaterialID);
}

bool AVAWorld::ResolvePrimitiveSettings(const UStaticMeshComponent* meshComponent, FVAPrimitiveSettings& outSettings, bool* outInvalid) const
{
	if (outInvalid)
		*outInvalid = false;

	UStaticMesh* mesh = meshComponent ? meshComponent->GetStaticMesh() : nullptr;

	if (!mesh)
		return false;

	const UVAMeshMaterialUserData* meshDefault = UVAMeshMaterialUserData::Find(mesh);
	const UObject* reporter = meshComponent->GetOwner();
	int32 defaultID = INDEX_NONE;
	outSettings = FVAPrimitiveSettings();

	if (meshDefault)
	{
		outSettings.PropagateMode = meshDefault->PropagateMode;
		outSettings.MeshLOD = meshDefault->MeshLOD;
		outSettings.bUseFlatTransmission = meshDefault->bUseFlatTransmission;

		if (!meshDefault->GetMaterialID(this, reporter, defaultID) || !meshDefault->GetPermeationMaterialID(this, reporter, outSettings.PermeationMaterialID))
		{
			if (outInvalid)
				*outInvalid = true;

			return false;
		}
	}
	else
	{
		outSettings.PropagateMode = EVAPropagateMode::All;
	}

	for (const FStaticMaterial& staticMaterial : mesh->GetStaticMaterials())
	{
		bool overridden = false;
		int32 materialID = INDEX_NONE;

		if (meshDefault && !meshDefault->FindSlotMaterialID(this, reporter, staticMaterial.MaterialSlotName, overridden, materialID))
		{
			if (outInvalid)
				*outInvalid = true;

			return false;
		}

		outSettings.SlotMaterialIDs.Add(overridden ? materialID : ResolveSlotMaterial(staticMaterial.MaterialInterface, defaultID));
	}

	if (meshDefault)
	{
		outSettings.MaterialId = defaultID;
		return true;
	}

	// Untagged meshes use their simple collision (PropagateMode All), which is one body with one physical material, so that beats the slots. A mesh matched only by it is raytraced by its collision alone
	if (HasSimpleCollision(mesh) && ResolveCollisionMaterial(meshComponent->BodyInstance, mesh->GetBodySetup(), outSettings.MaterialId))
		return true;

	// Simple collision and convex hulls use the primitive's material directly, so it's the material most slots use (the first on a tie), never Air
	TMap<int32, int32> slotCounts;
	int32 bestCount = 0;

	for (int32 materialID : outSettings.SlotMaterialIDs)
	{
		if (materialID == INDEX_NONE)
			continue;

		int32& count = slotCounts.FindOrAdd(materialID);

		if (++count > bestCount)
		{
			bestCount = count;
			outSettings.MaterialId = materialID;
		}
	}

	// Untagged geometry is ignored
	return bestCount > 0;
}

bool AVAWorld::MakeRenderMeshKey(UStaticMesh* staticMesh, const FVAPrimitiveSettings& settings, FVAMeshKey& outKey)
{
	outKey = FVAMeshKey{ staticMesh, settings.MeshLOD, INDEX_NONE };
	bool anyAdded = settings.SlotMaterialIDs.Num() == 0;

	// Slots with the primitive's own material are Air, so placements whose defaults differ but whose overrides match still share the mesh
	for (int32 materialID : settings.SlotMaterialIDs)
	{
		outKey.SlotMaterials.Add(materialID == settings.MaterialId ? (int32)VAMaterialAir : materialID);
		anyAdded |= materialID != INDEX_NONE;
	}

	return anyAdded;
}

bool AVAWorld::ReadRenderTriangles(UStaticMesh* staticMesh, const FVAMeshKey& key, TArray<FVector>& outVertices, TArray<int32>& outTriangleMaterials) const
{
	// Render vertices are float, both baked and live
	TArray<FVAVector3f> liveVertices;
	TArray<int32> liveSlots;
	const TArray<FVAVector3f>* renderVertices = nullptr;

	// One per triangle, or null for every triangle in slot 0
	const TArray<int32>* triangleSlots = nullptr;

	if (GetRenderTriangles(staticMesh, key.LOD, liveVertices, liveSlots))
	{
		renderVertices = &liveVertices;
		triangleSlots = &liveSlots;
	}
	else if (const FVABakedMesh* bakedMesh = FindBakedMesh(staticMesh, key.LOD))
	{
		renderVertices = &bakedMesh->Vertices;
		triangleSlots = bakedMesh->TriangleSlots.Num() == bakedMesh->Vertices.Num() / 3 ? &bakedMesh->TriangleSlots : nullptr;
	}

	if (!renderVertices)
	{
		VA_WARN_NAMED(TEXT("Mesh '%s' will not affect raytracing as it has no baked geometry and no render mesh data. Run 'Bake Geometry For Shipping' on the VAWorld and save the level."), *staticMesh->GetName());
		return false;
	}

	int32 triangleCount = renderVertices->Num() / 3;
	bool anyOwnMaterial = false;
	outVertices.Reset(renderVertices->Num());
	outTriangleMaterials.Reset(triangleCount);

	for (int32 triangle = 0; triangle < triangleCount; triangle++)
	{
		// A slot the mesh doesn't list, e.g. from a stale bake, uses the primitive's material
		int32 slot = triangleSlots ? (*triangleSlots)[triangle] : 0;
		int32 material = key.SlotMaterials.IsValidIndex(slot) ? key.SlotMaterials[slot] : (int32)VAMaterialAir;

		if (material == INDEX_NONE)
			continue;

		for (int32 corner = 0; corner < 3; corner++)
			outVertices.Add(FVector((*renderVertices)[triangle * 3 + corner]));

		outTriangleMaterials.Add(material);
		anyOwnMaterial |= material != (int32)VAMaterialAir;
	}

	// Every triangle uses the primitive's material, so the SDK needs no per-triangle materials
	if (!anyOwnMaterial)
		outTriangleMaterials.Reset();

	return outVertices.Num() > 0;
}

bool AVAWorld::GetMeshTriangles(UStaticMeshComponent* meshComponent, TArray<FVector>& outVertices, TArray<int32>& outTriangleMaterials) const
{
	UStaticMesh* staticMesh = meshComponent ? meshComponent->GetStaticMesh() : nullptr;
	FVAPrimitiveSettings settings;
	FVAMeshKey key;

	if (!ResolvePrimitiveSettings(meshComponent, settings) || settings.PropagateMode == EVAPropagateMode::Colliders)
		return false;

	if ((settings.PropagateMode != EVAPropagateMode::Visuals && HasSimpleCollision(staticMesh)) || !MakeRenderMeshKey(staticMesh, settings, key) || !ReadRenderTriangles(staticMesh, key, outVertices, outTriangleMaterials))
		return false;

	// Air means the primitive's material
	if (outTriangleMaterials.Num() == 0)
		outTriangleMaterials.Init(VAMaterialAir, outVertices.Num() / 3);

	for (int32& material : outTriangleMaterials)
		if (material == (int32)VAMaterialAir)
			material = settings.MaterialId;

	return true;
}

bool AVAWorld::PassesCollisionFilter(const UPrimitiveComponent* component) const
{
	return CollisionObjectTypes.Num() == 0 || CollisionObjectTypes.Contains(component->GetCollisionObjectType());
}

void AVAWorld::AddShapePrimitive(UShapeComponent* shape, const FVAPrimitiveSettings& settings)
{
	VAMaterialType materialType = (VAMaterialType)settings.MaterialId;

	FVAPrimitiveBinding binding;
	binding.Component = shape;

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

void AVAWorld::AddStaticMeshPrimitives(UStaticMeshComponent* meshComponent, const FVAPrimitiveSettings& settings, const FTransform& meshTransform)
{
	UStaticMesh* staticMesh = meshComponent->GetStaticMesh();

	// Ignore components with no meshes
	if (!staticMesh)
		return;

	VAMaterialType materialType = (VAMaterialType)settings.MaterialId;
	UBodySetup* bodySetup = staticMesh->GetBodySetup();
	bool addedSimple = false;

	auto makeBinding = [&](void* primitive, EVAPrimitiveKind kind, const FTransform& elementTransform, const FVector& extent)
	{
		FVAPrimitiveBinding binding;
		binding.Component = meshComponent;
		binding.Primitive = primitive;
		binding.Kind = kind;
		binding.MeshTransform = meshTransform;
		binding.ElementTransform = elementTransform;
		binding.LocalExtent = extent;
		return binding;
	};

	// If this mesh is composed of simple collision elements, add them all. A mesh whose collision is filtered out is skipped rather than falling back to its triangles, since it's a collider
	if (settings.PropagateMode != EVAPropagateMode::Visuals && HasSimpleCollision(staticMesh))
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
			// A box with no thickness, e.g. /Engine/BasicShapes/Plane's. A prism this thin leaks bouncing rays through it, and its transmission does nothing as rays have no length inside it
			if (FVector(boxElem.X, boxElem.Y, boxElem.Z).GetMin() <= KINDA_SMALL_NUMBER)
			{
				VAPlanePrimitive* plane = vaPlanePrimitiveCreate();
				vaPlanePrimitiveSetMaterial(plane, materialType);
				addedSimple |= AddBinding(makeBinding(plane, EVAPrimitiveKind::PlaneFromMesh, FTransform(boxElem.GetTransform().GetRotation(), boxElem.GetTransform().GetTranslation()), FVector(boxElem.X, boxElem.Y, boxElem.Z)), TEXT("plane"));
				continue;
			}

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
			auto readHull = [&](TArray<FVector>& vertices, TArray<int32>&)
			{
#if UE_VERSION_OLDER_THAN(5, 0, 0) && !WITH_CHAOS
				// 4.27's PhysX build has no hull indices, so the cooked PhysX hull is triangulated instead. It's already in body space (element transform applied, mirroring undone) and has render winding. A copy of the element doesn't keep the cooked hull, so this reads the body setup's own
				const FKConvexElem& convexElem = agg.ConvexElems[convexIndex];
				TArray<FDynamicMeshVertex> hullVertices;
				TArray<uint32> hullIndices;
				convexElem.AddCachedSolidConvexGeom(hullVertices, hullIndices, FColor::White);

				if (hullIndices.Num() == 0)
					return false;

				vertices.Reserve(hullIndices.Num());

				for (uint32 index : hullIndices)
					vertices.Add(hullVertices[index].Position);

				return true;
#else
				FKConvexElem convexElem = agg.ConvexElems[convexIndex];

				if (convexElem.IndexData.Num() == 0)
					convexElem.ComputeChaosConvexIndices();

				if (convexElem.IndexData.Num() == 0)
					return false;

				FTransform elementTransform = convexElem.GetTransform();
				vertices.Reserve(convexElem.IndexData.Num());

				// Always inside the hull, as it's convex
				FVector hullCentre = FVector::ZeroVector;

				for (const FVector& vertex : convexElem.VertexData)
					hullCentre += elementTransform.TransformPosition(vertex);

				hullCentre /= convexElem.VertexData.Num();

				// The SDK's triangle test is one-sided, otherwise rays pass into the hull from outside. IndexData is saved with the asset and its winding depends on what generated it, so each triangle is faced away from the centre
				for (int32 i = 0; i + 2 < convexElem.IndexData.Num(); i += 3)
				{
					FVector a = elementTransform.TransformPosition(convexElem.VertexData[convexElem.IndexData[i]]);
					FVector b = elementTransform.TransformPosition(convexElem.VertexData[convexElem.IndexData[i + 1]]);
					FVector c = elementTransform.TransformPosition(convexElem.VertexData[convexElem.IndexData[i + 2]]);

					// The SDK's front face normal for (a, b, c) is Cross(c - a, b - a)
					if (FVector::DotProduct(FVector::CrossProduct(c - a, b - a), a - hullCentre) < 0.0)
						Swap(b, c);

					vertices.Add(a);
					vertices.Add(b);
					vertices.Add(c);
				}

				return true;
#endif
			};

			addedSimple |= AddMeshPrimitive(FVAMeshKey{ staticMesh, 0, convexIndex }, readHull, meshComponent, settings, meshTransform, false);
		}
	}

	if (addedSimple || settings.PropagateMode == EVAPropagateMode::Colliders)
		return;

	// One shared mesh per mesh, LOD and slot materials. A mesh whose slots are all ignored adds nothing
	FVAMeshKey key;

	if (!MakeRenderMeshKey(staticMesh, settings, key))
		return;

	// The live render data, or the baked copy if a cooked build has dropped it
	auto readTriangles = [&](TArray<FVector>& vertices, TArray<int32>& triangleMaterials)
	{
		return ReadRenderTriangles(staticMesh, key, vertices, triangleMaterials);
	};

	AddMeshPrimitive(key, readTriangles, meshComponent, settings, meshTransform, settings.bUseFlatTransmission);
}

// The samples and hole flags of a landscape component's collision heightfield, in component space. The only part of AddLandscapePrimitive that depends on the physics engine
#if UE_VERSION_OLDER_THAN(5, 0, 0) && !WITH_CHAOS
struct FVALandscapeHeightfield
{
	TArray<physx::PxHeightFieldSample> Samples;
	int32 PhysXRows = 0;
	int32 PhysXColumns = 0;

	explicit FVALandscapeHeightfield(const ULandscapeHeightfieldCollisionComponent* component)
	{
		const physx::PxHeightField* heightField = IsValidRef(component->HeightfieldRef) ? component->HeightfieldRef->RBHeightfield : nullptr;

		if (!heightField)
			return;

		PhysXRows = heightField->getNbRows();
		PhysXColumns = heightField->getNbColumns();
		Samples.SetNumUninitialized(PhysXRows * PhysXColumns);
		heightField->saveCells(Samples.GetData(), Samples.Num() * Samples.GetTypeSize());
	}

	// PhysX's rows run backwards along the component's X and its columns along Y, as the cooked data is flipped for PhysX's handedness (ConvertHeightfieldDataForPhysx). A mirrored landscape isn't flipped, but AddLandscapePrimitive rejects those
	bool IsValid() const { return PhysXRows >= 2 && PhysXColumns >= 2; }
	int32 GetNumCols() const { return PhysXRows; }
	int32 GetNumRows() const { return PhysXColumns; }
	double GetHeight(int32 x, int32 y) const { return (double)Samples[(PhysXRows - 1 - x) * PhysXColumns + y].height * LANDSCAPE_ZSCALE; }

	// Whether the quad at (x, y) was painted with the visibility layer. The flip makes a quad's PhysX cell the one that starts on its far X edge
	bool IsHole(int32 x, int32 y) const { return Samples[(PhysXRows - 2 - x) * PhysXColumns + y].materialIndex0 == physx::PxHeightFieldMaterial::eHOLE; }
};
#else
struct FVALandscapeHeightfield
{
	const Chaos::FHeightField* HeightField = nullptr;

	explicit FVALandscapeHeightfield(const ULandscapeHeightfieldCollisionComponent* component)
	{
#if UE_VERSION_OLDER_THAN(5, 0, 0)
		HeightField = IsValidRef(component->HeightfieldRef) ? component->HeightfieldRef->Heightfield.Get() : nullptr;
#else
		HeightField = IsValidRef(component->HeightfieldRef) ? component->HeightfieldRef->HeightfieldGeometry.GetReference() : nullptr;
#endif
	}

	bool IsValid() const { return HeightField && HeightField->GetNumCols() >= 2 && HeightField->GetNumRows() >= 2; }
	int32 GetNumCols() const { return HeightField->GetNumCols(); }
	int32 GetNumRows() const { return HeightField->GetNumRows(); }
	double GetHeight(int32 x, int32 y) const { return HeightField->GetHeight(x, y) * LANDSCAPE_ZSCALE; }

	// Whether the quad at (x, y) was painted with the visibility layer
	bool IsHole(int32 x, int32 y) const { return HeightField->IsHole(x, y); }
};
#endif

void AVAWorld::AddLandscapePrimitive(ULandscapeHeightfieldCollisionComponent* landscapeComponent, const FVAPrimitiveSettings& settings)
{
	// A landscape is both a collider and a visual, so only the collision filter can leave it out
	if (settings.PropagateMode != EVAPropagateMode::Visuals && !PassesCollisionFilter(landscapeComponent))
		return;

	FString landscapeName = VAActorName(landscapeComponent->GetOwner());
	FVALandscapeHeightfield heightField(landscapeComponent);

	if (!heightField.IsValid())
	{
		VA_WARN_NAMED(TEXT("Landscape '%s' will not affect raytracing as its component '%s' has no collision. Enable collision on the landscape."), *landscapeName, *landscapeComponent->GetName());
		return;
	}

	// The heightmap's scale can't mirror it
	FVector scale = landscapeComponent->GetComponentTransform().GetScale3D();

	if (scale.X <= 0.0 || scale.Y <= 0.0 || scale.Z <= 0.0)
	{
		VA_WARN_NAMED(TEXT("Landscape '%s' will not affect raytracing as its scale %s has a negative or zero axis."), *landscapeName, *scale.ToString());
		return;
	}

	int32 quadsX = heightField.GetNumCols() - 1;
	int32 quadsY = heightField.GetNumRows() - 1;

	// The collision heightfield exists in cooked builds, unlike the heightmap. It's in component space, and the component's scale goes in the heightmap's scale, as its transform can't hold scale
	double quadSize = landscapeComponent->CollisionScale * landscapeComponent->CollisionSizeQuads / quadsX;

	// A component's quad count is 2^k - 1, so a power-of-two stride never divides it evenly. LandscapeLOD instead splits each side into equal cells, rounded up
	const int32 stride = 1 << FMath::Clamp(LandscapeLOD, 0, 7);
	int32 cellsX = FMath::DivideAndRoundUp(quadsX, stride);
	int32 cellsY = FMath::DivideAndRoundUp(quadsY, stride);
	double cellQuadsX = (double)quadsX / cellsX;
	double cellQuadsY = (double)quadsY / cellsY;

	// The height of the collision's surface at a point in quads. Exact at LOD 0, and along a component's edges only the edge's own points are used, so neighbouring components still meet
	auto surfaceAt = [&](double x, double y)
	{
		int32 x0 = FMath::Min(VAFloorToInt32(x), quadsX - 1);
		int32 y0 = FMath::Min(VAFloorToInt32(y), quadsY - 1);
		double fx = x - x0;
		double fy = y - y0;

		double h00 = heightField.GetHeight(x0, y0);
		double h10 = heightField.GetHeight(x0 + 1, y0);
		double h01 = heightField.GetHeight(x0, y0 + 1);
		double h11 = heightField.GetHeight(x0 + 1, y0 + 1);

		// Split along (x0, y0) to (x0 + 1, y0 + 1), like the heightmap's own triangles
		return fx >= fy
			? h00 + fx * (h10 - h00) + fy * (h11 - h10)
			: h00 + fy * (h01 - h00) + fx * (h11 - h01);
	};

	// Painted with the visibility layer. A coarse cell stays open if any quad under it is, so a small opening isn't closed over
	auto isHole = [&](int32 cellX, int32 cellY)
	{
		int32 x1 = FMath::Min(VACeilToInt32((cellX + 1) * cellQuadsX), quadsX);
		int32 y1 = FMath::Min(VACeilToInt32((cellY + 1) * cellQuadsY), quadsY);

		for (int32 y = VAFloorToInt32(cellY * cellQuadsY); y < y1; y++)
			for (int32 x = VAFloorToInt32(cellX * cellQuadsX); x < x1; x++)
				if (heightField.IsHole(x, y))
					return true;

		return false;
	};

	// Unreal's X runs along the heightmap's rows (its local Z) and Unreal's Y along each row (its local X), to match vaHeightmapPrimitiveSetTransformUnreal's axes
	int32 rowLength = cellsY + 1;
	int32 rowCount = cellsX + 1;
	TArray<VAFloat> heights;
	TArray<bool> holes;
	heights.SetNumUninitialized(rowLength * rowCount);
	holes.SetNumUninitialized(cellsX * cellsY);
	bool anyHoles = false;

	for (int32 x = 0; x < rowCount; x++)
		for (int32 y = 0; y < rowLength; y++)
			heights[x * rowLength + y] = (VAFloat)surfaceAt(x * cellQuadsX, y * cellQuadsY);

	for (int32 x = 0; x < cellsX; x++)
	{
		for (int32 y = 0; y < cellsY; y++)
		{
			holes[x * cellsY + y] = isHole(x, y);
			anyHoles |= holes[x * cellsY + y];
		}
	}

	VAHeightmapPrimitive* heightmap;
	VAResult result = vaHeightmapPrimitiveCreateWithHoles(heights.GetData(), anyHoles ? holes.GetData() : nullptr, rowLength, rowCount, &heightmap);

	if (result != VA_SUCCESS)
	{
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to create a heightmap primitive for '%s' on '%s'."), *landscapeComponent->GetName(), *landscapeName);
		return;
	}

	vaHeightmapPrimitiveSetMaterial(heightmap, (VAMaterialType)settings.MaterialId);

	// An open surface that always uses flat transmission, which is two-sided and muffles once per crossing. Scale and transform are set by RefreshPrimitiveTransform in AddBinding
	FVAPrimitiveBinding binding;
	binding.Component = landscapeComponent;
	binding.Primitive = heightmap;
	binding.Kind = EVAPrimitiveKind::Heightmap;
	binding.LocalExtent = FVector(quadSize * cellQuadsX, quadSize * cellQuadsY, 1.0);
	AddBinding(binding, TEXT("heightmap"));
}

int32 AVAWorld::GetHeightmapTriangleCount() const
{
	int32 triangles = 0;

	for (const FVAPrimitiveBinding& binding : PrimitiveBindings)
	{
		if (binding.Kind != EVAPrimitiveKind::Heightmap)
			continue;

		const VAHeightmapPrimitive* heightmap = static_cast<const VAHeightmapPrimitive*>(binding.Primitive);
		triangles += (vaHeightmapPrimitiveGetSizeX(heightmap) - 1) * (vaHeightmapPrimitiveGetSizeY(heightmap) - 1) * 2;
	}

	return triangles;
}

VAMesh* AVAWorld::AcquireSharedMesh(const FVAMeshKey& key, TFunctionRef<bool(TArray<FVector>&, TArray<int32>&)> readVertices, const UPrimitiveComponent* meshComponent)
{
	if (FVASharedMesh* shared = SharedMeshes.Find(key))
	{
		shared->ReferenceCount++;
		return shared->Mesh;
	}

	TArray<FVector> vertices;
	TArray<int32> triangleMaterials;

	if (!readVertices(vertices, triangleMaterials))
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

	// Null when every triangle uses the primitive's material
	TArray<VAMaterialType> vaMaterials;

	for (int32 material : triangleMaterials)
		vaMaterials.Add((VAMaterialType)material);

	VAMesh* mesh;
	VAResult result = vaMeshCreateWithMaterials(vaVertices.GetData(), vaMaterials.Num() > 0 ? vaMaterials.GetData() : nullptr, vaVertices.Num(), minBounds, maxBounds, &mesh);

	if (result != VA_SUCCESS)
	{
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to create a mesh for '%s' on '%s'."), *meshComponent->GetName(), *VAActorName(meshComponent->GetOwner()));
		return nullptr;
	}

	SharedMeshes.Add(key, FVASharedMesh{ mesh, 1, vaVertices.Num() / 3 });
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

bool AVAWorld::AddMeshPrimitive(const FVAMeshKey& key, TFunctionRef<bool(TArray<FVector>&, TArray<int32>&)> readVertices, UPrimitiveComponent* meshComponent, const FVAPrimitiveSettings& settings, const FTransform& meshTransform, bool useFlatTransmission)
{
	VAMesh* sharedMesh = AcquireSharedMesh(key, readVertices, meshComponent);

	if (!sharedMesh)
		return false;

	// The vertices are in mesh space, so component scale and an instance's transform only go into the primitive's transform
	VAMatrix vaTransform = MakeScaleRotTransMatrix(meshTransform * meshComponent->GetComponentTransform());
	VAMeshPrimitive* mesh;

	VAResult result = vaMeshPrimitiveCreateFromMesh((VAMaterialType)settings.MaterialId, sharedMesh, &vaTransform, &mesh);

	if (result != VA_SUCCESS)
	{
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to create a mesh primitive for '%s' on '%s'."), *meshComponent->GetName(), *VAActorName(meshComponent->GetOwner()));
		ReleaseSharedMesh(key);
		return false;
	}

	vaMeshPrimitiveSetUseFlatTransmission(mesh, useFlatTransmission);

	// Checked against the world's materials when the primitive is added
	result = vaMeshPrimitiveSetPermeationMaterial(mesh, (VAMaterialType)settings.PermeationMaterialID);

	switch (result)
	{
		case VA_SUCCESS:
		case VA_UNCHANGED:
			break;

		default:
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to set the permeation material of the mesh primitive for '%s' on '%s'."), *meshComponent->GetName(), *VAActorName(meshComponent->GetOwner()));
			break;
	}

	FVAPrimitiveBinding binding;
	binding.Component = meshComponent;
	binding.Primitive = mesh;
	binding.Kind = EVAPrimitiveKind::Mesh;
	binding.MeshKey = key;
	binding.MeshTransform = meshTransform;
	return AddBinding(binding, TEXT("mesh"));
}
