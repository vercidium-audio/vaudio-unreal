#pragma once

#include "CoreMinimal.h"
#include "VARaytracedSource.h"
#include "VASource.generated.h"

class USoundAttenuation;

UCLASS(DisplayName = "VASource")
class VAUDIOUNREAL_API AVASource : public AVARaytracedSource
{
	GENERATED_BODY()

protected:
	virtual bool ValidateConfig() override;
	virtual void InitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	virtual void Tick(float DeltaTime) override;

	// The sound file to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	// Used instead of SourceSound's own Sound Attenuation when set. Applies to sounds started after it changes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundAttenuation* AttenuationSettings = nullptr;

	// When true, plays SourceSound once this source is ready to play (see IsReadyToPlay). Otherwise it only plays when Play() is called
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bAutoPlay = false;

	// Destroys this actor once it has played and nothing is playing any more, like the sources spawned by AVAWorld::PlaySoundAtLocation. Its first Play() also cancels its life span, which PlaySoundAtLocation sets so a source that never becomes ready doesn't wait forever
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bDestroyWhenFinished = false;

	// Plays SourceSound. Each call starts a new sound, so calls overlap. Returns false until IsReadyToPlay() is true, or if the sound couldn't be created (e.g. there's no audio device)
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	bool Play();

	// Destroys this actor once the component is destroyed, or its actor is. Unreal detaches attached actors rather than destroying them, so AVAWorld::SpawnSoundAttached uses this for bStopWhenAttachedToDestroyed
	void DestroyWithComponent(USceneComponent* component);

private:
	// Cleared by the first Play(), whether bAutoPlay or the user called it
	bool bAutoPlayPending = false;

	// Set by the first Play() once this source is ready, whether or not a sound could be created
	bool bPlayAttempted = false;

	bool bDestroyWithComponent = false;
	TWeakObjectPtr<USceneComponent> DestroyWithComponentTarget;
};
