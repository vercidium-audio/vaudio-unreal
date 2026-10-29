#include "VAWorldBoundsVisualizer.h"
#include "Editor.h"
#include "SceneManagement.h"
#include "VAWorld.h"

IMPLEMENT_HIT_PROXY(HVAWorldBoundsFaceProxy, HComponentVisProxy);

// BoundsSize's ClampMin
static constexpr double MinimumBoundsSize = 0.0001;

static int32 GetFaceAxis(EVABoundsFace face)
{
	return static_cast<int32>(face) % 3;
}

static bool IsMinFace(EVABoundsFace face)
{
	return static_cast<int32>(face) >= 3;
}

FVector FVAWorldBoundsVisualizer::GetFaceCentre(const AVAWorld* world, EVABoundsFace face)
{
	const FVector min = world->GetBoundsPosition();
	const int32 axis = GetFaceAxis(face);

	FVector centre = min + world->BoundsSize * 0.5;
	centre[axis] = IsMinFace(face) ? min[axis] : min[axis] + world->BoundsSize[axis];
	return centre;
}

void FVAWorldBoundsVisualizer::MoveFace(AVAWorld* world, EVABoundsFace face, double delta)
{
	const int32 axis = GetFaceAxis(face);
	FVector size = world->BoundsSize;

	world->Modify();

	if (IsMinFace(face))
	{
		delta = FMath::Min(delta, size[axis] - MinimumBoundsSize);
		size[axis] -= delta;

		FVector location = world->GetActorLocation();
		location[axis] += delta;

		world->GetRootComponent()->Modify();
		world->SetActorLocation(location);
	}
	else
	{
		size[axis] = FMath::Max(size[axis] + delta, MinimumBoundsSize);
	}

	world->SetBoundsSize(size);
}

void FVAWorldBoundsVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const AVAWorld* world = Component ? Cast<AVAWorld>(Component->GetOwner()) : nullptr;

	// During play the bounds can sit away from the actor (BoundsUpdateDistance, BoundsFollow), so they're only edited in the editor world
	if (!world || !Component->GetWorld() || Component->GetWorld()->IsGameWorld())
		return;

	const bool bEdited = GetEditedComponent() == Component;

	for (int32 i = 0; i < 6; i++)
	{
		const EVABoundsFace face = static_cast<EVABoundsFace>(i);
		const FColor color = bEdited && SelectedFace == face ? FColor::White : world->BoundsColor;

		PDI->SetHitProxy(new HVAWorldBoundsFaceProxy(Component, face));
		PDI->DrawPoint(GetFaceCentre(world, face), color, 12.0f, SDPG_Foreground);
	}

	PDI->SetHitProxy(nullptr);
}

bool FVAWorldBoundsVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	if (!VisProxy || !VisProxy->IsA(HVAWorldBoundsFaceProxy::StaticGetType()))
		return false;

	const UVAWorldBoundsComponent* bounds = Cast<const UVAWorldBoundsComponent>(VisProxy->Component.Get());

	if (!bounds)
		return false;

	SelectFace(bounds, static_cast<HVAWorldBoundsFaceProxy*>(VisProxy)->Face);
	return SelectedFace.IsSet();
}

void FVAWorldBoundsVisualizer::SelectFace(const UVAWorldBoundsComponent* bounds, EVABoundsFace face)
{
	BoundsPath = FComponentPropertyPath(bounds);

	if (GetEditedWorld())
		SelectedFace = face;
	else
		EndEditing();
}

void FVAWorldBoundsVisualizer::EndEditing()
{
	BoundsPath.Reset();
	SelectedFace.Reset();
}

AVAWorld* FVAWorldBoundsVisualizer::GetEditedWorld() const
{
	return BoundsPath.IsValid() ? Cast<AVAWorld>(BoundsPath.GetParentOwningActor()) : nullptr;
}

UActorComponent* FVAWorldBoundsVisualizer::GetEditedComponent() const
{
	return BoundsPath.GetComponent();
}

bool FVAWorldBoundsVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	AVAWorld* world = GetEditedWorld();

	if (!world || !SelectedFace)
		return false;

	OutLocation = GetFaceCentre(world, *SelectedFace);
	return true;
}

bool FVAWorldBoundsVisualizer::HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale)
{
	AVAWorld* world = GetEditedWorld();

	if (!world || !SelectedFace)
		return false;

	// The bounds are axis-aligned, so only the part of the drag along the face's own axis counts. Rotation and scale do nothing
	const double delta = DeltaTranslate[GetFaceAxis(*SelectedFace)];

	if (delta != 0.0)
	{
		MoveFace(world, *SelectedFace, delta);
		world->PostEditMove(false);

		if (GEditor)
			GEditor->RedrawLevelEditingViewports(true);
	}

	return true;
}

void FVAWorldBoundsVisualizer::TrackingStopped(FEditorViewportClient* InViewportClient, bool bInDidMove)
{
	if (!bInDidMove)
		return;

	if (AVAWorld* world = GetEditedWorld())
		world->PostEditMove(true);
}
