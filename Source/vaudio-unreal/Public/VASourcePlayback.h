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

	// Drops components that finished playing
	void RemoveFinished();

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

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> Components;

	UPROPERTY(Transient)
	TObjectPtr<USoundSubmix> ReverbSubmix = nullptr;

	FVASourceFilter Filter;

	float ReverbSendLevel = 0.0f;
	float VolumeMultiplier = 1.0f;
	float PitchMultiplier = 1.0f;
	bool bDryEnabled = true;
};
