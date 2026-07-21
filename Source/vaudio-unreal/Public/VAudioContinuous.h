#pragma once

#include "CoreMinimal.h"
#include "VAudioEmitterBase.h"
#include "VAudioContinuous.generated.h"

struct VALowPassFilter;

// A raytracing target that plays no sound of its own - the listener casts occlusion/permeation
// rays towards it every frame, producing an up-to-date muffle/EAX result. Attach this to an actor
// (e.g. an enemy) so that many one-shot AVAudioRelativeSources on that actor (footsteps, barks)
// can reuse a single raytrace via GetGroupedEAXIndex()/ApplySourceFilter() instead of each one
// raytracing independently.
//
// AVAudioSource is-a AVAudioContinuous that also plays a sound. Modelling it as a subclass (rather
// than a sibling sharing some intermediate base) means the raytracing-target plumbing - occlusion/
// permeation UPROPERTYs, grouped-EAX submix routing, target-filter application - is written once
// here and AVAudioSource only adds what's unique to owning a SourceSound.
UCLASS(DisplayName = "VAudio Continuous")
class VAUDIOUNREAL_API AVAudioContinuous : public AVAudioEmitterBase
{
	GENERATED_BODY()

public:
	AVAudioContinuous();

protected:
	virtual void InitializeTypeSpecific() override;
	virtual void DeinitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	// When true, this emitter's EAX reverb is blended into the world's grouped EAX submixes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bAffectsGroupedEAX = true;

	// The loudest linear volume (0-1) this emitter's dry source will ever be played at by the consuming application.
	// Used to estimate how long the emitter's reverb tail stays audible - a quieter source's reverb tail finishes sooner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (EditCondition = "bAffectsGroupedEAX", ClampMin = "0.0", ClampMax = "1.0"))
	float MaxVolume = 1.0f;

	// --- Reverb ---

	// Number of reverb rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0"))
	int32 ReverbRayCount = 0;

	// Number of bounces per reverb ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0"))
	int32 ReverbBounceCount = 0;

	// The percentage of returning energy required for reverb to be at maximum volume
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReverbEnergyCap = 0.2f;

	// How long (in milliseconds) the echogram records data for. Returning reverb rays after this period will be ignored
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "1"))
	int32 MaxEchogramTime = 5000;

	// The length (in milliseconds) of each entry in the echogram
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "1"))
	int32 EchogramGranularity = 200;

	// --- Muffling ---

	// Number of occlusion rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0"))
	int32 OcclusionRayCount = 0;

	// Maximum number of bounces per occlusion ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0"))
	int32 OcclusionBounceCount = 0;

	// Percentage of occlusion energy required for the emitter to be at full volume. Defaults to 15% of the other emitter's OcclusionRayCount
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OcclusionEnergyCap = 0.15f;

	// Number of permeation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0"))
	int32 PermeationRayCount = 0;

	// Number of bounces per permeation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0"))
	int32 PermeationBounceCount = 0;

	// Percentage of permeation energy required for the emitter to be at full volume. Defaults to 15% of the other emitter's PermeationRayCount * PermeationBounceCount
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PermeationEnergyCap = 0.15f;

	// Energy threshold below which permeation rays are cancelled to prevent unnecessary traversal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.01"))
	float MinimumPermeationEnergy = 0.01f;

	// --- Ambient ---

	// Number of ambient occlusion rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0"))
	int32 AmbientOcclusionRayCount = 0;

	// Maximum number of bounces per ambient occlusion ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0"))
	int32 AmbientOcclusionBounceCount = 0;

	// Percentage of ambient occlusion energy required for the emitter to be at full volume.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AmbientOcclusionEnergyCap = 0.5f;

	// Number of ambient permeation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0"))
	int32 AmbientPermeationRayCount = 0;

	// Maximum number of bounces per ambient permeation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0"))
	int32 AmbientPermeationBounceCount = 0;

	// Percentage of ambient permeation energy required for the emitter to be at full volume
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AmbientPermeationEnergyCap = 0.5f;

	// --- Refresh ---

	// Number of trails rebuilt from scratch each frame to prevent staleness when the emitter moves
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Refresh", meta = (ClampMin = "0"))
	int32 RefreshRayCount = 0;

	// A ray trail will be re-created if an old ray bounce position is too far away from the new ray bounce position. This setting controls the allowed distance between old and new ray bounce positions
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Refresh", meta = (ClampMin = "0.0"))
	float RefreshDistanceThreshold = 1.0f;

	// --- Visualisation ---

	// Number of visualisation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 VisualisationRayCount = 0;

	// Number of bounces per visualisation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 VisualisationBounceCount = 0;

	// How often to cast visualisation rays (milliseconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "1"))
	int32 VisualisationUpdateFrequency = 500;

	// --- Advanced ---

	// User-defined integer tag for categorising emitters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced")
	int32 EmitterType = 0;

	// Whether this emitter's position is clamped to world bounds, to prevent going out of bounds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced")
	bool bClampPosition = false;

	// Seed used to randomise scattering vectors
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced")
	int32 ScatteringSeed = 0;

	// --- Runtime access ---

	// The world's grouped-EAX submix slot this emitter is currently blended into, or -1 if not
	// yet assigned/AffectsGroupedEAX is false. Used by AVAudioRelativeSource (item 4) to route
	// footsteps/barks attached to this emitter through the same submix.
	int32 GetGroupedEAXIndex() const { return CurrentGroupedEAXIndex; }

	// The listener's most recently raytraced muffling result for this emitter, or nullptr if the
	// listener hasn't raytraced it yet. Used by AVAudioRelativeSource (item 4) to reuse this
	// emitter's muffling instead of raytracing again for every attached one-shot sound.
	VALowPassFilter* GetMufflingResult() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	int32 CurrentGroupedEAXIndex = -1;

	void ApplyRayPropertiesToEmitter();
	void UpdateGroupedEAXIndex();
};
