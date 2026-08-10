#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BillboardComponent.h"
#include "VAudioEmitterBase.generated.h"

struct VAEmitter;
class AVAudioWorld;
class UVAudioVisualisationComponent;

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

// Broadcast after this emitter casts its rays for the first time (mirrors the SDK's
// vaEmitterSetOnRaytracingCompleteCallback)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVAOnRaytracingComplete);

// Broadcast when another emitter (typically the listener) raytraces this emitter for the first
// time. GainLF/GainHF are this emitter's target low-pass filter as seen by that other emitter
// (mirrors the SDK's vaEmitterSetOnRaytracedByAnotherEmitterCallback)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVAOnRaytracedByListener, float, GainLF, float, GainHF);

// Common base for every VA actor type (listener, source, relative source, ambient source).
// Owns the VAEmitter* lifecycle, position sync, and AVAudioWorld registration - the plumbing
// shared regardless of role. Subclasses fill in InitializeTypeSpecific()/DeinitializeTypeSpecific()
// for their own setup and override TickTypeSpecific() for their own per-frame behaviour.
UCLASS(Abstract)
class VAUDIOUNREAL_API AVAudioEmitterBase : public AActor
{
	GENERATED_BODY()

public:
	AVAudioEmitterBase();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// The world that this emitter belongs to
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	AVAudioWorld* AudioWorld = nullptr;

	// --- Runtime access ---

	VAEmitter* GetVAEmitter() const { return Emitter; }

	// Reads this emitter's raytraced EAX reverb result. bSuccess is false (and Result is default-constructed)
	// until raytracing has completed at least once
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	void GetReverbResult(bool& bSuccess, FVAEAXReverbResult& Result) const;

	// Reads this emitter's raytraced ambient low-pass filter result. bSuccess is false (and GainLF/GainHF
	// are zeroed) until raytracing has completed at least once
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Ambient")
	void GetAmbientFilterResult(bool& bSuccess, float& GainLF, float& GainHF) const;

	// Broadcast after this emitter casts its rays for the first time
	UPROPERTY(BlueprintAssignable, Category = "Vercidium Audio")
	FVAOnRaytracingComplete OnRaytracingComplete;

	// Broadcast when another emitter (typically the listener) raytraces this emitter for the
	// first time, with this emitter's target low-pass filter gains as seen by that emitter
	UPROPERTY(BlueprintAssignable, Category = "Vercidium Audio")
	FVAOnRaytracedByListener OnRaytracedByListener;

	// This emitter's index within its AVAudioWorld's RegisteredEmitters, assigned by
	// AVAudioWorld::RegisterEmitter/UnregisterEmitter. Used to build collision-free
	// GEngine->AddOnScreenDebugMessage keys - see VADebugMessageKeys.h.
	int32 GetEmitterIndex() const { return EmitterIndex; }
	void SetEmitterIndex(int32 Index) { EmitterIndex = Index; }

	// Creates the VA emitter and wires up audio components. Safe to call repeatedly:
	// no-ops (returns true) if already initialized, returns false if AudioWorld isn't assigned
	bool TryInitializeEmitter();

	// --- Reverb ---

	// Number of reverb rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0"))
	int32 ReverbRayCount = 0;

	// Number of bounces per reverb ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0"))
	int32 ReverbBounceCount = 0;

	// The percentage of returning energy required for reverb to be at maximum volume
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReverbEnergyCap = 0.15f;

	// How long (in milliseconds) the echogram records data for. Returning reverb rays after this period will be ignored
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "1"))
	int32 MaxEchogramTime = 5000;

	// The length (in milliseconds) of each entry in the echogram
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "1"))
	int32 EchogramGranularity = 200;

	// --- Muffling ---

	// Percentage of occlusion energy required for this emitter to be at full volume. Defaults to 15% of the other emitter's OcclusionRayCount
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OcclusionEnergyCap = 0.15f;

	// Percentage of permeation energy required for this emitter to be at full volume. Defaults to 15% of the other emitter's PermeationRayCount * PermeationBounceCount
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PermeationEnergyCap = 0.15f;

	// --- Ambient ---

	// Number of ambient occlusion rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0"))
	int32 AmbientOcclusionRayCount = 0;

	// Maximum number of bounces per ambient occlusion ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0"))
	int32 AmbientOcclusionBounceCount = 0;

	// Percentage of ambient occlusion energy required for the emitter to be at full volume.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AmbientOcclusionEnergyCap = 0.5f;

	// Number of ambient permeation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0"))
	int32 AmbientPermeationRayCount = 0;

	// Maximum number of bounces per ambient permeation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0"))
	int32 AmbientPermeationBounceCount = 0;

	// Percentage of ambient permeation energy required for the emitter to be at full volume
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AmbientPermeationEnergyCap = 0.5f;

	// --- Refresh ---

	// Number of trails rebuilt from scratch each frame to prevent staleness when the emitter moves
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Refresh", meta = (ClampMin = "0"))
	int32 RefreshRayCount = 0;

	// A ray trail will be re-created if an old ray bounce position is too far away from the new ray bounce position. This setting controls the allowed distance between old and new ray bounce positions
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Refresh", meta = (ClampMin = "0.0"))
	float RefreshDistanceThreshold = 1.0f;

	// --- Visualisation ---

	// Set by UVAudioVisualisationComponent::BeginPlay/EndPlay when one is attached, so the
	// visualisation callback trampoline (VAudioVisualisationComponent.cpp) can resolve it from the
	// VAEmitter* alone via vaEmitterGetUserData, the same way the other callback trampolines do.
	UVAudioVisualisationComponent* VisualisationComponent = nullptr;

	// --- Advanced ---

	// User-defined integer tag for categorising emitters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced")
	int32 EmitterType = 0;

	// Whether this emitter's position is clamped to world bounds, to prevent going out of bounds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced")
	bool bClampPosition = false;

	// Seed used to randomise scattering vectors
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced")
	int32 ScatteringSeed = 0;

	// --- Debug Rendering ---
	// Editor/debug-window visualisation only - no effect on raytracing.

	// Whether to render each trail a different colour in the debug window
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug Rendering")
	bool bRandomTrailColor = false;

	// Colour of ray trails in the debug window
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug Rendering")
	FColor TrailColor = FColor(255, 255, 255, 25);

	// Colour of reverb rays in the debug window
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug Rendering")
	FColor ReverbColor = FColor(27, 247, 255, 51);

	// Colour of occlusion rays in the debug window
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug Rendering")
	FColor OcclusionColor = FColor(113, 255, 164, 51);

	// Colour of permeation rays in the debug window
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug Rendering")
	FColor PermeationColor = FColor(255, 127, 42, 51);

	// Colour of ambient permeation rays in the debug window
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug Rendering")
	FColor AmbientPermeationColor = FColor(255, 204, 0, 51);

protected:
	virtual bool ValidateConfig() { return true; }

	// Called once per subclass after the base VAEmitter* is created and added to the vaWorld,
	// but before AudioWorld->RegisterEmitter(). Subclasses build their own audio components/
	// submix presets here and apply their own vaEmitterSet* calls.
	virtual void InitializeTypeSpecific() { }

	// Called from EndPlay before the VAEmitter* is destroyed and removed from the vaWorld.
	// Subclasses tear down their own audio components/presets here.
	virtual void DeinitializeTypeSpecific() {}

	// Called every Tick() after the shared position sync has run. Subclasses do their own
	// per-frame work here (raytracing target registration, filter application, etc).
	virtual void TickTypeSpecific(float DeltaTime) {}


	// On-screen warning, keyed by GetUniqueID() so each actor gets its own message slot
	// (see VANonEmitterSourceMessageBase in VADebugMessageKeys.h). Subclasses use this for
	// their own configuration warnings (missing sound file, etc), not just the AudioWorld check below.
	void DisplayWarning(const TCHAR* fmt, ...) const;
	void ClearWarning() const;

	// Pushes the ray-related UPROPERTYs above onto Emitter. Subclasses call this from
	// InitializeTypeSpecific() and (if WITH_EDITOR) PostEditChangeProperty(), and may override
	// it (calling Super::UpdateVAEmitter() first) to push their own additional
	// vaEmitterSet* calls at the same two call sites - see AVAudioListener.
	virtual void UpdateVAEmitter();

	VAEmitter* Emitter = nullptr;

private:
	// Set by AVAudioWorld::RegisterEmitter/UnregisterEmitter - see GetEmitterIndex() above.
	int32 EmitterIndex = -1;
	bool registered = false;
	bool failedInitialisation = false;
};
