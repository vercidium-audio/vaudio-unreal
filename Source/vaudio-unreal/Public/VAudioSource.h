#pragma once

#include "CoreMinimal.h"
#include "VAudioContinuous.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectFilter.h"
#include "VAudioSource.generated.h"

// A raytracing target that also plays a 3D sound - the direct equivalent of today's
// AVAudioEmitter with bIsMainListener = false. Inherits all raytracing-target behaviour
// (occlusion/permeation, grouped-EAX submix routing) from AVAudioContinuous and adds the
// SourceSound playback/filtering on top.
UCLASS(DisplayName = "VAudio Source")
class VAUDIOUNREAL_API AVAudioSource : public AVAudioContinuous
{
	GENERATED_BODY()

public:
	AVAudioSource();

protected:
	virtual void InitializeTypeSpecific() override;
	virtual void DeinitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	// The sound file to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	// Whether the sound should loop
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bLooping = true;

	// --- Runtime access ---

	void ApplySourceFilter(float GainLF, float GainHF);
	void SetDryOutputEnabled(bool bEnabled);

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

private:
	bool bCurrentDryEnabled = true;

	// True once TryInitializeEmitter() has built the LPF chain for SourceSound but hasn't spawned
	// SourceAudioComponent yet - spawn is deferred until the main listener has raytraced this
	// emitter at least once, so the source doesn't start clear and then pop to muffled (see Tick()).
	bool bSourcePendingSpawn = false;

	UPROPERTY(Transient)
	USourceEffectFilterPreset* SourceLPFPreset = nullptr;

	UPROPERTY(Transient)
	USoundEffectSourcePresetChain* SourceEffectChain = nullptr;

	void UpdateSourceSubmix();
	void TrySpawnSourceSound();
};
