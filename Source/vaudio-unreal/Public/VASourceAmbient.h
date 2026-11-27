// Human-created

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VASourcePlayback.h"
#include "VASourceAmbient.generated.h"

class AVAWorld;
class AVAListener;
class USoundSubmix;

UENUM(BlueprintType)
enum class EVAAmbientReverbSend : uint8
{
	None, // No reverb	
	ListenerReverbSubmix, // The current listener's ListenerReverbSubmix. Waits for the listener's first EAX before playing	
	Custom, // CustomReverbSubmix, e.g. grouped EAX, or a manually configured submix
};

UCLASS(DisplayName = "VASourceAmbient")
class VAUDIOUNREAL_API AVASourceAmbient : public AActor
{
	GENERATED_BODY()

public:
	AVASourceAmbient();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// The sound file to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.01", UIMax = "4.0"))
	float PitchMultiplier = 1.0f;

	// Only play reverb
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bReverbOnly = false;

	// Reverb type. The send is muffled by the listener's ambient filter's gainLF. TODO - custom LF/HF muffling in Unreal?
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	EVAAmbientReverbSend ReverbSend = EVAAmbientReverbSend::None;

	// Which reverb submix to use
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (EditCondition = "ReverbSend == EVAAmbientReverbSend::Custom", EditConditionHides))
	USoundSubmix* CustomReverbSubmix = nullptr;

	// True when the listener has cast its ambient and reverb rays
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	bool IsReadyToPlay() const;

	// Get the current reverb submix that this source is sending to. Updates every tick
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return Playback.GetReverbSubmix(); }

	// Volume multiplied by muffling gainLF, before distance attenuation
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	float GetReverbSendLevel() const { return Playback.GetReverbSendLevel(); }

	const FVASourcePlayback& GetPlayback() const { return Playback; }

private:
	UPROPERTY(Transient)
	FVASourcePlayback Playback;

	TWeakObjectPtr<AVAListener> WarnedListener;

	void TrySpawnSourceSound();

	bool checkedListenerRays = false;
	bool bSourcePendingSpawn = false;
};
