#pragma once

#include "CoreMinimal.h"
#include "VAudioContinuous.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectFilter.h"
#include "VAudioSource.generated.h"

UCLASS(DisplayName = "VAudio Source")
class VAUDIOUNREAL_API AVAudioSource : public AVAudioContinuous
{
	GENERATED_BODY()

public:
	AVAudioSource();

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
