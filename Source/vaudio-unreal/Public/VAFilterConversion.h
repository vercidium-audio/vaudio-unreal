#pragma once

#include "CoreMinimal.h"
#include "VAFilterConversion.generated.h"

class UAudioComponent;

constexpr float VA_MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
constexpr float VA_MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;

// Maps an SDK gainHF to a low-pass cutoff on a log-frequency scale, so equal steps in gainHF sound like equal steps in muffling
inline float VAGainHFToCutoffFrequency(float GainHF)
{
	float t = FMath::Clamp(GainHF, 0.0f, 1.0f);
	return FMath::Exp(FMath::Lerp(FMath::Loge(VA_MIN_LOW_PASS_CUTOFF_FREQUENCY), FMath::Loge(VA_MAX_LOW_PASS_CUTOFF_FREQUENCY), t));
}

// Pre-attenuation submix sends are tapped after the source's volume, so dividing by the volume sends the unmuffled signal to reverb, like Godot's fullReverb. Unreal clamps send levels to [0, 1], so this undershoots when Volume < SendLevel
inline float VACompensateReverbSendLevel(float SendLevel, float Volume)
{
	if (SendLevel <= 0.0f)
		return 0.0f;

	return Volume <= SendLevel ? 1.0f : SendLevel / Volume;
}

// The muffling filter shared by every source type: gainLF sets the volume, gainHF sets the component's low-pass filter. Unlike the volume and the source effect chain, that filter is applied after the pre-attenuation reverb send, so it only muffles the dry path. Unreal's own occlusion/attenuation LPF is combined with it, and the lowest cutoff wins
USTRUCT()
struct VAUDIOUNREAL_API FVASourceFilter
{
	GENERATED_BODY()

	// Must be called before Play(), so the first block is already filtered
	void Attach(UAudioComponent* Component) const;

	// The result is kept even when Component is null (not playing yet, or no audio device), and is pushed by Attach
	void Apply(UAudioComponent* Component, float GainLF, float GainHF);

	float CompensateReverbSendLevel(float SendLevel) const { return VACompensateReverbSendLevel(SendLevel, Volume); }

	float GetVolume() const { return Volume; }
	float GetCutoffFrequency() const { return CutoffFrequency; }

private:
	float Volume = 1.0f;
	float CutoffFrequency = VA_MAX_LOW_PASS_CUTOFF_FREQUENCY;
};
