#pragma once

#include "CoreMinimal.h"
#include "VAudioEmitterBase.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Sound/SoundSubmix.h"
#include "VAudioListener.generated.h"

// Mirrors every field of the SDK's VAEAXReverb for Blueprint consumption
USTRUCT(BlueprintType)
struct FVAEAXReverbResult
{
	GENERATED_BODY()

	// Delay before early reflections are heard, in seconds (0-0.3)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float ReflectionsDelay = 0.0f;

	// Modal density of the late reverberation (0-1)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float Density = 0.0f;

	// Echo diffusion of the late reverberation (0-1)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float Diffusion = 0.0f;

	// Low-frequency gain of the reverb (0-1)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float GainLF = 0.0f;

	// High-frequency gain of the reverb (0-1)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float GainHF = 0.0f;

	// Overall linear gain of the reverb (0-1)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float Gain = 0.0f;

	// Reverberation decay time at mid frequencies, in seconds (0.1-20)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float DecayTime = 0.0f;

	// Ratio of low-frequency decay time to mid-frequency decay time (0.1-2)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float DecayLFRatio = 0.0f;

	// Ratio of high-frequency decay time to mid-frequency decay time (0.1-2)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float DecayHFRatio = 0.0f;

	// Linear gain of early reflections (0-3.16)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float ReflectionsGain = 0.0f;

	// Linear gain of late reverberation (0-10)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float LateReverbGain = 0.0f;

	// Delay of late reverberation relative to early reflections, in seconds (0-0.1)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float LateReverbDelay = 0.0f;

	// Cycling time of the echo effect, in seconds (0.075-0.25)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float EchoTime = 0.0f;

	// Amplitude of the echo effect (0-1)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float EchoDepth = 0.0f;

	// Cycling time of the modulation effect, in seconds (0.04-4)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float ModulationTime = 0.0f;

	// Amplitude of the modulation effect (0-1)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float ModulationDepth = 0.0f;

	// Linear gain applied per meter of distance for high-frequency air absorption (0.892-1)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float AirAbsorptionGainHF = 0.0f;

	// Reference frequency for high-frequency decay ratio, in Hz (1000-20000)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float HFReference = 0.0f;

	// Reference frequency for low-frequency decay ratio, in Hz (20-1000)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float LFReference = 0.0f;

	// Rolloff factor for reflected sound sources (0-10)
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float RoomRolloffFactor = 0.0f;

	// Whether to limit high-frequency decay time to the air absorption limit
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	bool bDecayHFLimit = false;
};

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
	virtual bool ValidateConfig() override;
	virtual void InitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;
	virtual void UpdateVAEmitter() override;

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

	// --- Muffling ---

	// Number of occlusion rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0"))
	int32 OcclusionRayCount = 0;

	// Maximum number of bounces per occlusion ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0"))
	int32 OcclusionBounceCount = 0;

	// Number of permeation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0"))
	int32 PermeationRayCount = 0;

	// Number of bounces per permeation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0"))
	int32 PermeationBounceCount = 0;

	// Energy threshold below which permeation rays are cancelled to prevent unnecessary traversal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.01"))
	float MinimumPermeationEnergy = 0.01f;

	// --- Blueprint results ---

	// Reads this listener's raytraced EAX reverb result. bSuccess is false (and Result is default-constructed)
	// until raytracing has completed at least once
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	void GetReverbResult(bool& bSuccess, FVAEAXReverbResult& Result) const;

	// Reads this listener's raytraced ambient low-pass filter result. bSuccess is false (and GainLF/GainHF
	// are zeroed) until raytracing has completed at least once
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Ambient")
	void GetAmbientFilterResult(bool& bSuccess, float& GainLF, float& GainHF) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// Transient: created via NewObject() in BeginPlay/TryInitializeEmitter and torn down in
	// EndPlay. Must never be serialized - saving the level while this is set (e.g. mid-PIE, or
	// after a crash skips EndPlay) writes it as a real export that doesn't round-trip through a
	// reload and corrupts the package (see FLinkerLoad::CreateExport crash).
	UPROPERTY(Transient)
	USubmixEffectReverbPreset* ListenerReverbPreset = nullptr;

	void ApplyListenerReverb();
};
