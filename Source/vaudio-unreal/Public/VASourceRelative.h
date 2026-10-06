#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "VAFilterConversion.h"
#include "VASourceRelative.generated.h"

class AVAEmitterBase;
class AVAListener;
class AVAEmitter;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	TArray<USoundBase*> SourceSounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	bool bAttachToSelf = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	AVAEmitterBase* ReverbSource = nullptr;

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

	const FVASourceFilter& GetFilter() const { return Filter; }

	// The submix this source's reverb is sent to: the listener's ListenerReverbSubmix, the continuous emitter's grouped EAX submix (at its relative gain) or listener reverb, or null. Resolved every tick, even with no audio device
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

	// This actor has no VAEmitter* of its own (see class comment) but still needs a root
	// component so bAttachToSelf spawning has something to attach the audio component to.
	UPROPERTY(VisibleAnywhere, Category = "Vercidium Audio|Source")
	USceneComponent* SourceRootComponent = nullptr;

	// Cached downcasts of ReverbSource, resolved once at BeginPlay - exactly one of these is set
	// after a valid BeginPlay (or both null if ReverbSource was misconfigured).
	UPROPERTY(Transient)
	AVAListener* ListenerEmitter = nullptr;

	UPROPERTY(Transient)
	AVAEmitter* ContinuousEmitter = nullptr;

	bool bSourcePendingSpawn = false;

	void TrySpawnSourceSound();
	void UpdateSourceSubmix();
	bool ResolveReverbSend(USoundSubmix*& OutSubmix, float& OutSendLevel) const;
};
