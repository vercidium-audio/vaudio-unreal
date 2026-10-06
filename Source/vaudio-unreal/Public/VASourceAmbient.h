#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "VAFilterConversion.h"
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

	// The world whose main listener's ambient filter this source reads from
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	AVAWorld* AudioWorld = nullptr;

	// The sound file to play (2D - rain/wind/room-tone has no meaningful position)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

	const FVASourceFilter& GetFilter() const { return Filter; }

private:
	UPROPERTY(Transient)
	FVASourceFilter Filter;

	void TrySpawnSourceSound();

	bool checkedListenerRays = false;
	bool bSourcePendingSpawn = false;
};
