#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "VAudioRelativeSource.generated.h"

class AVAudioEmitterBase;
class AVAudioListener;
class AVAudioContinuous;

UCLASS(DisplayName = "VAudio Relative Source")
class VAUDIOUNREAL_API AVAudioRelativeSource : public AActor
{
	GENERATED_BODY()

public:
	AVAudioRelativeSource();

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
	AVAudioEmitterBase* ReverbSource = nullptr;

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

private:
	// This actor has no VAEmitter* of its own (see class comment) but still needs a root
	// component so bAttachToSelf spawning has something to attach the audio component to.
	UPROPERTY(VisibleAnywhere, Category = "Vercidium Audio|Source")
	USceneComponent* SourceRootComponent = nullptr;

	// Cached downcasts of ReverbSource, resolved once at BeginPlay - exactly one of these is set
	// after a valid BeginPlay (or both null if ReverbSource was misconfigured).
	UPROPERTY(Transient)
	AVAudioListener* ListenerEmitter = nullptr;

	UPROPERTY(Transient)
	AVAudioContinuous* ContinuousEmitter = nullptr;

	bool bSourcePendingSpawn = false;

	void TrySpawnSourceSound();
	void ApplyReverbSource();
	void DisplayWarning(const TCHAR* fmt, ...) const;
};
