#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BillboardComponent.h"
#include "VAudioEmitterBase.generated.h"

struct VAEmitter;
class AVAudioWorld;

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
	float ReverbEnergyCap = 0.2f;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AmbientOcclusionEnergyCap = 0.5f;

	// Number of ambient permeation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0"))
	int32 AmbientPermeationRayCount = 0;

	// Maximum number of bounces per ambient permeation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0"))
	int32 AmbientPermeationBounceCount = 0;

	// Percentage of ambient permeation energy required for the emitter to be at full volume
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Ambient", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AmbientPermeationEnergyCap = 0.5f;

	// --- Refresh ---

	// Number of trails rebuilt from scratch each frame to prevent staleness when the emitter moves
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Refresh", meta = (ClampMin = "0"))
	int32 RefreshRayCount = 0;

	// A ray trail will be re-created if an old ray bounce position is too far away from the new ray bounce position. This setting controls the allowed distance between old and new ray bounce positions
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Refresh", meta = (ClampMin = "0.0"))
	float RefreshDistanceThreshold = 1.0f;

	// --- Visualisation ---

	// Number of visualisation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 VisualisationRayCount = 0;

	// Number of bounces per visualisation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 VisualisationBounceCount = 0;

	// How often to cast visualisation rays (milliseconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "1"))
	int32 VisualisationUpdateFrequency = 500;

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

protected:
	// Called once per subclass after the base VAEmitter* is created and added to the vaWorld,
	// but before AudioWorld->RegisterEmitter(). Subclasses build their own audio components/
	// submix presets here and apply their own vaEmitterSet* calls.
	virtual bool InitializeTypeSpecific() { return false; }

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
