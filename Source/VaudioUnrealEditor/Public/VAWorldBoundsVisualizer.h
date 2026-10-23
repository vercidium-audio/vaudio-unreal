#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class AVAWorld;
class UVAWorldBoundsComponent;

// A face of the world bounds box. The axis is the index % 3 (X, Y, Z), and the first three are the max faces
enum class EVABoundsFace : uint8
{
	MaxX,
	MaxY,
	MaxZ,
	MinX,
	MinY,
	MinZ,
};

struct HVAWorldBoundsFaceProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY(VAUDIOUNREALEDITOR_API);

	HVAWorldBoundsFaceProxy(const UActorComponent* component, EVABoundsFace face)
		: HComponentVisProxy(component, HPP_Foreground)
		, Face(face)
	{}

	EVABoundsFace Face;
};

// Drag handles on the faces of a selected VAWorld's bounds box, like Godot's VAWorldGizmoPlugin. Clicking a handle moves the translate widget to it, and dragging moves that face along its own axis while the opposite face stays put
class VAUDIOUNREALEDITOR_API FVAWorldBoundsVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	virtual void EndEditing() override;
	virtual bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
	virtual bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale) override;
	virtual void TrackingStopped(FEditorViewportClient* InViewportClient, bool bInDidMove) override;
	virtual UActorComponent* GetEditedComponent() const override;

	// Selects a face's handle, as clicking it does
	void SelectFace(const UVAWorldBoundsComponent* bounds, EVABoundsFace face);

	TOptional<EVABoundsFace> GetSelectedFace() const { return SelectedFace; }

	// Centre of one face of the world's bounds box
	static FVector GetFaceCentre(const AVAWorld* world, EVABoundsFace face);

	// Moves one face along its axis by delta world units. A max face only changes BoundsSize, a min face also moves the actor so the max face stays put. A face can't cross the opposite one. Call inside a transaction to make it undoable
	static void MoveFace(AVAWorld* world, EVABoundsFace face, double delta);

private:
	AVAWorld* GetEditedWorld() const;

	FComponentPropertyPath BoundsPath;
	TOptional<EVABoundsFace> SelectedFace;
};
