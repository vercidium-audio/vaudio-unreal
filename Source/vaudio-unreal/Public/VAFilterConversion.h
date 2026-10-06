#pragma once

#include "CoreMinimal.h"
#include "VAFilterConversion.generated.h"

class UAudioComponent;
class USourceEffectFilterPreset;
class USoundEffectSourcePresetChain;

constexpr float VA_MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
constexpr float VA_MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;

// Maps an SDK gainHF to a low-pass cutoff on a log-frequency scale, so equal steps in gainHF sound like equal steps in muffling
inline float VAGainHFToCutoffFrequency(float GainHF)
{
	float t = FMath::Clamp(GainHF, 0.0f, 1.0f);
	return FMath::Exp(FMath::Lerp(FMath::Loge(VA_MIN_LOW_PASS_CUTOFF_FREQUENCY), FMath::Loge(VA_MAX_LOW_PASS_CUTOFF_FREQUENCY), t));
}

// The muffling filter shared by every source type: gainLF sets the volume, gainHF sets the cutoff of a per-source LPF in the source effect chain. UAudioComponent::SetLowPassFilterFrequency isn't used, since Unreal's own occlusion drives that filter too
USTRUCT()
struct VAUDIOUNREAL_API FVASourceFilter
{
	GENERATED_BODY()

	void Initialize(UObject* Outer);

	// Must be called before Play(), otherwise the effect chain is never applied
	void Attach(UAudioComponent* Component) const;

	// The result is kept even when Component is null (not playing yet, or no audio device), and is pushed by Attach
	void Apply(UAudioComponent* Component, float GainLF, float GainHF);

	float GetVolume() const { return Volume; }
	float GetCutoffFrequency() const { return CutoffFrequency; }

private:
	UPROPERTY(Transient)
	TObjectPtr<USourceEffectFilterPreset> Preset = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USoundEffectSourcePresetChain> Chain = nullptr;

	float Volume = 1.0f;
	float CutoffFrequency = VA_MAX_LOW_PASS_CUTOFF_FREQUENCY;

	void PushCutoff() const;
};
