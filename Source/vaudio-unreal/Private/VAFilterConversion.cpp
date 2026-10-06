#include "VAFilterConversion.h"
#include "Components/AudioComponent.h"

void FVASourceFilter::Attach(UAudioComponent* Component) const
{
	Component->SetLowPassFilterEnabled(true);
	Component->SetLowPassFilterFrequency(CutoffFrequency);
	Component->SetVolumeMultiplier(Volume);
}

void FVASourceFilter::Apply(UAudioComponent* Component, float GainLF, float GainHF)
{
	float cutoff = VAGainHFToCutoffFrequency(GainHF);

	if (cutoff != CutoffFrequency)
	{
		CutoffFrequency = cutoff;

		if (Component)
			Component->SetLowPassFilterFrequency(CutoffFrequency);
	}

	if (GainLF != Volume)
	{
		Volume = GainLF;

		if (Component)
			Component->SetVolumeMultiplier(Volume);
	}
}
