#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VASourcePlayback.h"
#include "VASourceAmbient.generated.h"

class AVAWorld;
class AVAListener;
class USoundSubmix;

// Where an AVASourceAmbient's reverb is sent
UENUM(BlueprintType)
enum class EVAAmbientReverbSend : uint8
{
	None,
	// The current listener's ListenerReverbSubmix. Waits for the listener's first EAX before playing
	ListenerReverbSubmix,
	// CustomReverbSubmix, e.g. a hand-tuned reverb. Plays straight away
	Custom,
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

	// The sound file to play (2D - rain/wind/room-tone has no meaningful position)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	// Multiplies the volume set by the listener's ambient filter
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.01", UIMax = "4.0"))
	float PitchMultiplier = 1.0f;

	// Silences this source's direct audio but keeps its reverb send. With ReverbSend set to None this silences it entirely. The VAWorld's bReverbOnly does this for every source
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bReverbOnly = false;

	// Where this source's reverb is sent. The send is muffled by the listener's ambient filter's gainLF, like Godot's use_listener_reverb
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	EVAAmbientReverbSend ReverbSend = EVAAmbientReverbSend::None;

	// The submix this source's reverb is sent to when ReverbSend is Custom
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (EditCondition = "ReverbSend == EVAAmbientReverbSend::Custom", EditConditionHides))
	USoundSubmix* CustomReverbSubmix = nullptr;

	// True once the listener has its first ambient filter and, with ReverbSend set to ListenerReverbSubmix, its first EAX (see AVASourceRelative::IsReadyToPlay)
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	bool IsReadyToPlay() const;

	// The submix this source's reverb is sent to, or null. Resolved every tick once the listener has its ambient filter, even with no audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return Playback.GetReverbSubmix(); }

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
