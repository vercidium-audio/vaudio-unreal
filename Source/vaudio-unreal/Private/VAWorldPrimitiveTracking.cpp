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
		DestroyPrimitive(binding.Primitive, binding.Kind);
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
		DestroyPrimitive(binding.Primitive, binding.Kind);

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

void AVAWorld::DestroyPrimitive(void* primitive, EVAPrimitiveKind kind)
{
	switch (kind)
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
			vaMeshPrimitiveDestroy(static_cast<VAMeshPrimitive*>(primitive));
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

void AVAWorld::RefreshPrimitiveTransform(const FVAPrimitiveBinding& binding)
{
	USceneComponent* component = binding.Component.Get();

	if (!component)
		return;

	FTransform componentTransform = component->GetComponentTransform();

	// The space the mesh's vertices and simple collision elements are defined in, including scale
	FTransform meshTransform = binding.MeshTransform * componentTransform;
	FVector meshScale = meshTransform.GetScale3D().GetAbs();

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
			FVector extent = CastChecked<UBoxComponent>(component)->GetScaledBoxExtent();
			VAPrismPrimitive* vaPrism = static_cast<VAPrismPrimitive*>(binding.Primitive);
			vaPrismPrimitiveSetSize(vaPrism, vaVectorCreate(extent.X * 2.0f, extent.Y * 2.0f, extent.Z * 2.0f));
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
			vaSpherePrimitiveSetRadius(vaSphere, binding.LocalExtent.X * meshScale.GetMax());
			break;
		}
		case EVAPrimitiveKind::PrismFromMesh:
		{
			// Rotated and positioned like the element, with the scale applied to its size rather than its transform
			FTransform elementTransform(meshTransform.GetRotation() * binding.ElementTransform.GetRotation(), meshTransform.TransformPosition(binding.ElementTransform.GetTranslation()));
			VAPrismPrimitive* vaPrism = static_cast<VAPrismPrimitive*>(binding.Primitive);
			vaPrismPrimitiveSetSizeUnreal(vaPrism, binding.LocalExtent * meshScale);
			vaPrismPrimitiveSetTransformUnreal(vaPrism, elementTransform);
			break;
		}
		case EVAPrimitiveKind::CapsuleFromMesh:
		{
			FTransform elementTransform(meshTransform.GetRotation() * binding.ElementTransform.GetRotation(), meshTransform.TransformPosition(binding.ElementTransform.GetTranslation()));
			VACapsulePrimitive* vaCapsule = static_cast<VACapsulePrimitive*>(binding.Primitive);
			vaCapsulePrimitiveSetRadius(vaCapsule, binding.LocalExtent.X * FMath::Max(meshScale.X, meshScale.Y));
			vaCapsulePrimitiveSetLength(vaCapsule, binding.LocalExtent.Z * meshScale.Z);
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
