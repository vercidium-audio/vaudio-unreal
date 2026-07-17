#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "Components/BillboardComponent.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectFilter.h"
#include "VAudioEmitter.generated.h"

struct VAEmitter;
class AVAudioWorld;

// Place this actor in the level for each audio source (or the player listener).
// Assign the VAudioWorld reference and tune per-emitter ray settings in the Details panel.
UCLASS(DisplayName = "VA Audio Emitter")
class VAUDIOUNREAL_API AVAudioEmitter : public AActor
{
	GENERATED_BODY()

public:
	AVAudioEmitter();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// The world that this emitter belongs to
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	AVAudioWorld* AudioWorld = nullptr;

	// --- Listener ---
	
	// Every world must have a main listener emitter. Directional reverb and ambience volume are relative to this emitter
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener")
	bool bIsMainListener = false;

	// Automatically move this emitter (and the VA listener position) to the first player controller's camera every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener", meta = (EditCondition = "bIsMainListener"))
	bool bAutoFollowCamera = true;

	// This submix applies reverb to sounds created by this listener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener", meta = (EditCondition = "bIsMainListener"))
	USoundSubmix* ListenerReverbSubmix = nullptr;

	// WIP - this will likely be replaced in future, as it only supports one ambient sound.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener", meta = (EditCondition = "bIsMainListener"))
	USoundBase* AmbientSound = nullptr;

	// Whether the ambient sound has reverb
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener", meta = (EditCondition = "bIsMainListener"))
	bool bAmbientThroughReverb = true;

	// Target emitters that this emitter will cast occlusion and permeation rays towards
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener", meta = (EditCondition = "bIsMainListener"))
	TArray<AVAudioEmitter*> TargetEmitters;

	// --- Source ---

	// The sound file to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (EditCondition = "!bIsMainListener"))
	USoundBase* SourceSound = nullptr;

	// When true, this emitter's EAX reverb is blended into the world's grouped EAX submixes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (EditCondition = "!bIsMainListener"))
	bool bAffectsGroupedEAX = true;

	// Whether the sound should loop
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (EditCondition = "!bIsMainListener"))
	bool bLooping = true;

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

	// The loudest linear volume (0-1) this emitter's dry source will ever be played at by the consuming application.
	// Used to estimate how long the emitter's reverb tail stays audible - a quieter source's reverb tail finishes sooner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (EditCondition = "bAffectsGroupedEAX", ClampMin = "0.0", ClampMax = "1.0"))
	float MaxVolume = 1.0f;

	// How long (in milliseconds) the echogram records data for. Returning reverb rays after this period will be ignored
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "1"))
	int32 MaxEchogramTime = 5000;

	// The length (in milliseconds) of each entry in the echogram
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "1"))
	int32 EchogramGranularity = 200;

	// The lower bound of the relative reverb blend range. This affects the directional reverb that is heard by this emitter
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (EditCondition = "bIsMainListener", ClampMin = "0.0", ClampMax = "1.0"))
	float RelativeReverbInnerThreshold = 0.6f;

	// The upper bound of the relative reverb blend range. This affects the directional reverb that is heard by this emitter
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (EditCondition = "bIsMainListener", ClampMin = "0.0", ClampMax = "1.0"))
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
	float MinimumPermeationEnergy = 0.0f;

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

	VAEmitter* GetVAEmitter() const { return Emitter; }
	void ApplySourceFilter(float GainLF, float GainHF);
	void SetDryOutputEnabled(bool bEnabled);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	VAEmitter* Emitter = nullptr;
	int32 CurrentGroupedEAXIndex = -1;
	bool bTargetsRegistered = false;
	TSet<AVAudioEmitter*> RegisteredTargets;
	bool bCurrentDryEnabled = true;

	// True once TryInitializeEmitter() has built the LPF chain for SourceSound but hasn't spawned
	// SourceAudioComponent yet - spawn is deferred until the main listener has raytraced this
	// emitter at least once, so the source doesn't start clear and then pop to muffled (see Tick()).
	bool bSourcePendingSpawn = false;

	// Transient: created via NewObject()/SpawnSound* in BeginPlay/TryInitializeEmitter and
	// torn down in EndPlay. Must never be serialized — saving the level while these are set
	// (e.g. mid-PIE, or after a crash skips EndPlay) writes them as real exports that don't
	// round-trip through a reload and corrupt the package (see FLinkerLoad::CreateExport crash).
	UPROPERTY(Transient)
	UAudioComponent* AmbientAudioComponent = nullptr;

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

	UPROPERTY(Transient)
	USubmixEffectReverbPreset* ListenerReverbPreset = nullptr;

	UPROPERTY(Transient)
	USourceEffectFilterPreset* SourceLPFPreset = nullptr;

	UPROPERTY(Transient)
	USoundEffectSourcePresetChain* SourceEffectChain = nullptr;

	void ApplyListenerReverb();
	void ApplyGroupedEAXReverb();
	void ApplyAmbientFilter();
	void UpdateSourceSubmix();
	void TrySpawnSourceSound();

	// Creates the VA emitter and wires up audio components. Safe to call repeatedly:
	// no-ops (returns true) if already initialized, returns false if AudioWorld's
	// VAWorld isn't ready yet (actor BeginPlay order isn't guaranteed).
	bool TryInitializeEmitter();
};
