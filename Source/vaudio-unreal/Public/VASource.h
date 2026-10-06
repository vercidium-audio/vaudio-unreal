#pragma once

#include "CoreMinimal.h"
#include "VAEmitter.h"
#include "VASourcePlayback.h"
#include "VASource.generated.h"

UCLASS(DisplayName = "VASource")
class VAUDIOUNREAL_API AVASource : public AVAEmitter
{
	GENERATED_BODY()

public:
	AVASource();

protected:
	virtual bool ValidateConfig() override;
	virtual void InitializeTypeSpecific() override;
	virtual void DeinitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	// The sound file to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	// When true, plays SourceSound once this source is ready to play (see IsReadyToPlay). Otherwise it only plays when Play() is called
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bAutoPlay = false;

	// Multiplies the volume set by muffling. The reverb send is scaled by it too
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.01", UIMax = "4.0"))
	float PitchMultiplier = 1.0f;

	// Plays SourceSound. Each call starts a new sound, so calls overlap. Returns false until IsReadyToPlay() is true, or if the sound couldn't be created (e.g. there's no audio device)
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	bool Play();

	// Stops every sound started by Play()
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	void Stop();

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Source")
	bool IsPlaying() const { return Playback.IsPlaying(); }

	void SetDryOutputEnabled(bool bEnabled) { Playback.SetDryOutputEnabled(bEnabled); }

	const FVASourcePlayback& GetPlayback() const { return Playback; }

	// The submix this source's reverb is sent to: its grouped EAX submix, the current listener's ListenerReverbSubmix (bUseListenerReverb), or null. Resolved every tick, even with no audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return Playback.GetReverbSubmix(); }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	float GetReverbSendLevel() const { return Playback.GetReverbSendLevel(); }

private:
	UPROPERTY(Transient)
	FVASourcePlayback Playback;

	// Cleared by the first Play(), whether bAutoPlay or the user called it
	bool bAutoPlayPending = false;

	// Pushes the muffling result, multipliers and reverb send to Playback
	void UpdatePlayback();
};
