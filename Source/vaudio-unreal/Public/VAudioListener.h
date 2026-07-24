#pragma once

#include "CoreMinimal.h"
#include "VAudioEmitterBase.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Sound/SoundSubmix.h"
#include "VAudioListener.generated.h"

// Place exactly one of these in the level - the world's single reference point for directional
// reverb and ambience. Every other raytracing-target actor (AVAudioSource, AVAudioContinuous)
// is added to TargetEmitters so this listener raytraces towards it.
UCLASS(DisplayName = "VAudio Listener")
class VAUDIOUNREAL_API AVAudioListener : public AVAudioEmitterBase
{
	GENERATED_BODY()

public:
	AVAudioListener();

protected:
	virtual void InitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	// Automatically move this emitter (and the VA listener position) to the first player controller's camera every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener")
	bool bAutoFollowCamera = true;

	// This submix applies reverb to sounds created by this listener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener")
	USoundSubmix* ListenerReverbSubmix = nullptr;

	// Target emitters that this listener will cast occlusion and permeation rays towards.
	// Holds both AVAudioSource and AVAudioContinuous actors - both are raytracing targets.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener")
	TArray<AVAudioEmitterBase*> TargetEmitters;

	// --- Reverb ---

	// The lower bound of the relative reverb blend range. This affects the directional reverb that is heard by this listener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RelativeReverbInnerThreshold = 0.6f;

	// The upper bound of the relative reverb blend range. This affects the directional reverb that is heard by this listener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RelativeReverbOuterThreshold = 0.8f;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	bool bTargetsRegistered = false;
	TSet<AVAudioEmitterBase*> RegisteredTargets;

	// Transient: created via NewObject() in BeginPlay/TryInitializeEmitter and torn down in
	// EndPlay. Must never be serialized - saving the level while this is set (e.g. mid-PIE, or
	// after a crash skips EndPlay) writes it as a real export that doesn't round-trip through a
	// reload and corrupts the package (see FLinkerLoad::CreateExport crash).
	UPROPERTY(Transient)
	USubmixEffectReverbPreset* ListenerReverbPreset = nullptr;

	void ApplyListenerReverb();
	void ApplyGroupedEAXReverb();
};
