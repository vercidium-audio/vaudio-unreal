#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "VASourcePlayback.h"
#include "VASourceLeech.generated.h"

class AVAEmitter;

// A spatialised sound that reuses an AVAEmitter's muffling and reverb instead of raytracing itself, e.g. several gunshots and footsteps attached to one enemy. Like Godot's VASourceLeech
UCLASS(DisplayName = "VASourceLeech")
class VAUDIOUNREAL_API AVASourceLeech : public AActor
{
	GENERATED_BODY()

public:
	AVASourceLeech();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// One of these is picked at random each time the sound plays
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	TArray<USoundBase*> SourceSounds;

	// Only used when this actor isn't attached to an AVAEmitter. The emitter it's attached to always takes priority
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	AVAEmitter* Emitter = nullptr;

	// When true, plays once the leeched emitter is ready to play (see IsReadyToPlay). Otherwise it only plays when Play() is called
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	bool bAutoPlay = false;

	// Multiplies the volume set by muffling. The reverb send is scaled by it too
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.01", UIMax = "4.0"))
	float PitchMultiplier = 1.0f;

	// Plays a random sound from SourceSounds. Each call starts a new sound, so calls overlap. Returns false until IsReadyToPlay() is true, or if the sound couldn't be created (e.g. there's no audio device)
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	bool Play();

	// Stops every sound started by Play()
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	void Stop();

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Source")
	bool IsPlaying() const { return Playback.IsPlaying(); }

	// True once the leeched emitter is ready to play, see AVAEmitter::IsReadyToPlay
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	bool IsReadyToPlay() const;

	// The emitter this source currently leeches: the AVAEmitter it's attached to, else Emitter
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	AVAEmitter* GetLeechedEmitter() const;

	const FVASourcePlayback& GetPlayback() const { return Playback; }

	// The leeched emitter's grouped EAX submix (at its relative gain), the listener's ListenerReverbSubmix (bUseListenerReverb), or null. Resolved every tick, even with no audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return Playback.GetReverbSubmix(); }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	float GetReverbSendLevel() const { return Playback.GetReverbSendLevel(); }

private:
	UPROPERTY(Transient)
	FVASourcePlayback Playback;

	// Lets this actor attach to an emitter, and the audio components attach to this actor
	UPROPERTY(VisibleAnywhere, Category = "Vercidium Audio|Source")
	USceneComponent* SourceRootComponent = nullptr;

	bool bValidConfig = false;

	// Cleared by the first Play(), whether bAutoPlay or the user called it
	bool bAutoPlayPending = false;

	bool bWarnedNoEmitter = false;

	// Pushes the multipliers, and the emitter's muffling and reverb send, to Playback
	void UpdatePlayback(AVAEmitter* emitter);
};
