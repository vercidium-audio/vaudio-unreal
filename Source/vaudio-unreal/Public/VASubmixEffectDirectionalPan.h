#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundEffectSubmix.h"
#include "VASubmixEffectDirectionalPan.generated.h"

// Settings for FVASubmixEffectDirectionalPan, pushed once per tick from AVAWorld::ApplyGroupedEAXReverb() 
USTRUCT(BlueprintType)
struct FVASubmixEffectDirectionalPanSettings
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SubmixEffect|Preset", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Pan = 0.0f;
};

class FVASubmixEffectDirectionalPan : public FSoundEffectSubmix
{
public:
	VAUDIOUNREAL_API FVASubmixEffectDirectionalPan();

	// Called on an audio effect at initialization on main thread before audio processing begins.
	VAUDIOUNREAL_API virtual void Init(const FSoundEffectSubmixInitData& InitData) override;

	// Process the input block of audio. Called on audio thread.
	VAUDIOUNREAL_API virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override;

	// Called when an audio effect preset is changed.
	VAUDIOUNREAL_API virtual void OnPresetChanged() override;

private:
	Audio::TParams<FVASubmixEffectDirectionalPanSettings> PendingSettings;

	// Audio-thread copy of the current pan value, updated from PendingSettings at the start of
	// OnProcessAudio().
	float RenderThreadPan = 0.0f;
};

UCLASS(ClassGroup = AudioSourceEffect, meta = (BlueprintSpawnableComponent), MinimalAPI)
class UVASubmixEffectDirectionalPanPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:
	EFFECT_PRESET_METHODS(VASubmixEffectDirectionalPan)

	UFUNCTION(BlueprintCallable, Category = "Audio|Effects")
	VAUDIOUNREAL_API void SetSettings(const FVASubmixEffectDirectionalPanSettings& InSettings);

	// Convenience wrapper around SetSettings() for the common case of just updating Pan - matches
	// how AVAWorld::ApplyGroupedEAXReverb() only ever has a single float to push per tick.
	VAUDIOUNREAL_API void SetPan(float NewPan);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset, meta = (ShowOnlyInnerProperties))
	FVASubmixEffectDirectionalPanSettings Settings;
};
