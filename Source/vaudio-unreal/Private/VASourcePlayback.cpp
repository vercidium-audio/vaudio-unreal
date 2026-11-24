// Human-reviewed

#include "VASourcePlayback.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"

#include "VASubmixSend.h"
#include "VALog.h"

void VACancelPendingPlay(bool& bAutoPlayPending, bool& bPlayAttempted)
{
	if (!bAutoPlayPending)
		return;

	bAutoPlayPending = false;
	bPlayAttempted = true;
}

bool VAApplyBoundsRules(FVASourcePlayback& Playback, bool bSilenced, bool bLooping, bool& bAutoPlayPending, bool& bPlayAttempted)
{
	if (!bSilenced)
		return false;

	if (!bLooping)
	{
		VACancelPendingPlay(bAutoPlayPending, bPlayAttempted);
	}
	// A paused loop waits too, and starts paused once back inside
	else if (Playback.HasSound())
	{
		bAutoPlayPending = true;
		bPlayAttempted = false;
	}

	Playback.Stop();
	return true;
}

bool VAApplyBoundsRulesOnPlay(const UObject* Source, FVASourcePlayback& Playback, bool bSilenced, bool bLooping, bool& bAutoPlayPending, bool& bPlayAttempted)
{
	if (!bSilenced)
		return false;

	if (!bLooping)
		VAReport(ELogVerbosity::Warning, Source, VA_CALL_SITE_KEY(Source), TEXT("This source did not play because it is outside the world bounds."));

	bAutoPlayPending = true;
	return VAApplyBoundsRules(Playback, bSilenced, bLooping, bAutoPlayPending, bPlayAttempted);
}

void FVASourcePlayback::Play(UAudioComponent* Component, float StartTime)
{
	Component->bAutoDestroy = bAutoDestroy;

	// Must set these directly, as SetLowPassFilterFrequency() does nothing before calling Play()
	Component->bEnableLowPassFilter = true;
	Component->LowPassFilterFrequency = Filter.GetCutoffFrequency();
	Component->SetVolumeMultiplier(GetVolume());
	Component->SetPitchMultiplier(PitchMultiplier);

	// Play() copies this to the sound it starts
	Component->SetPaused(bPaused);

	if (bTrackPlaybackTime)
	{
		TSharedPtr<float> playbackTime = MakeShared<float>(FMath::Max(0.0f, StartTime));
		LatestPlaybackTime = playbackTime;

		// Must be bound before Play(), which only reports the playback percent if something is listening. The percent includes the start time, and keeps growing while a looping sound repeats
		Component->OnAudioPlaybackPercentNative.AddLambda([playbackTime](const UAudioComponent*, const USoundWave* Wave, const float Percent)
		{
			const float duration = Wave ? Wave->Duration : 0.0f;

			if (duration <= 0.0f)
				return;

			const float time = Percent * duration;
			*playbackTime = Wave->bLooping ? FMath::Fmod(time, duration) : FMath::Min(time, duration);
		});
	}

	StartComponent(Component, StartTime);
	Components.Add(Component);
}

void FVASourcePlayback::StartComponent(UAudioComponent* Component, float StartTime)
{
	Component->Play(FMath::Max(0.0f, StartTime));

	if (ReverbSubmix)
		VASetReverbSend(Component, ReverbSubmix, ReverbSendLevel);

	if (!bDryEnabled)
		VASetDryOutputEnabled(Component, false);
}

void FVASourcePlayback::Stop()
{
	for (UAudioComponent* component : Components)
		if (IsValid(component))
			component->Stop();

	Components.Empty();
	LatestPlaybackTime.Reset();
}

bool FVASourcePlayback::HasSound() const
{
	for (UAudioComponent* component : Components)
		if (IsValid(component) && component->IsPlaying())
			return true;

	return false;
}

void FVASourcePlayback::SetPaused(bool bPause)
{
	if (bPause == bPaused)
		return;

	bPaused = bPause;

	for (UAudioComponent* component : Components)
		if (IsValid(component))
			component->SetPaused(bPaused);
}

void FVASourcePlayback::Seek(float Time)
{
	// A finished sound that hasn't been dropped yet isn't restarted
	for (UAudioComponent* component : Components)
		if (IsValid(component) && component->IsPlaying())
			StartComponent(component, Time);

	if (LatestPlaybackTime)
		*LatestPlaybackTime = FMath::Max(0.0f, Time);
}

float FVASourcePlayback::GetPlaybackTime() const
{
	return HasSound() && LatestPlaybackTime ? *LatestPlaybackTime : 0.0f;
}

void FVASourcePlayback::RemoveFinished()
{
	// bAutoDestroy destroys a component once its sound finishes
	Components.RemoveAll([](UAudioComponent* component)
	{
		return !IsValid(component) || !component->IsPlaying();
	});
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
	// Silence the old submix when moving to a new one
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
