#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BillboardComponent.h"
#include "VAEmitter.generated.h"

struct VAEmitter;
class AVAWorld;
class UVAVisualisation;
class USoundSubmix;
struct VALowPassFilter;

// Mirrors every field of the SDK's VAEAXReverb for Blueprint consumption, like Godot's get_eax_debug_info
USTRUCT(BlueprintType)
struct FVAEAXReverbResult
{
	GENERATED_BODY()

	// Percentage of reverb ray energy that escaped outside
	UPROPERTY(BlueprintReadOnly, Category = "Vercidium Audio|Reverb")
	float OutsidePercent = 0.0f;

	// Percentage of reverb ray energy that returned to the emitter
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
};

// Broadcast after this emitter casts its rays for the first time (mirrors the SDK's vaEmitterSetOnRaytracingCompleteCallback)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVAOnRaytracingComplete);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVAOnRaytracedByListener, float, GainLF, float, GainHF);

UCLASS(DisplayName = "VAEmitter")
class VAUDIOUNREAL_API AVAEmitter : public AActor
{
	GENERATED_BODY()

public:
	AVAEmitter();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// The world that this emitter belongs to
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	AVAWorld* AudioWorld = nullptr;

	// --- Runtime access ---

	VAEmitter* GetVAEmitter() const { return Emitter; }

	// Reads this emitter's raytraced EAX reverb result. bSuccess is false (and Result is default-constructed) until raytracing has completed at least once
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	void GetReverbResult(bool& bSuccess, FVAEAXReverbResult& Result) const;

	// Reads this emitter's raytraced ambient low-pass filter result. bSuccess is false (and GainLF/GainHF are zeroed) until raytracing has completed at least once
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Ambience")
	void GetAmbientFilterResult(bool& bSuccess, float& GainLF, float& GainHF) const;

	// Reads this emitter's raytraced muffling result. bSuccess is false (and GainLF/GainHF are zeroed) until the listener has raytraced this emitter at least once
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Muffling")
	void GetMufflingFilterResult(bool& bSuccess, float& GainLF, float& GainHF) const;

	VALowPassFilter* GetMufflingResult() const;

	// True once this emitter has cast its reverb rays for the first time
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	bool IsRaytraced() const;

	// True once the listener has raytraced this emitter and, if it casts reverb rays and affects grouped EAX, it has cast its own reverb rays too. Sources don't play until then. Stays true after a bRaytraceOnce emitter leaves the world
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	bool IsReadyToPlay() const;

	// The grouped EAX reverb this emitter contributes to, or -1 if it has none
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	int32 GetGroupedEAXIndex() const;

	// False while this emitter is outside the world's bounds
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	bool GetWithinWorldBounds() const;

	// The position the SDK raytraces from. Differs from the actor location when bClampPosition keeps it inside the world bounds
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	FVector GetVAPosition() const;

	// Mirrors Godot's VAWorld::get_reverb_effect: the grouped EAX submix at its relative gain, else the listener's ListenerReverbSubmix (bUseListenerReverb), else null. Returns false to keep the current send, e.g. while the listener is switching
	bool ResolveReverbSend(USoundSubmix*& OutSubmix, float& OutSendLevel) const;

	// Broadcast after this emitter casts its rays for the first time
	UPROPERTY(BlueprintAssignable, Category = "Vercidium Audio")
	FVAOnRaytracingComplete OnRaytracingComplete;

	// Broadcast when another emitter (typically the listener) raytraces this emitter for the first time, with this emitter's target low-pass filter gains as seen by that emitter
	UPROPERTY(BlueprintAssignable, Category = "Vercidium Audio")
	FVAOnRaytracedByListener OnRaytracedByListener;

	// Creates the VA emitter and wires up audio components. Safe to call repeatedly: no-ops (returns true) if already registered with the world, returns false if AudioWorld isn't assigned or initialisation failed. A listener that isn't current is registered but has no handle
	bool TryInitializeEmitter();

	// Called from the SDK callbacks during vaWorldUpdate. The Blueprint delegates are broadcast later by FlushPendingEvents, after vaWorldUpdate returns, so a handler that spawns or destroys emitters can't re-enter the SDK
	void QueueRaytracingComplete();
	void QueueRaytracedByListener(float gainLF, float gainHF);
	void FlushPendingEvents();

	// Called from the OnRemoved callback once the SDK has removed handle from the world. The world destroys it later
	void OnEmitterRemoved(VAEmitter* handle);

	// Removes this emitter from the world once the listener has raytraced it, so it stops casting rays. The actor keeps its last muffling result
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	bool bRaytraceOnce = false;

	// --- Reverb ---

	// Number of reverb rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0"))
	int32 ReverbRayCount = 0;

	// Number of bounces per reverb ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0"))
	int32 ReverbBounceCount = 0;

	// The percentage of returning energy required for reverb to be at full volume
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float ReverbEnergyCap = 0.15f;

	// Energy threshold below which reverb rays stop bouncing to prevent unnecessary traversal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.001"))
	float MinimumReverbEnergy = 0.01f;

	// The loudest linear volume (0-1) this emitter's dry source will ever be played at. Used to estimate how long its reverb tail stays audible, as a quieter source's reverb tail finishes sooner
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxVolume = 1.0f;

	// How long (in milliseconds) the echogram records data for. Returning reverb rays after this period will be ignored
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "1"))
	int32 MaxEchogramTime = 5000;

	// The length (in milliseconds) of each entry in the echogram
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (ClampMin = "1"))
	int32 EchogramGranularity = 100;

	// When true, this emitter's EAX reverb is blended into the world's grouped EAX submixes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	bool bAffectsGroupedEAX = false;

	// When true, this emitter is kept alive after being removed from the world while its reverb tail continues to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (EditCondition = "bAffectsGroupedEAX"))
	bool bKeepReverbTailAlive = true;

	// When true, grouped EAX reverbs calculate the direction and gain they're heard from relative to this emitter. Listeners enable this
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	bool bHasRelativeReverb = false;

	// When bAffectsGroupedEAX is false, this emitter uses the current listener's ListenerReverbSubmix if this is true, and has no reverb if it's false
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	bool bUseListenerReverb = false;

	// --- Muffling ---

	// Percentage of occlusion energy required for this emitter to be at full volume. Defaults to 15% of the other emitter's OcclusionRayCount
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", UIMax = "1.0", Delta = "0.001"))
	float OcclusionEnergyCap = 0.15f;

	// Percentage of permeation energy required for this emitter to be at full volume. Defaults to 15% of the other emitter's PermeationRayCount * PermeationBounceCount
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", UIMax = "1.0", Delta = "0.001"))
	float PermeationEnergyCap = 0.15f;

	// --- Ambience ---

	// Number of ambient occlusion rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambience", meta = (ClampMin = "0"))
	int32 AmbientOcclusionRayCount = 0;

	// Maximum number of bounces per ambient occlusion ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambience", meta = (ClampMin = "0"))
	int32 AmbientOcclusionBounceCount = 0;

	// Percentage of ambient occlusion energy required for the emitter to be at full volume
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambience", meta = (ClampMin = "0.0", UIMax = "1.0", Delta = "0.001"))
	float AmbientOcclusionEnergyCap = 0.15f;

	// Low-frequency energy threshold below which ambient occlusion rays stop bouncing to prevent unnecessary traversal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambience", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.001"))
	float MinimumAmbientOcclusionEnergy = 0.01f;

	// Number of ambient permeation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambience", meta = (ClampMin = "0"))
	int32 AmbientPermeationRayCount = 0;

	// Maximum number of bounces per ambient permeation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambience", meta = (ClampMin = "0"))
	int32 AmbientPermeationBounceCount = 0;

	// Percentage of ambient permeation energy required for the emitter to be at full volume
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambience", meta = (ClampMin = "0.0", UIMax = "1.0", Delta = "0.001"))
	float AmbientPermeationEnergyCap = 0.15f;

	// Energy threshold below which ambient permeation rays are cancelled to prevent unnecessary traversal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambience", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.001"))
	float MinimumAmbientPermeationEnergy = 0.01f;

	// --- Advanced ---

	// User-defined integer tag for categorising emitters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced")
	int32 Type = 0;

	// Controls the number of trails that are 'refreshed' each frame. Refreshing a trail involves re-casting the first ray, and if it hits a different position than last time, the entire trail will be trimmed and recalculated. See RefreshDistanceThreshold for the allowed distance between old and new bounce positions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced", meta = (ClampMin = "0"))
	int32 TrailRefreshCount = 16;

	// The allowed distance between new and old bounce positions when refreshing trails. See TrailRefreshCount for more information.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced", meta = (ClampMin = "0.0"))
	float RefreshDistanceThreshold = 1.0f;

	// Seed used to randomise scattering vectors. Each new emitter gets a random seed
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced")
	int32 ScatteringSeed = 0;

	// Whether this emitter's position is clamped to world bounds, to prevent going out of bounds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Advanced")
	bool bClampPosition = true;

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

	// --- Visualisation ---

	// Set by the UVAVisualisation on this actor while it receives visualisation data
	UPROPERTY(Transient)
	TObjectPtr<UVAVisualisation> VisualisationComponent = nullptr;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual bool ValidateConfig() { return true; }

	virtual void InitializeTypeSpecific();

	// Called from EndPlay before the VAEmitter* is destroyed and removed from the vaWorld. Subclasses tear down their own audio components/presets here
	virtual void DeinitializeTypeSpecific();

	// Called every Tick() after the shared position sync has run. Subclasses do their own per-frame work here (filter application, etc). Still runs with no handle after a bRaytraceOnce emitter has left the world
	virtual void TickTypeSpecific(float DeltaTime);

	// Pushes every property to the SDK handle
	virtual void UpdateVAEmitter();

	// Creates the SDK handle and adds it to the world. Listeners override these, as every listener in a world shares one handle
	virtual bool AttachToWorld();
	virtual void DetachFromWorld();

	// Creates a handle with this actor's callbacks. Properties aren't pushed yet
	void CreateEmitter();

	// Points a handle's user data, name and position at this actor
	void AdoptEmitter(VAEmitter* handle);

	// For a handle that was never added to the world
	void DestroyUnaddedEmitter();

	bool IsRegistered() const { return registered; }

	VAEmitter* Emitter = nullptr;

	// Updated every tick while bAffectsGroupedEAX is set
	int32 CurrentGroupedEAXIndex = -1;

private:
	bool registered = false;
	bool failedInitialisation = false;

	// bRaytraceOnce: set when the listener raytraces this emitter, and the emitter leaves the world at the end of the first Tick where IsReadyToPlay() is true
	bool pendingRaytraceOnceRelease = false;
	bool raytraceOnceReleased = false;

	bool pendingRaytracingComplete = false;
	bool pendingRaytracedByListener = false;
	float pendingGainLF = 0.0f;
	float pendingGainHF = 0.0f;

	// Keeps the last muffling result after a bRaytraceOnce emitter leaves the world
	bool hasLastMufflingResult = false;
	float lastMufflingGainLF = 0.0f;
	float lastMufflingGainHF = 0.0f;

	// Removes the emitter from the world and hands its handle to the world, which destroys it once the SDK has let go of it
	void ReleaseEmitter();
};
