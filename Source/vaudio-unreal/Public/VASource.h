#pragma once

#include "CoreMinimal.h"
#include "VAEmitter.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectFilter.h"
#include "VASource.generated.h"

UCLASS(DisplayName = "VASource")
class VAUDIOUNREAL_API AVASource : public AVAEmitter
{
	GENERATED_BODY()

public:
	AVASource();

protected:
	virtual bool ValidateConfig() override;
	virtual void InitializeTypeSpecific() override;
	virtual void DeinitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	// The sound file to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	void ApplySourceFilter(float GainLF, float GainHF);
	void SetDryOutputEnabled(bool bEnabled);

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

private:
	bool bCurrentDryEnabled = true;

	bool bSourcePendingSpawn = false;

	UPROPERTY(Transient)
	USourceEffectFilterPreset* SourceLPFPreset = nullptr;

	UPROPERTY(Transient)
	USoundEffectSourcePresetChain* SourceEffectChain = nullptr;

	void UpdateSourceSubmix();
	void TrySpawnSourceSound();
};
