#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
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

	// The world whose current listener's reverb this source uses
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	AVAWorld* AudioWorld = nullptr;

	// One of these is picked at random when the sound plays
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ExposeOnSpawn = "true"))
	TArray<USoundBase*> SourceSounds;

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

	// The current listener's ListenerReverbSubmix, or null. Resolved every tick, even with no audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return ReverbSubmix; }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	float GetReverbSendLevel() const { return ReverbSendLevel; }

private:
	UPROPERTY(Transient)
	USoundSubmix* ReverbSubmix = nullptr;

	float ReverbSendLevel = 0.0f;

	UPROPERTY(Transient)
	AVAListener* WarnedListener = nullptr;

	bool bSourcePendingSpawn = false;

	void TrySpawnSourceSound();
	void UpdateSourceSubmix();
};
