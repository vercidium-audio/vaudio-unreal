// Human-created

#pragma once

#include "CoreMinimal.h"
#include "VAEAXReverbResult.generated.h"

// Mirrors every field of the SDK's VAEAXReverb for use in Blueprints
USTRUCT(BlueprintType)
struct FVAEAXReverbResult
{
	GENERATED_BODY()

	// Percentage of ambient occlusion energy that reached the edge of the world
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float OutsidePercent = 0.0f;

	// Percentage of reverb energy that returned to the emitter
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float ReturnedPercent = 0.0f;

	// Average low-frequency absorption of all surfaces hit by reverb rays
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float MaterialAbsorptionLF = 0.0f;

	// Average high-frequency absorption of all surfaces hit by reverb rays
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float MaterialAbsorptionHF = 0.0f;

	// Average roughness of all surfaces hit by reverb rays
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float MaterialRoughness = 0.0f;

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

	// TODO - add new FMOD variables here (`HighCut`, `LowShelfGain`, `WetLevel` and `EarlyLateMix`)
};
