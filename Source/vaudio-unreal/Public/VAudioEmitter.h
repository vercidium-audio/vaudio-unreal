#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "Components/BillboardComponent.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectFilter.h"
#include "VAudioEmitter.generated.h"

struct VAEmitter;
class AVAudioWorld;

// Place this actor in the level for each audio source (or the player listener).
// Assign the VAudioWorld reference and tune per-emitter ray settings in the Details panel.
UCLASS(DisplayName = "VA Audio Emitter")
class VAUDIOUNREAL_API AVAudioEmitter : public AActor
{
	GENERATED_BODY()

public:
	AVAudioEmitter();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// --- World reference ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	AVAudioWorld* AudioWorld = nullptr;

	// --- Listener ---

	// When true this emitter tracks the player camera each tick and drives
	// reverb / ambient filtering on the assigned submix and ambient audio component.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener")
	bool bIsMainListener = false;

	// --- Source reverb ---

	// When true, this emitter's EAX is blended into the world's grouped EAX zones.
	// The SDK assigns a groupedEAXIndex; that index selects which submix receives the reverb.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (EditCondition = "!bIsMainListener"))
	bool bAffectsGroupedEAX = true;

	// Submix that receives listener-relative reverb (ambience, footsteps, gunshots, etc). Driven by this emitter's own EAX.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener", meta = (EditCondition = "bIsMainListener"))
	USoundSubmix* ListenerReverbSubmix = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener", meta = (EditCondition = "bIsMainListener"))
	USoundBase* AmbientSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener", meta = (EditCondition = "bIsMainListener"))
	bool bAmbientThroughReverb = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener", meta = (EditCondition = "bIsMainListener"))
	TArray<AVAudioEmitter*> TargetEmitters;

	// --- Source ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (EditCondition = "!bIsMainListener"))
	USoundBase* SourceSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (EditCondition = "!bIsMainListener"))
	bool bLooping = true;

	// --- Reverb rays ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	int32 ReverbRayCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	int32 ReverbBounceCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	float ReverbEnergyCap = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	int32 MaxEchogramTime = 5000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	int32 EchogramGranularity = 50;

	// --- Occlusion rays ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Occlusion")
	int32 OcclusionRayCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Occlusion")
	int32 OcclusionBounceCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Occlusion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OcclusionEnergyCap = 0.15f;

	// --- Permeation rays ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Permeation")
	int32 PermeationRayCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Permeation")
	int32 PermeationBounceCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Permeation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PermeationEnergyCap = 0.15f;

	// --- Refresh ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Refresh")
	int32 RefreshRayCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Refresh")
	float RefreshDistanceThreshold = 0.1f;

	// --- Ambient ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient")
	int32 AmbientOcclusionRayCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient")
	int32 AmbientOcclusionBounceCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient")
	float AmbientOcclusionEnergyCap = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient")
	int32 AmbientPermeationRayCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient")
	int32 AmbientPermeationBounceCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient")
	float AmbientPermeationEnergyCap = 0.5f;

	// --- Runtime access ---

	VAEmitter* GetVAEmitter() const { return Emitter; }
	void ApplySourceFilter(float GainLF, float GainHF);
	void SetDryOutputEnabled(bool bEnabled);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	VAEmitter* Emitter = nullptr;

	UPROPERTY()
	UAudioComponent* AmbientAudioComponent = nullptr;

	UPROPERTY()
	UAudioComponent* SourceAudioComponent = nullptr;

	int32 CurrentGroupedEAXIndex = -1;
	bool bTargetsRegistered = false;
	bool bCurrentDryEnabled = true;

	UPROPERTY()
	USubmixEffectReverbPreset* ListenerReverbPreset = nullptr;

	UPROPERTY()
	USourceEffectFilterPreset* SourceLPFPreset = nullptr;

	UPROPERTY()
	USoundEffectSourcePresetChain* SourceEffectChain = nullptr;

	void ApplyListenerReverb();
	void ApplyGroupedEAXReverb();
	void ApplyAmbientFilter();
	void UpdateSourceSubmix();
};
