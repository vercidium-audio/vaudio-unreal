#include "VAudioReverbConversion.h"

FSubmixEffectReverbSettings VAEAXReverbToSubmixSettings(const VAEAXReverb* EAX)
{
	FSubmixEffectReverbSettings settings;

	settings.DecayTime           = FMath::Clamp(EAX->decayTime,           0.1f,  20.0f);
	settings.DecayHFRatio        = FMath::Clamp(EAX->decayHFRatio,        0.1f,   2.0f);
	settings.Density             = FMath::Clamp(EAX->density,             0.0f,   1.0f);
	settings.Diffusion           = FMath::Clamp(EAX->diffusion,           0.0f,   1.0f);
	settings.Gain                = FMath::Clamp(EAX->gain,                0.0f,   1.0f);
	settings.GainHF              = FMath::Clamp(EAX->gainHF,              0.0f,   1.0f);
	settings.ReflectionsGain     = FMath::Clamp(EAX->reflectionsGain,     0.0f,   3.16f);
	settings.ReflectionsDelay    = FMath::Clamp(EAX->reflectionsDelay,    0.0f,   0.3f);
	settings.LateGain            = FMath::Clamp(EAX->lateReverbGain,      0.0f,  10.0f);
	settings.LateDelay           = FMath::Clamp(EAX->lateReverbDelay,     0.0f,   0.1f);
	settings.AirAbsorptionGainHF = FMath::Clamp(EAX->airAbsorptionGainHF, 0.0f,   1.0f);
	settings.WetLevel            = 1.0f;//FMath::Clamp(EAX->returnedPercent,     0.0f,   1.0f);
	settings.DryLevel            = 0.0f;

	return settings;
}
