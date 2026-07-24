#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundEffectSubmix.h"
#include "VAudioSubmixEffectDirectionalPan.generated.h"

// Settings for FSubmixEffectDirectionalPan - currently just the pan value itself, pushed once per
// tick from AVAudioWorld::ApplyGroupedEAXReverb() (see directional_reverb_plan.md).
USTRUCT(BlueprintType)
struct FSubmixEffectDirectionalPanSettings
{
	GENERATED_USTRUCT_BODY()

	// Pan strength/direction in [-1, 1]. Not normalized - magnitude is meaningful (0 = centered,
	// +-1 = full left/right), matching vaEAXReverbGetRelativeDirection()'s magnitude-as-strength
	// convention. See directional_reverb_plan.md's "Pan model" section.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SubmixEffect|Preset", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Pan = 0.0f;
};

// Applies equal-power left/right panning to a submix's rendered audio, intended to sit after a
// USubmixEffectReverbPreset in a grouped-EAX submix's effect chain so the wet reverb tail itself
// is panned toward vaEAXReverbGetRelativeDirection() (see directional_reverb_plan.md). Passes audio
// through unchanged for anything other than mono/stereo, rather than attempting to reinterpret an
// unknown channel layout.
class FSubmixEffectDirectionalPan : public FSoundEffectSubmix
{
public:
	VAUDIOUNREAL_API FSubmixEffectDirectionalPan();

	// Called on an audio effect at initialization on main thread before audio processing begins.
	VAUDIOUNREAL_API virtual void Init(const FSoundEffectSubmixInitData& InitData) override;

	// Process the input block of audio. Called on audio thread.
	VAUDIOUNREAL_API virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override;

	// Called when an audio effect preset is changed.
	VAUDIOUNREAL_API virtual void OnPresetChanged() override;

private:
	// Lock-free game-thread -> audio-thread handoff, matching FSubmixEffectSubmixEQ's PendingSettings
	// pattern (AudioMixerSubmixEffectEQ.h) - OnPresetChanged() runs on the game thread, OnProcessAudio()
	// on the audio render thread.
	Audio::TParams<FSubmixEffectDirectionalPanSettings> PendingSettings;

	// Audio-thread copy of the current pan value, updated from PendingSettings at the start of
	// OnProcessAudio().
	float RenderThreadPan = 0.0f;
};

UCLASS(ClassGroup = AudioSourceEffect, meta = (BlueprintSpawnableComponent), MinimalAPI)
class USubmixEffectDirectionalPanPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:
	EFFECT_PRESET_METHODS(SubmixEffectDirectionalPan)

	UFUNCTION(BlueprintCallable, Category = "Audio|Effects")
	VAUDIOUNREAL_API void SetSettings(const FSubmixEffectDirectionalPanSettings& InSettings);

	// Convenience wrapper around SetSettings() for the common case of just updating Pan - matches
	// how AVAudioWorld::ApplyGroupedEAXReverb() only ever has a single float to push per tick.
	VAUDIOUNREAL_API void SetPan(float NewPan);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset, meta = (ShowOnlyInnerProperties))
	FSubmixEffectDirectionalPanSettings Settings;
};
