#pragma once

#include "CoreMinimal.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"

extern "C" {
#include "vaudio.h"
}

// Converts the SDK's raytraced EAX reverb result to UE's submix reverb settings. Re-clamps every
// field even though EAX is already clamped, in case UE's EAX ranges change in a future engine version.
FSubmixEffectReverbSettings VAEAXReverbToSubmixSettings(const VAEAXReverb* EAX);
