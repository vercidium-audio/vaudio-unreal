#pragma once

#include "CoreMinimal.h"
#include "VAFilterConversion.h"
#include "VASourcePlayback.generated.h"

class UAudioComponent;
class USoundSubmix;

// Owns every audio component a source plays, and pushes the muffling filter, volume/pitch multipliers, dry output toggle and reverb send to them. Each Play() adds a component, so repeated plays overlap instead of cutting each other off, like Godot's ALSource. Every audio mixer call a source makes goes through here
USTRUCT()
struct VAUDIOUNREAL_API FVASourcePlayback
{
	GENERATED_BODY()

	// Takes a component that hasn't started playing, applies the current state to it, then plays it
	void Play(UAudioComponent* Component);

	void Stop();

	bool IsPlaying() const;

	// Drops components that finished playing. Only for auto-destroyed components, as nothing else would destroy the dropped ones
	void RemoveFinished();

	// Set on each component by Play(). Off for pooled components, which their owner takes back after Stop()
	bool bAutoDestroy = true;

	// Kept with no components (not playing yet, or no audio device), and applied by the next Play()
	void SetFilter(float GainLF, float GainHF);

	void SetVolumeMultiplier(float Value);
	void SetPitchMultiplier(float Value);
	void SetDryOutputEnabled(bool bEnabled);

	// For components that aren't attached to the source
	void SetLocation(const FVector& Location);

	// Silences the old submix when it changes. The level is divided by the filter's gainLF, see VACompensateReverbSendLevel
	void SetReverbSend(USoundSubmix* Submix, float SendLevel);

	const FVASourceFilter& GetFilter() const { return Filter; }
	USoundSubmix* GetReverbSubmix() const { return ReverbSubmix; }
	float GetReverbSendLevel() const { return ReverbSendLevel; }

	// The component volume: gainLF x the volume multiplier
	float GetVolume() const { return Filter.GetVolume() * VolumeMultiplier; }
	float GetPitchMultiplier() const { return PitchMultiplier; }

	const TArray<UAudioComponent*>& GetComponents() const { return Components; }

private:
	UPROPERTY(Transient)
	TArray<UAudioComponent*> Components;

	UPROPERTY(Transient)
	USoundSubmix* ReverbSubmix = nullptr;

	FVASourceFilter Filter;

	float ReverbSendLevel = 0.0f;
	float VolumeMultiplier = 1.0f;
	float PitchMultiplier = 1.0f;
	bool bDryEnabled = true;
};

// Ends a play that's waiting (bAutoPlay, or a loop waiting for the bounds) as if it had played, so bAutoDestroy destroys its source. For Stop()
void VACancelPendingPlay(bool& bAutoPlayPending, bool& bPlayAttempted);

// The bounds rules for a source's Play() state, shared by VASource and VASourceLeech. While bSilenced (see AVAEmitter::IsSilencedByBounds) it stops Playback: a loop that was playing waits to play again from the start (bAutoPlayPending), and anything else that was waiting to play ends as if it had played, so bAutoDestroy destroys its source. Returns bSilenced, when nothing should start
bool VAApplyBoundsRules(FVASourcePlayback& Playback, bool bSilenced, bool bLooping, bool& bAutoPlayPending, bool& bPlayAttempted);

// For Play() while bSilenced: a loop waits to play once back inside, anything else is warned about and ends as if it had played. Returns bSilenced, when Play() should return false
bool VAApplyBoundsRulesOnPlay(const UObject* Source, FVASourcePlayback& Playback, bool bSilenced, bool bLooping, bool& bAutoPlayPending, bool& bPlayAttempted);
