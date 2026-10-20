#include "VAWorld.h"
#include "VAConstants.h"
#include "VALog.h"

#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"

extern "C" {
#include "vaudio.h"
}

bool AVAWorld::AddBinding(FVAPrimitiveBinding binding, const TCHAR* typeName)
{
	USceneComponent* component = binding.Component.Get();
	FString actorName = component->GetOwner()->GetActorNameOrLabel();

	RefreshPrimitiveTransform(binding);

	VAResult result = vaWorldAddPrimitive_(World, binding.Primitive);

	if (result != VA_SUCCESS)
	{
		VA_WARN_NAMED_RESULT(result, TEXT("'%s': failed to add %s primitive to raytracing."), *actorName, typeName);
		ActorsWithInvalidMaterials.AddUnique(actorName);
		DestroyPrimitive(binding);
		return false;
	}

	binding.Handle = component->TransformUpdated.AddUObject(this, &AVAWorld::OnPrimitiveComponentMoved);

	int32 index = PrimitiveBindings.Add(binding);
	PrimitiveBindingsByComponent.FindOrAdd(component).Add(index);
	return true;
}

void AVAWorld::RemoveBindings(TFunctionRef<bool(const FVAPrimitiveBinding&)> predicate)
{
	// The world already ended, and DestroyPrimitives destroyed every primitive
	if (!World)
		return;

	bool removedAny = false;

	for (int32 i = PrimitiveBindings.Num() - 1; i >= 0; --i)
	{
		FVAPrimitiveBinding& binding = PrimitiveBindings[i];

		if (!predicate(binding))
			continue;

		if (USceneComponent* component = binding.Component.Get())
			component->TransformUpdated.Remove(binding.Handle);

		VAResult result = vaWorldRemovePrimitive_(World, binding.Primitive);

		if (result != VA_SUCCESS)
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to remove a primitive from the world."));

		// The SDK captures what it needs in vaWorldRemovePrimitive, so the primitive can be destroyed straight away
		DestroyPrimitive(binding);

		PrimitiveBindings.RemoveAtSwap(i);
		removedAny = true;
	}

	if (!removedAny)
		return;

	PrimitiveBindingsByComponent.Empty();

	for (int32 i = 0; i < PrimitiveBindings.Num(); i++)
		if (USceneComponent* component = PrimitiveBindings[i].Component.Get())
			PrimitiveBindingsByComponent.FindOrAdd(component).Add(i);
}

void AVAWorld::DestroyPrimitives()
{
	RemoveBindings([](const FVAPrimitiveBinding&) { return true; });
}

void AVAWorld::DestroyPrimitive(const FVAPrimitiveBinding& binding)
{
	void* primitive = binding.Primitive;

	switch (binding.Kind)
	{
		case EVAPrimitiveKind::Sphere:
		case EVAPrimitiveKind::SphereFromMesh:
			vaSpherePrimitiveDestroy(static_cast<VASpherePrimitive*>(primitive));
			break;

		case EVAPrimitiveKind::Prism:
		case EVAPrimitiveKind::PrismFromMesh:
			vaPrismPrimitiveDestroy(static_cast<VAPrismPrimitive*>(primitive));
			break;

		case EVAPrimitiveKind::Capsule:
		case EVAPrimitiveKind::CapsuleFromMesh:
			vaCapsulePrimitiveDestroy(static_cast<VACapsulePrimitive*>(primitive));
			break;

		case EVAPrimitiveKind::Mesh:
			// The primitive references the shared mesh, so it goes first
			vaMeshPrimitiveDestroy(static_cast<VAMeshPrimitive*>(primitive));
			ReleaseSharedMesh(binding.MeshKey);
			break;
	}
}

void AVAWorld::OnGeometryActorEndPlay(AActor* actor, EEndPlayReason::Type endPlayReason)
{
	RemoveBindings([actor](const FVAPrimitiveBinding& binding)
	{
		USceneComponent* component = binding.Component.Get();
		return !component || component->GetOwner() == actor;
	});
}

// A negative mesh scale mirrors a collision element. Boxes and capsules are symmetric about their own axes, so the mirrored element is the same shape with its rotation conjugated by the sign flip, and its size from the absolute scale
static FQuat MirrorElementRotation(const FQuat& rotation, const FVector& meshScale)
{
	FVector sign = meshScale.GetSignVector();

	if (sign == FVector::OneVector)
		return rotation;

	FScaleMatrix flip(sign);
	return FQuat(flip * FRotationMatrix::Make(rotation) * flip);
}

void AVAWorld::RefreshPrimitiveTransform(const FVAPrimitiveBinding& binding)
{
	USceneComponent* component = binding.Component.Get();

	if (!component)
		return;

	FTransform componentTransform = component->GetComponentTransform();

	// The space the mesh's vertices and simple collision elements are defined in, including scale
	FTransform meshTransform = binding.MeshTransform * componentTransform;
	FVector signedMeshScale = meshTransform.GetScale3D();
	FVector meshScale = signedMeshScale.GetAbs();

	switch (binding.Kind)
	{
		case EVAPrimitiveKind::Sphere:
		{
			VASpherePrimitive* vaSphere = static_cast<VASpherePrimitive*>(binding.Primitive);
			vaSpherePrimitiveSetCenterUnreal(vaSphere, componentTransform.GetTranslation());
			vaSpherePrimitiveSetRadius(vaSphere, CastChecked<USphereComponent>(component)->GetScaledSphereRadius());
			break;
		}
		case EVAPrimitiveKind::Prism:
		{
			// Negative with a negative scale, and a box is symmetric about its own axes, so the mirror only needs the absolute size
			FVector extent = CastChecked<UBoxComponent>(component)->GetScaledBoxExtent().GetAbs();
			VAPrismPrimitive* vaPrism = static_cast<VAPrismPrimitive*>(binding.Primitive);
			vaPrismPrimitiveSetSize(vaPrism, FVectorToVAVector(extent * 2.0));
			vaPrismPrimitiveSetTransformUnreal(vaPrism, componentTransform);
			break;
		}
		case EVAPrimitiveKind::Capsule:
		{
			UCapsuleComponent* capsuleComp = CastChecked<UCapsuleComponent>(component);
			VACapsulePrimitive* vaCapsule = static_cast<VACapsulePrimitive*>(binding.Primitive);
			vaCapsulePrimitiveSetRadius(vaCapsule, capsuleComp->GetScaledCapsuleRadius());
			vaCapsulePrimitiveSetLength(vaCapsule, capsuleComp->GetScaledCapsuleHalfHeight_WithoutHemisphere() * 2.0f);
			vaCapsulePrimitiveSetTransformUnreal(vaCapsule, componentTransform);
			break;
		}
		case EVAPrimitiveKind::Mesh:
		{
			vaMeshPrimitiveSetTransformUnreal(static_cast<VAMeshPrimitive*>(binding.Primitive), meshTransform);
			break;
		}
		case EVAPrimitiveKind::SphereFromMesh:
		{
			VASpherePrimitive* vaSphere = static_cast<VASpherePrimitive*>(binding.Primitive);
			vaSpherePrimitiveSetCenterUnreal(vaSphere, meshTransform.TransformPosition(binding.ElementTransform.GetTranslation()));
			vaSpherePrimitiveSetRadius(vaSphere, (VAFloat)(binding.LocalExtent.X * meshScale.GetMax()));
			break;
		}
		case EVAPrimitiveKind::PrismFromMesh:
		{
			// Rotated and positioned like the element, with the scale applied to its size rather than its transform
			FTransform elementTransform(meshTransform.GetRotation() * MirrorElementRotation(binding.ElementTransform.GetRotation(), signedMeshScale), meshTransform.TransformPosition(binding.ElementTransform.GetTranslation()));
			VAPrismPrimitive* vaPrism = static_cast<VAPrismPrimitive*>(binding.Primitive);
			vaPrismPrimitiveSetSizeUnreal(vaPrism, binding.LocalExtent * meshScale);
			vaPrismPrimitiveSetTransformUnreal(vaPrism, elementTransform);
			break;
		}
		case EVAPrimitiveKind::CapsuleFromMesh:
		{
			FTransform elementTransform(meshTransform.GetRotation() * MirrorElementRotation(binding.ElementTransform.GetRotation(), signedMeshScale), meshTransform.TransformPosition(binding.ElementTransform.GetTranslation()));
			VACapsulePrimitive* vaCapsule = static_cast<VACapsulePrimitive*>(binding.Primitive);
			vaCapsulePrimitiveSetRadius(vaCapsule, (VAFloat)(binding.LocalExtent.X * FMath::Max(meshScale.X, meshScale.Y)));
			vaCapsulePrimitiveSetLength(vaCapsule, (VAFloat)(binding.LocalExtent.Z * meshScale.Z));
			vaCapsulePrimitiveSetTransformUnreal(vaCapsule, elementTransform);
			break;
		}
	}
}

void AVAWorld::OnPrimitiveComponentMoved(USceneComponent* updatedComponent, EUpdateTransformFlags updateTransformFlags, ETeleportType teleport)
{
	if (TArray<int32>* bindingIndices = PrimitiveBindingsByComponent.Find(updatedComponent))
		for (int32 bindingIndex : *bindingIndices)
			RefreshPrimitiveTransform(PrimitiveBindings[bindingIndex]);
}
