#include "VASourcePlayback.h"
#include "Components/AudioComponent.h"

#include "VASubmixSend.h"

void FVASourcePlayback::Play(UAudioComponent* Component)
{
	Component->bAutoDestroy = true;
	Component->SetLowPassFilterEnabled(true);
	Component->SetLowPassFilterFrequency(Filter.GetCutoffFrequency());
	Component->SetVolumeMultiplier(GetVolume());
	Component->SetPitchMultiplier(PitchMultiplier);
	Component->Play();

	// These are commands to the active sound, which Play() creates
	if (ReverbSubmix)
		VASetReverbSend(Component, ReverbSubmix, ReverbSendLevel);

	if (!bDryEnabled)
		VASetDryOutputEnabled(Component, false);

	Components.Add(Component);
}

void FVASourcePlayback::Stop()
{
	for (UAudioComponent* component : Components)
		if (IsValid(component))
			component->Stop();

	Components.Empty();
}

bool FVASourcePlayback::IsPlaying() const
{
	for (UAudioComponent* component : Components)
		if (IsValid(component) && component->IsPlaying())
			return true;

	return false;
}

void FVASourcePlayback::RemoveFinished()
{
	// bAutoDestroy destroys a component once its sound finishes
	Components.RemoveAll([](const TObjectPtr<UAudioComponent>& component) { return !IsValid(component) || !component->IsPlaying(); });
}

void FVASourcePlayback::SetFilter(float GainLF, float GainHF)
{
	if (!Filter.Apply(GainLF, GainHF))
		return;

	for (UAudioComponent* component : Components)
	{
		if (IsValid(component))
		{
			component->SetLowPassFilterFrequency(Filter.GetCutoffFrequency());
			component->SetVolumeMultiplier(GetVolume());
		}
	}
}

void FVASourcePlayback::SetVolumeMultiplier(float Value)
{
	if (Value == VolumeMultiplier)
		return;

	VolumeMultiplier = Value;

	for (UAudioComponent* component : Components)
		if (IsValid(component))
			component->SetVolumeMultiplier(GetVolume());
}

void FVASourcePlayback::SetPitchMultiplier(float Value)
{
	if (Value == PitchMultiplier)
		return;

	PitchMultiplier = Value;

	for (UAudioComponent* component : Components)
		if (IsValid(component))
			component->SetPitchMultiplier(PitchMultiplier);
}

void FVASourcePlayback::SetDryOutputEnabled(bool bEnabled)
{
	if (bEnabled == bDryEnabled)
		return;

	bDryEnabled = bEnabled;

	for (UAudioComponent* component : Components)
		if (IsValid(component))
			VASetDryOutputEnabled(component, bDryEnabled);
}

void FVASourcePlayback::SetLocation(const FVector& Location)
{
	for (UAudioComponent* component : Components)
		if (IsValid(component))
			component->SetWorldLocationAndRotation(Location, FRotator::ZeroRotator);
}

void FVASourcePlayback::SetReverbSend(USoundSubmix* Submix, float SendLevel)
{
	// Moved to another submix (a different grouped EAX slot, between grouped and listener reverb, or a new current listener), so silence the old send
	if (ReverbSubmix && ReverbSubmix != Submix)
	{
		for (UAudioComponent* component : Components)
			if (IsValid(component))
				VASetReverbSend(component, ReverbSubmix, 0.0f);
	}

	ReverbSubmix = Submix;
	ReverbSendLevel = Filter.CompensateReverbSendLevel(SendLevel);

	if (!ReverbSubmix)
		return;

	for (UAudioComponent* component : Components)
		if (IsValid(component))
			VASetReverbSend(component, ReverbSubmix, ReverbSendLevel);
}
