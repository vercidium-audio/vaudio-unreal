#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "VAudioAmbientSource.generated.h"

class AVAudioWorld;
struct VALowPassFilter;

UCLASS(DisplayName = "VAudio Ambient Source")
class VAUDIOUNREAL_API AVAudioAmbientSource : public AActor
{
	GENERATED_BODY()

public:
	AVAudioAmbientSource();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// The world whose main listener's ambient filter this source reads from
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	AVAudioWorld* AudioWorld = nullptr;

	// The sound file to play (2D - rain/wind/room-tone has no meaningful position)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

private:
	void TrySpawnSourceSound(const VALowPassFilter* AmbientFilter);
	void DisplayWarning(const TCHAR* fmt, ...) const;
};
