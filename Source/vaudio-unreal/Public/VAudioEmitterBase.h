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

protected:
	// Called once per subclass after the base VAEmitter* is created and added to the vaWorld,
	// but before AudioWorld->RegisterEmitter(). Subclasses build their own audio components/
	// submix presets here and apply their own vaEmitterSet* calls.
	virtual void InitializeTypeSpecific() {}

	// Called from EndPlay before the VAEmitter* is destroyed and removed from the vaWorld.
	// Subclasses tear down their own audio components/presets here.
	virtual void DeinitializeTypeSpecific() {}

	// Called every Tick() after the shared position sync has run. Subclasses do their own
	// per-frame work here (raytracing target registration, filter application, etc).
	virtual void TickTypeSpecific(float DeltaTime) {}

	// Creates the VA emitter and wires up audio components. Safe to call repeatedly:
	// no-ops (returns true) if already initialized, returns false if AudioWorld's
	// VAWorld isn't ready yet (actor BeginPlay order isn't guaranteed).
	bool TryInitializeEmitter();

	// On-screen warning, keyed by GetUniqueID() so each actor gets its own message slot
	// (see VANonEmitterSourceMessageBase in VADebugMessageKeys.h). Subclasses use this for
	// their own configuration warnings (missing sound file, etc), not just the AudioWorld check below.
	void DisplayWarning(const TCHAR* fmt, ...) const;

	VAEmitter* Emitter = nullptr;

private:
	// Set by AVAudioWorld::RegisterEmitter/UnregisterEmitter - see GetEmitterIndex() above.
	int32 EmitterIndex = -1;
};
