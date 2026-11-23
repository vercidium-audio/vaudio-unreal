#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VASourcePlayback.h"
#include "VASourceRelative.generated.h"

class AVAWorld;
class AVAListener;

// A listener-relative (2D) sound, e.g. the player's own footsteps or weapon. It isn't raytraced or muffled, and sends to the current listener's ListenerReverbSubmix, like Godot's VASourceRelative. It waits for the listener's first EAX before playing (see IsReadyToPlay)
UCLASS(DisplayName = "VASourceRelative")
class VAUDIOUNREAL_API AVASourceRelative : public AActor
{
	GENERATED_BODY()

public:
	AVASourceRelative();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// One of these is picked at random each time the sound plays
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	TArray<USoundBase*> SourceSounds;

	// When true, plays once this source is ready to play (see IsReadyToPlay). Otherwise it only plays when Play() is called
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	bool bAutoPlay = false;

	// Destroys this actor once it has played and nothing is playing any more. The reverb tail belongs to the listener, so it isn't waited for. Disable this to call Play() repeatedly. A source that never plays isn't destroyed
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	bool bAutoDestroy = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.01", UIMax = "4.0"))
	float PitchMultiplier = 1.0f;

	// Silences this source's direct audio but keeps its send to the listener's reverb, e.g. to hear your own microphone in the room without the dry signal. The VAWorld's bReverbOnly does this for every source
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bReverbOnly = false;

	// Plays a random sound from SourceSounds, starting StartTime seconds in. Each call starts a new sound, so calls overlap. While paused it starts paused. Before IsReadyToPlay() is true it returns true and waits, then plays once ready (several calls while waiting play once, from the last StartTime). Returns false if this actor failed validation, or if the sound couldn't be created (e.g. there's no audio device)
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	bool Play(float StartTime = 0.0f);

	// Stops every sound started by Play(), and cancels a bAutoPlay or Play() that's waiting for IsReadyToPlay(), as if it had played
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	void Stop();

	// False while paused
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Source")
	bool IsPlaying() const { return Playback.IsPlaying(); }

	// See AVARaytracedSource::SetPaused
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	void SetPaused(bool bPause) { Playback.SetPaused(bPause); }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Source")
	bool IsPaused() const { return Playback.IsPaused(); }

	// See AVARaytracedSource::Seek
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	void Seek(float Time) { Playback.Seek(Time); }

	// See AVARaytracedSource::GetPlaybackTime
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Source")
	float GetPlaybackTime() const { return Playback.GetPlaybackTime(); }

	// True once the current listener has its first EAX, so the sound never plays through the default reverb of its ListenerReverbSubmix. Also true straight away if the listener has no ListenerReverbSubmix, casts no reverb rays or is outside the VAWorld's bounds. False while there's no listener
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	bool IsReadyToPlay() const;

	const FVASourcePlayback& GetPlayback() const { return Playback; }

	// The current listener's ListenerReverbSubmix, or null. Resolved every tick, even with no audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return Playback.GetReverbSubmix(); }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	float GetReverbSendLevel() const { return Playback.GetReverbSendLevel(); }

	// Sends playback to the current listener's ListenerReverbSubmix, unmuffled. Keeps the current send while there's no listener. Shared with AVAWorld::PlaySoundRelative, which warns as reporter once per listener through warnedListener
	static void ApplyListenerReverbSend(FVASourcePlayback& playback, const AVAWorld* audioWorld, const UObject* reporter, TWeakObjectPtr<AVAListener>& warnedListener);

private:
	UPROPERTY(Transient)
	FVASourcePlayback Playback;

	TWeakObjectPtr<AVAListener> WarnedListener;

	bool bValidConfig = false;

	// A bAutoPlay or Play() waiting for IsReadyToPlay(), started from PendingStartTime by Tick
	bool bPlayPending = false;
	float PendingStartTime = 0.0f;

	// Set by the first Play() once this source is ready, whether or not a sound could be created
	bool bPlayAttempted = false;

	// Pushes the multipliers and the listener's reverb send to Playback
	void UpdatePlayback();
};
