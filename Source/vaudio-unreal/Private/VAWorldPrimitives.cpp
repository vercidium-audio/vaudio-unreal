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

			FVABakedMesh& Baked = BakedMeshes.AddDefaulted_GetRef();

			if (!GetRenderVertices(Mesh, MatComp->MeshLOD, Baked.Vertices))
			{
				VA_WARN_NAMED(TEXT("Mesh '%s' on '%s' has no render data in-editor, skipping"), *Mesh->GetName(), *Actor->GetActorNameOrLabel());
				BakedMeshes.Pop();
				continue;
			}

			Baked.ActorName = Actor->GetName();
			Baked.ComponentName = MeshComp->GetFName();

			++BakedCount;

			VA_LOG_NAMED(TEXT("Baked '%s'.'%s' tris=%d"), *Actor->GetActorNameOrLabel(), *MeshComp->GetName(), Baked.Vertices.Num() / 3);
		}
	}

	MarkPackageDirty();
	VA_LOG_NAMED(TEXT("Baked %d mesh component(s). Save the level to persist."), BakedCount);
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
		for (const FKConvexElem& sourceElem : agg.ConvexElems)
		{
			FKConvexElem convexElem = sourceElem;

			if (convexElem.IndexData.IsEmpty())
				convexElem.ComputeChaosConvexIndices();

			if (convexElem.IndexData.IsEmpty())
				continue;

			FTransform elementTransform = convexElem.GetTransform();
			TArray<FVector3f> vertices;
			vertices.Reserve(convexElem.IndexData.Num());

			// The SDK's triangle test is one-sided. Render triangles already have the winding it expects, but Chaos' hull indices are wound the other way, so each triangle's last two vertices are swapped. Otherwise rays pass into the hull from outside
			// TODO - add a winding field to Mesh and MeshPrimitives in the C SDK, so this kind of data transform below isn't required
			for (int32 i = 0; i + 2 < convexElem.IndexData.Num(); i += 3)
			{
				vertices.Add(FVector3f(elementTransform.TransformPosition(convexElem.VertexData[convexElem.IndexData[i]])));
				vertices.Add(FVector3f(elementTransform.TransformPosition(convexElem.VertexData[convexElem.IndexData[i + 2]])));
				vertices.Add(FVector3f(elementTransform.TransformPosition(convexElem.VertexData[convexElem.IndexData[i + 1]])));
			}

			addedSimple |= AddMeshPrimitive(vertices, meshComponent, source, meshTransform, materialId, false);
		}
	}

	if (addedSimple || source->PropagateMode == EVAPropagateMode::Colliders)
		return;

	// Attempt to use baked geometry. Fall back to mesh data (may be unavailable in cooked builds)
	TArray<FVector3f> localVertices;
	const FVABakedMesh* bakedMesh = BakedMeshes.FindByPredicate([&](const FVABakedMesh& baked) { return baked.ComponentName == meshComponent->GetFName() && baked.ActorName == actor->GetName(); });

	if (bakedMesh)
	{
		localVertices = bakedMesh->Vertices;
	}
	else if (!GetRenderVertices(staticMesh, source->MeshLOD, localVertices))
	{
		VA_WARN_NAMED(TEXT("Mesh '%s' will not affect raytracing as it has no baked geometry and no render mesh data. Run 'Bake Geometry For Shipping' on the VAWorld and save the level."), *staticMesh->GetName());
		return;
	}

	AddMeshPrimitive(localVertices, meshComponent, source, meshTransform, materialId, useFlatTransmission);
}

bool AVAWorld::AddMeshPrimitive(const TArray<FVector3f>& vertices, UStaticMeshComponent* meshComponent, UVAMaterialComponent* source, const FTransform& meshTransform, int32 materialId, bool useFlatTransmission)
{
	TArray<VAVector> vaVertices;
	vaVertices.Reserve(vertices.Num());

	// Not vaudio.h's VECTOR_MIN, which is FLT_MIN (the smallest positive float), so it would be wrong for a mesh entirely in negative coordinates
	VAVector minBounds = vaVectorCreate(FLT_MAX, FLT_MAX, FLT_MAX);
	VAVector maxBounds = vaVectorCreate(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (const FVector3f& localPosition : vertices)
	{
		VAVector vaPosition = vaVectorCreate(localPosition.X, localPosition.Y, localPosition.Z);
		vaVertices.Add(vaPosition);

		minBounds = vaVectorMin(minBounds, vaPosition);
		maxBounds = vaVectorMax(maxBounds, vaPosition);
	}

	VAMatrix vaTransform = MakeScaleRotTransMatrix(meshTransform * meshComponent->GetComponentTransform());
	VAMeshPrimitive* mesh;

	VAResult result = vaMeshPrimitiveCreate((VAMaterialType)materialId, vaVertices.GetData(), vaVertices.Num(), minBounds, maxBounds, &vaTransform, &mesh);

	if (result != VA_SUCCESS)
	{
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to create a mesh primitive for '%s' on '%s'."), *meshComponent->GetName(), *meshComponent->GetOwner()->GetActorNameOrLabel());
		return false;
	}

	vaMeshPrimitiveSetUseFlatTransmission(mesh, useFlatTransmission);

	FVAPrimitiveBinding binding;
	binding.Component = meshComponent;
	binding.Source = source;
	binding.Primitive = mesh;
	binding.Kind = EVAPrimitiveKind::Mesh;
	binding.MeshTransform = meshTransform;
	return AddBinding(binding, TEXT("mesh"));
}
