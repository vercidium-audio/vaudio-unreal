#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "VAFilterConversion.h"
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

	// One of these is picked at random when the sound plays
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	TArray<USoundBase*> SourceSounds;

	// Only used when this actor isn't attached to an AVAEmitter. The emitter it's attached to always takes priority
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	AVAEmitter* Emitter = nullptr;

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

	// The emitter this source currently leeches: the AVAEmitter it's attached to, else Emitter
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	AVAEmitter* GetLeechedEmitter() const;

	const FVASourceFilter& GetFilter() const { return Filter; }

	// The leeched emitter's grouped EAX submix (at its relative gain), the listener's ListenerReverbSubmix (bUseListenerReverb), or null. Resolved every tick, even with no audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return ReverbSubmix; }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	float GetReverbSendLevel() const { return ReverbSendLevel; }

private:
	UPROPERTY(Transient)
	FVASourceFilter Filter;

	UPROPERTY(Transient)
	USoundSubmix* ReverbSubmix = nullptr;

	float ReverbSendLevel = 0.0f;

	// Lets this actor attach to an emitter, and the audio component attach to this actor
	UPROPERTY(VisibleAnywhere, Category = "Vercidium Audio|Source")
	USceneComponent* SourceRootComponent = nullptr;

	bool bSourcePendingSpawn = false;
	bool bWarnedNoEmitter = false;

	void TrySpawnSourceSound(AVAEmitter* emitter);
	void UpdateSourceSubmix(AVAEmitter* emitter);
};
