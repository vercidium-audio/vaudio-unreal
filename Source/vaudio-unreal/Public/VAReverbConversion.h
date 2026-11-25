// Human-reviewed

#pragma once

#include "CoreMinimal.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"

extern "C" {
#include "vaudio.h"
}

// Converts the SDK's raytraced EAX reverb result to UE's submix reverb settings. Re-clamps every
// field even though EAX is already clamped, in case UE's EAX ranges change in a future engine version.
VAUDIOUNREAL_API FSubmixEffectReverbSettings VAEAXReverbToSubmixSettings(const VAEAXReverb* EAX);

// Also writes the preset's Settings property, which it copies when its first effect instance is created (e.g. once the submix is registered), so a late instance doesn't start from stale settings
VAUDIOUNREAL_API void VASetReverbPresetSettings(USubmixEffectReverbPreset* preset, const FSubmixEffectReverbSettings& settings);

// A reverb preset with a wet level of 0, so anything sent to it before its first EAX is silent rather than the engine's default reverb
VAUDIOUNREAL_API USubmixEffectReverbPreset* VACreateSilentReverbPreset(UObject* outer);
