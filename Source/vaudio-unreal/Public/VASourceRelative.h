#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VASourcePlayback.h"
#include "VASourceRelative.generated.h"

class AVAWorld;
class AVAListener;

// A listener-relative (2D) sound, e.g. the player's own footsteps or weapon. It isn't raytraced or muffled, and sends to the current listener's ListenerReverbSubmix, like Godot's VASourceRelative
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

	// When true, plays on the first tick. Otherwise it only plays when Play() is called
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	bool bAutoPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.01", UIMax = "4.0"))
	float PitchMultiplier = 1.0f;

	// Plays a random sound from SourceSounds. Each call starts a new sound, so calls overlap. Returns false if this actor failed validation, or if the sound couldn't be created (e.g. there's no audio device)
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	bool Play();

	// Stops every sound started by Play()
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	void Stop();

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Source")
	bool IsPlaying() const { return Playback.IsPlaying(); }

	const FVASourcePlayback& GetPlayback() const { return Playback; }

	// The current listener's ListenerReverbSubmix, or null. Resolved every tick, even with no audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return Playback.GetReverbSubmix(); }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	float GetReverbSendLevel() const { return Playback.GetReverbSendLevel(); }

private:
	UPROPERTY(Transient)
	FVASourcePlayback Playback;

	UPROPERTY(Transient)
	AVAListener* WarnedListener = nullptr;

	bool bValidConfig = false;

	// Cleared by the first Play(), whether bAutoPlay or the user called it
	bool bAutoPlayPending = false;

	// Pushes the multipliers and the listener's reverb send to Playback
	void UpdatePlayback();
};
