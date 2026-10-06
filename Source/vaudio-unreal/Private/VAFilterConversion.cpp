#include "VAFilterConversion.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectFilter.h"

const float LOW_PASS_RESONANCE = 0.707f; // Butterworth Q constant - maximally flat passband, no resonant peak at the cutoff

void FVASourceFilter::Initialize(UObject* Outer)
{
	Preset = NewObject<USourceEffectFilterPreset>(Outer);
	PushCutoff();

	Chain = NewObject<USoundEffectSourcePresetChain>(Outer);
	FSourceEffectChainEntry ChainEntry;
	ChainEntry.Preset = Preset;
	ChainEntry.bBypass = false;
	Chain->Chain.Add(ChainEntry);
}

void FVASourceFilter::Attach(UAudioComponent* Component) const
{
	check(Chain);

	Component->SetSourceEffectChain(Chain);
	Component->SetVolumeMultiplier(Volume);
}

void FVASourceFilter::Apply(UAudioComponent* Component, float GainLF, float GainHF)
{
	float cutoff = VAGainHFToCutoffFrequency(GainHF);

	if (cutoff != CutoffFrequency)
	{
		CutoffFrequency = cutoff;
		PushCutoff();
	}

	if (GainLF != Volume)
	{
		Volume = GainLF;

		if (Component)
			Component->SetVolumeMultiplier(Volume);
	}
}

void FVASourceFilter::PushCutoff() const
{
	if (!Preset)
		return;

	FSourceEffectFilterSettings settings;
	settings.FilterCircuit   = ESourceEffectFilterCircuit::StateVariable;
	settings.FilterType      = ESourceEffectFilterType::LowPass;
	settings.CutoffFrequency = CutoffFrequency;
	settings.FilterQ         = LOW_PASS_RESONANCE;
	Preset->SetSettings(settings);
}
