#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "VAudioVisualisationComponent.generated.h"

struct VAVisualisationData;
class AVAudioEmitterBase;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Renders vaEmitterSetVisualisationCallback's ray-bounce results as fading diamond sprites.
// Add as a child component of any AVAudioEmitterBase-derived actor (AVAudioListener,
// AVAudioSource, AVAudioContinuous, AVAudioAmbientSource) - this component finds that owner,
// registers itself as the visualisation callback target, and consumes the resulting
// VAVisualisationData batches. Each callback invocation (every owner's
// VisualisationUpdateFrequency ms) writes one instance per ray bounce into an
// InstancedStaticMeshComponent; per-frame fading is entirely GPU-side (DiamondMaterial reads
// each instance's spawn time out of its per-instance custom data plus the CurrentTime/fade
// parameters below), so nothing here does per-instance work outside the callback itself.
//
// DiamondMaterial contract (the plugin ships no content, so this material must be authored by
// hand in the host project):
// - Unlit, Translucent blend mode, two-sided (or disable backface culling) - diamonds face an
//   arbitrary hit normal.
// - Scalar parameters: CurrentTime, FadeInMs, FadeOutMs, DurationMs, MaxOpacity.
// - Vector parameter: BaseColor (RGB).
// - Reads per-instance custom data index 0 (PerInstanceCustomData material node) as SpawnTime,
//   in the same GetWorld()->GetTimeSeconds() basis as CurrentTime.
// - Fragment logic:
//     elapsedMs = (CurrentTime - SpawnTime) * 1000
//     clip if elapsedMs < 0 or elapsedMs > DurationMs
//     fadeIn  = FadeInMs  > 0 ? saturate(elapsedMs / FadeInMs)  : 1
//     fadeOut = FadeOutMs > 0 ? saturate((DurationMs - elapsedMs) / FadeOutMs) : 1
//     EmissiveColor = BaseColor.rgb
//     Opacity = MaxOpacity * min(fadeIn, fadeOut)
UCLASS(ClassGroup = ("Vercidium Audio"), meta = (BlueprintSpawnableComponent), DisplayName = "VA Visualisation")
class VAUDIOUNREAL_API UVAudioVisualisationComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UVAudioVisualisationComponent();

	// Material used to render each diamond. Must be unlit + translucent and read the per-instance
	// custom data float (index 0) as elapsed-seconds-since-spawn - see the class comment above
	// for the full contract this material must implement.
	UPROPERTY(EditAnywhere, Category = "Vercidium Audio|Visualisation")
	TObjectPtr<UMaterialInterface> DiamondMaterial = nullptr;

	// Number of visualisation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 VisualisationRayCount = 0;

	// Number of bounces per visualisation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 VisualisationBounceCount = 0;

	// How often to cast visualisation rays (milliseconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "1"))
	int32 VisualisationUpdateFrequency = 500;

	// How long, in milliseconds, each diamond takes to fade in from transparent to Color's alpha
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 FadeInMilliseconds = 750;

	// How long, in milliseconds, each diamond takes to fade out to transparent at the end of its lifetime
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 FadeOutMilliseconds = 750;

	// How long, in milliseconds, each diamond remains visible in total, including FadeInMilliseconds and FadeOutMilliseconds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "1"))
	int32 DurationMilliseconds = 1500;

	// Colour of each diamond. Alpha is the maximum opacity reached once a diamond has fully faded in
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation")
	FLinearColor Color = FLinearColor(0.11f, 0.97f, 1.0f, 0.75f);

	// Radius of each diamond, in world units (cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0.01"))
	float Size = 10.0f;

	// How far, in world units, each diamond is pushed off the surface it landed on along the hit
	// normal. Increase if diamonds z-fight with nearby geometry
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0.0"))
	float NormalOffset = 2.0f;

	// Ray bounces further than this distance from the owning emitter are not rendered. 0 = no limit
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0.0"))
	float MaxDistance = 2000.0f;

	// Called from the VAEmitterVisualisationCallback trampoline with a batch of ray-bounce
	// results, always on the main thread during vaWorldUpdate() - safe to touch UObjects directly
	void OnVisualisationData(VAVisualisationData* data, int32 count);

#if WITH_EDITOR
	// Programmatically builds the fade graph documented above onto DiamondMaterial, so a
	// material-editor beginner never has to wire nodes by hand - sets Blend Mode/Shading Model
	// and creates+wires the parameter/custom-HLSL nodes described in the class comment. Requires
	// DiamondMaterial to be a plain UMaterial asset (not a Material Instance); warns and does
	// nothing otherwise. Safe to press repeatedly - only regenerates nodes it previously created.
	UFUNCTION(CallInEditor, Category = "Vercidium Audio|Visualisation")
	void GenerateFadeNodes();
#endif

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void DestroyComponent(bool bPromoteChildren) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> InstancedMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DiamondMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DiamondMaterialInstance = nullptr;

	AVAudioEmitterBase* OwnerEmitter = nullptr;

	int32 NextInstance = 0;

	bool bCallbackRegistered = false;

	// Diamonds must stay alive (fading) for DurationMilliseconds while new callbacks arrive every
	// VisualisationUpdateFrequency ms, so the ring buffer needs room for every batch still fading
	// at once - not just the latest one - or a new callback's writes stomp on still-visible
	// instances from a few callbacks ago. See OnVisualisationData.
	int32 GetRequiredInstanceCount() const;

	static UStaticMesh* BuildDiamondMesh();

	void CreateInstancedMesh();
	void ApplyMaterialParameters();
	void ApplyVisualisationSettings() const;
	void TeardownVisualisation();

	// Checks DiamondMaterial's blend mode, shading model, and required parameters against the
	// contract documented above, and raises a distinct on-screen DisplayWarning for each problem
	// found (cleared automatically once the problem is fixed and PostEditChangeProperty/BeginPlay
	// re-validates). Does not detect a parameter that exists but isn't wired to Opacity/EmissiveColor.
	void ValidateDiamondMaterial() const;

	void DisplayWarning(const TCHAR* fmt, ...) const;
	void ClearWarning() const;
	void DisplayMaterialWarning(uint32 offset, const TCHAR* fmt, ...) const;
	void ClearMaterialWarning(uint32 offset) const;
};
