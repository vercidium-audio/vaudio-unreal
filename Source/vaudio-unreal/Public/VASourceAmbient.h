#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VASourcePlayback.h"
#include "VASourceAmbient.generated.h"

class AVAWorld;

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

	const FVASourcePlayback& GetPlayback() const { return Playback; }

private:
	UPROPERTY(Transient)
	FVASourcePlayback Playback;

	void TrySpawnSourceSound();

	bool checkedListenerRays = false;
	bool bSourcePendingSpawn = false;
};
