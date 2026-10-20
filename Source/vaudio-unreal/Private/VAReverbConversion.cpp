#include "VAReverbConversion.h"

// UE's plate reverb (FSubmixEffectReverb + FLateReflectionsFast) doesn't treat DecayTime as an RT60, so the EAX fields can't be passed through as-is. Its Density and Diffusion drive the plate's allpass diffusers, and vaudio's low values (e.g. 0.03-0.3 for concrete) turn the tail into discrete repeats ~100 ms apart, so both are held at PlateDiffusion. DecayTime is then picked from a measured table so the plate's RT60 matches vaudio's, and DecayHFRatio from the HF decay ratio. The reverb_tail_rt60 automation test checks this

// Mean length of the left (908 + 4217 + 2656 + 3136) and right (672 + 4453 + 1800 + 3720) plate loops, in samples at the plates' 29761 Hz reference rate
static constexpr float PlatePassSeconds = 10781.0f / 29761.0f;

// UE doesn't pass the mixer's sample rate to presets, so assume the default
static constexpr float AssumedSampleRate = 48000.0f;

static constexpr float PlateDiffusion = 0.8f;

// UE DecayTime input -> measured RT60 of the plate at PlateDiffusion with no dampening (DecayHFRatio 0.05), from reverb_tail_rt60. Re-measure if PlateDiffusion or the engine's reverb changes
static constexpr float DecayTimeInputs[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 12.0f, 14.0f };
static constexpr float MeasuredRT60s[] = { 0.309f, 0.585f, 0.991f, 1.245f, 1.435f, 1.599f, 1.8f, 2.04f, 2.367f, 2.724f, 3.206f, 3.832f, 4.709f, 5.969f, 8.026f, 8.619f, 9.264f, 10.008f, 10.931f, 11.993f, 15.391f, 21.328f };

static float DecayTimeInputForRT60(float RT60)
{
	constexpr int32 count = UE_ARRAY_COUNT(MeasuredRT60s);
	RT60 = FMath::Clamp(RT60, MeasuredRT60s[0], MeasuredRT60s[count - 1]);

	for (int32 i = 1; i < count; ++i)
	{
		if (RT60 <= MeasuredRT60s[i])
		{
			float alpha = (RT60 - MeasuredRT60s[i - 1]) / (MeasuredRT60s[i] - MeasuredRT60s[i - 1]);
			return FMath::Lerp(DecayTimeInputs[i - 1], DecayTimeInputs[i], alpha);
		}
	}

	return DecayTimeInputs[count - 1];
}

// Linear gain per plate pass that decays by 60 dB over RT60 seconds
static float PassGainForRT60(float RT60)
{
	return FMath::Pow(10.0f, -3.0f * PlatePassSeconds / FMath::Max(RT60, 0.001f));
}

// One-pole LPF coefficient whose magnitude at Frequency is Ratio (DC gain is always 1)
static float OnePoleCoefficientForGain(float Ratio, float Frequency)
{
	if (Ratio >= 1.0f)
		return 0.0f;

	float r2 = Ratio * Ratio;
	float cosW = FMath::Cos(2.0f * PI * FMath::Min(Frequency, AssumedSampleRate * 0.49f) / AssumedSampleRate);
	float a = 1.0f - r2;
	float b = 1.0f - r2 * cosW;

	return FMath::Clamp((b - FMath::Sqrt(FMath::Max(b * b - a * a, 0.0f))) / a, 0.0f, 0.99f);
}

FSubmixEffectReverbSettings VAEAXReverbToSubmixSettings(const VAEAXReverb* EAX)
{
	FSubmixEffectReverbSettings settings;

	float rt60 = FMath::Clamp(EAX->decayTime, 0.1f, 20.0f);
	float rt60HF = rt60 * FMath::Clamp(EAX->decayHFRatio, 0.1f, 2.0f);

	// The LPF handles HF decaying faster than mid. It can't make HF decay slower, so ratios above 1 are flat
	float passGain = PassGainForRT60(rt60);
	float dampening = OnePoleCoefficientForGain(PassGainForRT60(rt60HF) / passGain, EAX->hfReference);

	// (1 - Dampening) also scales the whole tail, so cap it at half the mid decay rate. Long tails then get less HF damping than asked, but keep their mid RT60
	dampening = FMath::Min(dampening, 1.0f - FMath::Sqrt(passGain));

	// (1 - Dampening) takes a fixed number of dB off every pass, and the table covers the rest of the decay rate
	float dampeningDbPerSecond = -20.0f * FMath::LogX(10.0f, 1.0f - dampening) / PlatePassSeconds;
	float undampedDbPerSecond = FMath::Max(60.0f / rt60 - dampeningDbPerSecond, 0.001f);

	settings.DecayTime           = DecayTimeInputForRT60(60.0f / undampedDbPerSecond);
	settings.DecayHFRatio        = 0.05f + dampening / 0.999f * 1.9f;
	settings.Density             = PlateDiffusion;
	settings.Diffusion           = PlateDiffusion;
	// Unreal's plate reverb ignores LateGain and drives the late tail level from Gain instead, so fold lateReverbGain into it
	settings.Gain                = FMath::Clamp(EAX->gain * EAX->lateReverbGain, 0.0f, 1.0f);
	settings.ReflectionsGain     = FMath::Clamp(EAX->reflectionsGain,     0.0f,   3.16f);
	settings.ReflectionsDelay    = FMath::Clamp(EAX->reflectionsDelay,    0.0f,   0.3f);
	settings.LateGain            = FMath::Clamp(EAX->lateReverbGain,      0.0f,  10.0f);
	settings.LateDelay           = FMath::Clamp(EAX->lateReverbDelay,     0.0f,   0.1f);
	settings.AirAbsorptionGainHF = FMath::Clamp(EAX->airAbsorptionGainHF, 0.0f,   1.0f);

	settings.WetLevel            = FMath::Clamp(EAX->gainLF, 0.0f, 1.0f);
	settings.GainHF              = EAX->gainLF > 0.0f ? FMath::Clamp(EAX->gainHF / EAX->gainLF, 0.0f, 1.0f) : 0.0f;
	settings.DryLevel            = 0.0f;

	return settings;
}
