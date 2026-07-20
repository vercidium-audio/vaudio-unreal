#pragma once

#include "CoreMinimal.h"
#include "VAudioEmitterBase.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Sound/SoundSubmix.h"
#include "VAudioListener.generated.h"

// Place exactly one of these in the level - the world's single reference point for directional
// reverb and ambience. Every other raytracing-target actor (AVAudioSource, AVAudioContinuous)
// is added to TargetEmitters so this listener raytraces towards it.
UCLASS(DisplayName = "VAudio Listener")
class VAUDIOUNREAL_API AVAudioListener : public AVAudioEmitterBase
{
	GENERATED_BODY()

public:
	AVAudioListener();

protected:
	virtual void InitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	// Automatically move this emitter (and the VA listener position) to the first player controller's camera every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener")
	bool bAutoFollowCamera = true;

	// This submix applies reverb to sounds created by this listener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener")
	USoundSubmix* ListenerReverbSubmix = nullptr;

	// Target emitters that this listener will cast occlusion and permeation rays towards.
	// Holds both AVAudioSource and AVAudioContinuous actors - both are raytracing targets.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener")
	TArray<AVAudioEmitterBase*> TargetEmitters;

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

	// The lower bound of the relative reverb blend range. This affects the directional reverb that is heard by this listener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RelativeReverbInnerThreshold = 0.6f;

	// The upper bound of the relative reverb blend range. This affects the directional reverb that is heard by this listener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RelativeReverbOuterThreshold = 0.8f;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
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

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	bool bTargetsRegistered = false;
	TSet<AVAudioEmitterBase*> RegisteredTargets;

	// Transient: created via NewObject() in BeginPlay/TryInitializeEmitter and torn down in
	// EndPlay. Must never be serialized - saving the level while this is set (e.g. mid-PIE, or
	// after a crash skips EndPlay) writes it as a real export that doesn't round-trip through a
	// reload and corrupts the package (see FLinkerLoad::CreateExport crash).
	UPROPERTY(Transient)
	USubmixEffectReverbPreset* ListenerReverbPreset = nullptr;

	void ApplyListenerReverb();
	void ApplyGroupedEAXReverb();
};
