#include "VAudioReverbConversion.h"

FSubmixEffectReverbSettings VAEAXReverbToSubmixSettings(const VAEAXReverb* EAX)
{
	FSubmixEffectReverbSettings settings;

	settings.DecayTime           = FMath::Clamp(EAX->decayTime,           0.1f,  20.0f);
	settings.DecayHFRatio        = FMath::Clamp(EAX->decayHFRatio,        0.1f,   2.0f);
	settings.Density             = FMath::Clamp(EAX->density,             0.0f,   1.0f);
	settings.Diffusion           = FMath::Clamp(EAX->diffusion,           0.0f,   1.0f);
	settings.Gain                = FMath::Clamp(EAX->gain,                0.0f,   1.0f);
	settings.ReflectionsGain     = FMath::Clamp(EAX->reflectionsGain,     0.0f,   3.16f);
	settings.ReflectionsDelay    = FMath::Clamp(EAX->reflectionsDelay,    0.0f,   0.3f);
	settings.LateGain            = FMath::Clamp(EAX->lateReverbGain,      0.0f,  10.0f);
	settings.LateDelay           = FMath::Clamp(EAX->lateReverbDelay,     0.0f,   0.1f);
	settings.AirAbsorptionGainHF = FMath::Clamp(EAX->airAbsorptionGainHF, 0.0f,   1.0f);

	// Unreal has no GainLF, so gainLF drives the overall WetLevel instead (EAX->gain is
	// effectively constant across presets and carries no useful information).
	// GainHF is then re-derived as a ratio of gainHF/gainLF so that, once WetLevel scales
	// the whole wet signal by gainLF, GainHF only applies the additional HF attenuation
	// beyond that - otherwise HF content would be attenuated twice (by WetLevel and GainHF).
	settings.WetLevel            = FMath::Clamp(EAX->gainLF, 0.0f, 1.0f);
	settings.GainHF              = EAX->gainLF > 0.0f ? FMath::Clamp(EAX->gainHF / EAX->gainLF, 0.0f, 1.0f) : 0.0f;
	settings.DryLevel            = 0.0f;

	return settings;
}
