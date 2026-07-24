#include "VAudioWorld.h"
#include "VAConstants.h"
#include "VARawLog.h"

#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"

extern "C" {
#include "vaudio.h"
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

void AVAudioWorld::DestroyPrimitives()
{
	UnbindPrimitiveComponents();

	for (VAMeshPrimitive*    P : MeshPrimitives)    { vaWorldRemovePrimitive_(GetVAWorld(), P); vaMeshPrimitiveDestroy(P); }
	for (VACapsulePrimitive* P : CapsulePrimitives) { vaWorldRemovePrimitive_(GetVAWorld(), P); vaCapsulePrimitiveDestroy(P); }
	for (VASpherePrimitive*  P : SpherePrimitives)  { vaWorldRemovePrimitive_(GetVAWorld(), P); vaSpherePrimitiveDestroy(P); }
	for (VAPrismPrimitive*   P : PrismPrimitives)   { vaWorldRemovePrimitive_(GetVAWorld(), P); vaPrismPrimitiveDestroy(P); }

	MeshPrimitives.Empty();
	CapsulePrimitives.Empty();
	SpherePrimitives.Empty();
	PrismPrimitives.Empty();
}

// ---------------------------------------------------------------------------
// Move tracking — keeps primitive transforms in sync with their owning components
// ---------------------------------------------------------------------------

void AVAudioWorld::BindPrimitiveToComponent(void* Primitive, EVAudioPrimitiveKind Kind, USceneComponent* Component,
	const FTransform& LocalOffset, const FVector& LocalExtent)
{
	// Null if this primitive's Actor has no scene component at all (shouldn't happen - every
	// UShapeComponent/UStaticMeshComponent passed in here is itself a USceneComponent), but guard
	// anyway since a bad bind here would silently leave a primitive frozen at its creation-time
	// transform with no on-screen indication.
	if (!Component)
	{
		VALog(L"BindPrimitiveToComponent() called with a null Component - this primitive will not track its actor's movement.");
		return;
	}

	FVAudioPrimitiveBinding& Binding = PrimitiveBindings.AddDefaulted_GetRef();
	Binding.Component    = Component;
	Binding.Primitive    = Primitive;
	Binding.Kind         = Kind;
	Binding.LocalOffset  = LocalOffset;
	Binding.LocalExtent  = LocalExtent;
	Binding.Handle       = Component->TransformUpdated.AddUObject(this, &AVAudioWorld::OnPrimitiveComponentMoved);
}

void AVAudioWorld::RefreshPrimitiveTransform(const FVAudioPrimitiveBinding& Binding)
{
	USceneComponent* Component = Binding.Component.Get();

	// Null if the owning actor/component was destroyed without this world's EndPlay running yet
	// (e.g. mid-PIE actor deletion) - OnPrimitiveComponentMoved() below already drops bindings
	// whose component has gone stale, so this should be rare, not a normal per-call case.
	if (!Component)
		return;

	FTransform WorldTransform = Binding.LocalOffset * Component->GetComponentTransform();

	switch (Binding.Kind)
	{
	case EVAudioPrimitiveKind::Mesh:
	{
		VAMatrix Mat = MakeScaleRotTransMatrix(WorldTransform);
		vaMeshPrimitiveSetTransform(static_cast<VAMeshPrimitive*>(Binding.Primitive), &Mat);
		break;
	}
	case EVAudioPrimitiveKind::Capsule:
	{
		UCapsuleComponent* Capsule = CastChecked<UCapsuleComponent>(Component);
		VAMatrix Mat = MakeRotTransMatrix(WorldTransform);
		VACapsulePrimitive* Cap = static_cast<VACapsulePrimitive*>(Binding.Primitive);
		vaCapsulePrimitiveSetRadius(Cap, Capsule->GetScaledCapsuleRadius());
		vaCapsulePrimitiveSetLength(Cap, Capsule->GetScaledCapsuleHalfHeight_WithoutHemisphere() * 2.0f);
		vaCapsulePrimitiveSetTransform(Cap, &Mat);
		break;
	}
	case EVAudioPrimitiveKind::Sphere:
	{
		USphereComponent* Sphere = CastChecked<USphereComponent>(Component);
		FVector Center = WorldTransform.GetTranslation();
		VASpherePrimitive* Sp = static_cast<VASpherePrimitive*>(Binding.Primitive);
		vaSpherePrimitiveSetCenterUnreal(Sp, Center);
		vaSpherePrimitiveSetRadius(Sp, Sphere->GetScaledSphereRadius());
		break;
	}
	case EVAudioPrimitiveKind::Prism:
	{
		UBoxComponent* Box = CastChecked<UBoxComponent>(Component);
		VAMatrix Mat = MakeRotTransMatrix(WorldTransform);
		FVector Extent = Box->GetScaledBoxExtent();
		VAPrismPrimitive* Prism = static_cast<VAPrismPrimitive*>(Binding.Primitive);
		vaPrismPrimitiveSetSize(Prism, vaVectorCreate(Extent.X * 2.0f, Extent.Y * 2.0f, Extent.Z * 2.0f));
		vaPrismPrimitiveSetTransform(Prism, &Mat);
		break;
	}
	case EVAudioPrimitiveKind::CapsuleFromMesh:
	{
		// Matches the FMath::Max(Scale.X, Scale.Y)/Scale.Z convention ScanAndAddPrimitives
		// originally used for FKSphylElem - see the comment on EVAudioPrimitiveKind.
		FVector Scale = Component->GetComponentTransform().GetScale3D();
		VAMatrix Mat = MakeRotTransMatrix(WorldTransform);
		VACapsulePrimitive* Cap = static_cast<VACapsulePrimitive*>(Binding.Primitive);
		vaCapsulePrimitiveSetRadius(Cap, Binding.LocalExtent.X * FMath::Max(Scale.X, Scale.Y));
		vaCapsulePrimitiveSetLength(Cap, Binding.LocalExtent.Z * Scale.Z);
		vaCapsulePrimitiveSetTransform(Cap, &Mat);
		break;
	}
	case EVAudioPrimitiveKind::SphereFromMesh:
	{
		FVector Scale = Component->GetComponentTransform().GetScale3D();
		FVector Center = WorldTransform.GetTranslation();
		VASpherePrimitive* Sp = static_cast<VASpherePrimitive*>(Binding.Primitive);
		vaSpherePrimitiveSetCenterUnreal(Sp, Center);
		vaSpherePrimitiveSetRadius(Sp, Binding.LocalExtent.X * Scale.GetAbsMax());
		break;
	}
	case EVAudioPrimitiveKind::PrismFromMesh:
	{
		FVector Scale = Component->GetComponentTransform().GetScale3D();
		VAMatrix Mat = MakeRotTransMatrix(WorldTransform);
		VAPrismPrimitive* Prism = static_cast<VAPrismPrimitive*>(Binding.Primitive);
		vaPrismPrimitiveSetSize(Prism, vaVectorCreate(
			Binding.LocalExtent.X * Scale.X, Binding.LocalExtent.Y * Scale.Y, Binding.LocalExtent.Z * Scale.Z));
		vaPrismPrimitiveSetTransform(Prism, &Mat);
		break;
	}
	}
}

void AVAudioWorld::OnPrimitiveComponentMoved(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	for (int32 i = PrimitiveBindings.Num() - 1; i >= 0; --i)
	{
		FVAudioPrimitiveBinding& Binding = PrimitiveBindings[i];

		// Component was destroyed without going through DestroyPrimitives()/UnbindPrimitiveComponents()
		// first (e.g. the owning actor was deleted mid-PIE while this world is still alive) - drop
		// the now-useless binding instead of leaving its primitive frozen forever.
		if (!Binding.Component.IsValid())
		{
			PrimitiveBindings.RemoveAtSwap(i);
			continue;
		}

		if (Binding.Component.Get() == UpdatedComponent)
			RefreshPrimitiveTransform(Binding);
	}
}

void AVAudioWorld::UnbindPrimitiveComponents()
{
	for (const FVAudioPrimitiveBinding& Binding : PrimitiveBindings)
	{
		if (USceneComponent* Component = Binding.Component.Get())
			Component->TransformUpdated.Remove(Binding.Handle);
	}

	PrimitiveBindings.Empty();
}
