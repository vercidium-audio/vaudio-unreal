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

	// The submix this source's reverb is sent to: its grouped EAX submix, the current listener's ListenerReverbSubmix (bUseListenerReverb), or null. Resolved every tick, even with no audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return ReverbSubmix; }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	float GetReverbSendLevel() const { return ReverbSendLevel; }

private:
	UPROPERTY(Transient)
	USoundSubmix* ReverbSubmix = nullptr;

	float ReverbSendLevel = 0.0f;

	bool bCurrentDryEnabled = true;

	bool bSourcePendingSpawn = false;

	UPROPERTY(Transient)
	USourceEffectFilterPreset* SourceLPFPreset = nullptr;

	UPROPERTY(Transient)
	USoundEffectSourcePresetChain* SourceEffectChain = nullptr;

	void UpdateSourceSubmix();
	bool ResolveReverbSend(USoundSubmix*& OutSubmix, float& OutSendLevel);
	void SendToSubmix(USoundSubmix* Submix, float SendLevel);
	void TrySpawnSourceSound();
};
