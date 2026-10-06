#pragma once

#include "CoreMinimal.h"
#include "VARaytracedSource.h"
#include "VASource.generated.h"

UCLASS(DisplayName = "VASource")
class VAUDIOUNREAL_API AVASource : public AVARaytracedSource
{
	GENERATED_BODY()

protected:
	virtual bool ValidateConfig() override;
	virtual void InitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	// The sound file to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	// When true, plays SourceSound once this source is ready to play (see IsReadyToPlay). Otherwise it only plays when Play() is called
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bAutoPlay = false;

	// Plays SourceSound. Each call starts a new sound, so calls overlap. Returns false until IsReadyToPlay() is true, or if the sound couldn't be created (e.g. there's no audio device)
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	bool Play();

private:
	// Cleared by the first Play(), whether bAutoPlay or the user called it
	bool bAutoPlayPending = false;
};
