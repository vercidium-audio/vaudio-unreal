// Human-reviewed

#pragma once

#include "CoreMinimal.h"
#include "VAEmitter.h"
#include "VASourcePlayback.h"
#include "VARaytracedSource.generated.h"

// Base for sources that are raytraced emitters themselves (VASource and VAStreamSource). Owns the playback, and pushes this emitter's muffling, multipliers and reverb send to it every tick
UCLASS(Abstract, DisplayName = "VARaytracedSource")
class VAUDIOUNREAL_API AVARaytracedSource : public AVAEmitter
{
	GENERATED_BODY()

public:
	AVARaytracedSource();

protected:
	virtual void DeinitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

	// Pushes the muffling result, multipliers and reverb send to Playback
	void UpdatePlayback();

	UPROPERTY(Transient)
	FVASourcePlayback Playback;

public:
	// Multiplies the volume set by muffling. The reverb send is scaled by it too
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.01", UIMax = "4.0"))
	float PitchMultiplier = 1.0f;

	// Silences this source's direct audio but keeps its reverb send, e.g. to hear your own microphone in the room without the dry signal. The VAWorld's bReverbOnly does this for every source
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bReverbOnly = false;

	// Stops every sound this source is playing
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	virtual void Stop();

	// False while paused
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Source")
	bool IsPlaying() const { return Playback.IsPlaying(); }

	// Pauses every sound this source is playing, and resumes them from the same time when unpaused. Sounds played while paused start paused
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	void SetPaused(bool bPause) { Playback.SetPaused(bPause); }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Source")
	bool IsPaused() const { return Playback.IsPaused(); }

	// Moves every sound this source is playing to Time seconds in, while playing or paused. A looping sound wraps around, and one that doesn't loop finishes if Time is past its end
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	virtual void Seek(float Time) { Playback.Seek(Time); }

	// How many seconds into the most recently played sound it is, or 0 if nothing is playing. Only updated while the sound plays on an audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Source")
	float GetPlaybackTime() const { return Playback.GetPlaybackTime(); }

	const FVASourcePlayback& GetPlayback() const { return Playback; }

	// The submix this source's reverb is sent to: its grouped EAX submix, the current listener's ListenerReverbSubmix (bUseListenerReverb), or null. Resolved every tick, even with no audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return Playback.GetReverbSubmix(); }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	float GetReverbSendLevel() const { return Playback.GetReverbSendLevel(); }
};
