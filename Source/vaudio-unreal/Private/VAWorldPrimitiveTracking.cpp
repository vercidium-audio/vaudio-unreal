#include "VAWorld.h"
#include "VAConstants.h"
#include "VALog.h"

#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"

extern "C" {
#include "vaudio.h"
}

void AVAWorld::DestroyPrimitives()
{
	UnbindPrimitiveComponents();

	VAWorld* vaWorld = GetVAWorld();

	for (VASpherePrimitive* prim : SpherePrimitives)
	{
		vaWorldRemovePrimitive_(vaWorld, prim);
		vaSpherePrimitiveDestroy(prim);
	}

	for (VAPrismPrimitive* prim : PrismPrimitives)
	{
		vaWorldRemovePrimitive_(vaWorld, prim);
		vaPrismPrimitiveDestroy(prim);
	}

	for (VACapsulePrimitive* prim : CapsulePrimitives)
	{
		vaWorldRemovePrimitive_(vaWorld, prim);
		vaCapsulePrimitiveDestroy(prim);
	}

	for (VAMeshPrimitive* prim : MeshPrimitives)
	{
		vaWorldRemovePrimitive_(vaWorld, prim);
		vaMeshPrimitiveDestroy(prim);
	}

	SpherePrimitives.Empty();
	PrismPrimitives.Empty();
	CapsulePrimitives.Empty();
	MeshPrimitives.Empty();
}

void AVAWorld::BindPrimitiveToComponent(void* primitive, EVAPrimitiveKind kind, USceneComponent* component, const FTransform& localOffset, const FVector& localExtent)
{
	// Shouldn't be null but check anyway
	if (!component)
	{
		VA_WARN_NAMED(TEXT("Primitive has a null Component and will not track its actor's movement."));
		return;
	}

	int32 bindingIndex = PrimitiveBindings.Num();
	FVAPrimitiveBinding& binding = PrimitiveBindings.AddDefaulted_GetRef();
	binding.Component    = component;
	binding.Primitive    = primitive;
	binding.Kind         = kind;
	binding.LocalOffset  = localOffset;
	binding.LocalExtent  = localExtent;
	binding.Handle       = component->TransformUpdated.AddUObject(this, &AVAWorld::OnPrimitiveComponentMoved);

	PrimitiveBindingsByComponent.FindOrAdd(component).Add(bindingIndex);
}

void AVAWorld::RefreshPrimitiveTransform(const FVAPrimitiveBinding& binding)
{
	USceneComponent* component = binding.Component.Get();

	if (!component)
		return;

	FTransform worldTransform = binding.LocalOffset * component->GetComponentTransform();

	switch (binding.Kind)
	{
		case EVAPrimitiveKind::Sphere:
		{
			USphereComponent* sphereComp = CastChecked<USphereComponent>(component);
			VASpherePrimitive* vaSphere = static_cast<VASpherePrimitive*>(binding.Primitive);

			vaSpherePrimitiveSetCenterUnreal(vaSphere, worldTransform.GetTranslation());
			vaSpherePrimitiveSetRadius(vaSphere, sphereComp->GetScaledSphereRadius());
			break;
		}
		case EVAPrimitiveKind::Prism:
		{
			UBoxComponent* boxComp = CastChecked<UBoxComponent>(component);
			FVector extent = boxComp->GetScaledBoxExtent();
			VAPrismPrimitive* vaPrism = static_cast<VAPrismPrimitive*>(binding.Primitive);

			vaPrismPrimitiveSetSize(vaPrism, vaVectorCreate(extent.X * 2.0f, extent.Y * 2.0f, extent.Z * 2.0f));
			vaPrismPrimitiveSetTransformUnreal(vaPrism, worldTransform);
			break;
		}
		case EVAPrimitiveKind::Capsule:
		{
			UCapsuleComponent* capsuleComp = CastChecked<UCapsuleComponent>(component);
			VACapsulePrimitive* vaCapsule = static_cast<VACapsulePrimitive*>(binding.Primitive);

			vaCapsulePrimitiveSetRadius(vaCapsule, capsuleComp->GetScaledCapsuleRadius());
			vaCapsulePrimitiveSetLength(vaCapsule, capsuleComp->GetScaledCapsuleHalfHeight_WithoutHemisphere() * 2.0f);
			vaCapsulePrimitiveSetTransformUnreal(vaCapsule, worldTransform);
			break;
		}
		case EVAPrimitiveKind::Mesh:
		{
			vaMeshPrimitiveSetTransformUnreal(static_cast<VAMeshPrimitive*>(binding.Primitive), worldTransform);
			break;
		}
		case EVAPrimitiveKind::CapsuleFromMesh:
		{
			// Matches the FMath::Max(scale.X, scale.Y)/scale.Z convention ScanAndAddPrimitives
			// originally used for FKSphylElem - see the comment on EVAPrimitiveKind.
			FVector scale = component->GetComponentTransform().GetScale3D();
			VACapsulePrimitive* vaCapsule = static_cast<VACapsulePrimitive*>(binding.Primitive);

			vaCapsulePrimitiveSetRadius(vaCapsule, binding.LocalExtent.X * FMath::Max(scale.X, scale.Y));
			vaCapsulePrimitiveSetLength(vaCapsule, binding.LocalExtent.Z * scale.Z);
			vaCapsulePrimitiveSetTransformUnreal(vaCapsule, worldTransform);
			break;
		}
		case EVAPrimitiveKind::SphereFromMesh:
		{
			FVector scale = component->GetComponentTransform().GetScale3D();
			FVector center = worldTransform.GetTranslation();
			VASpherePrimitive* vaSphere = static_cast<VASpherePrimitive*>(binding.Primitive);

			vaSpherePrimitiveSetCenterUnreal(vaSphere, center);
			vaSpherePrimitiveSetRadius(vaSphere, binding.LocalExtent.X * scale.GetAbsMax());
			break;
		}
		case EVAPrimitiveKind::PrismFromMesh:
		{
			FVector Scale = component->GetComponentTransform().GetScale3D();
			VAPrismPrimitive* vaPrism = static_cast<VAPrismPrimitive*>(binding.Primitive);

			vaPrismPrimitiveSetSizeUnreal(vaPrism, binding.LocalExtent * Scale);
			vaPrismPrimitiveSetTransformUnreal(vaPrism, worldTransform);
			break;
		}
	}
}

void AVAWorld::OnPrimitiveComponentMoved(USceneComponent* updatedComponent, EUpdateTransformFlags updateTransformFlags, ETeleportType teleport)
{
	TArray<int32>* bindingIndices = PrimitiveBindingsByComponent.Find(updatedComponent);

	if (!bindingIndices)
		return;

	// Loop over the bindings owned by this component (not all components!)
	for (int32 i = bindingIndices->Num() - 1; i >= 0; --i)
	{
		int32 bindingIndex = (*bindingIndices)[i];
		FVAPrimitiveBinding& binding = PrimitiveBindings[bindingIndex];

		// If the actor was deleted during PIE while the world is alive, kill the binding
		if (!binding.Component.IsValid())
		{
			// RemoveAtSwap moves the last binding into bindingIndex, so PrimitiveBindingsByComponent's
			// entry for whichever component that last binding belongs to must point at its new index.
			int32 lastIndex = PrimitiveBindings.Num() - 1;

			if (bindingIndex != lastIndex)
			{
				if (USceneComponent* movedComponent = PrimitiveBindings[lastIndex].Component.Get())
				{
					TArray<int32>& movedIndices = PrimitiveBindingsByComponent[movedComponent];
					movedIndices[movedIndices.Find(lastIndex)] = bindingIndex;
				}
			}

			PrimitiveBindings.RemoveAtSwap(bindingIndex);
			bindingIndices->RemoveAtSwap(i);
			continue;
		}

		RefreshPrimitiveTransform(binding);
	}

	if (bindingIndices->IsEmpty())
		PrimitiveBindingsByComponent.Remove(updatedComponent);
}

void AVAWorld::UnbindPrimitiveComponents()
{
	for (const FVAPrimitiveBinding& Binding : PrimitiveBindings)
	{
		if (USceneComponent* Component = Binding.Component.Get())
			Component->TransformUpdated.Remove(Binding.Handle);
	}

	PrimitiveBindings.Empty();
	PrimitiveBindingsByComponent.Empty();
}
