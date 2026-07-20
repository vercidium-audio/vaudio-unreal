#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "VAudioRelativeSource.generated.h"

class AVAudioEmitterBase;
class AVAudioListener;
class AVAudioContinuous;

// For footsteps/gunshots/one-shot sounds that should reuse another emitter's already-computed
// reverb/muffling rather than raytracing independently. NOT a raytracing target itself - it is
// never added to a listener's TargetEmitters and has no occlusion/permeation UPROPERTYs.
//
// ReverbSource resolves the reverb/muffling to reuse:
//  - AVAudioListener: this actor's own non-directional reverb (vaEmitterGetEAX), same mechanism
//    as AVAudioListener::ApplyListenerReverb(). Use for sounds relative to the player (footsteps).
//  - AVAudioContinuous: that emitter's already-raytraced muffling result
//    (AVAudioContinuous::GetMufflingResult()), so many relative sounds on one actor (e.g. an
//    enemy's footsteps and barks) share a single raytrace instead of each running its own.
//
// This does not inherit AVAudioEmitterBase and creates no VAEmitter*: everything it reads
// (vaEmitterGetEAX on the listener, GetMufflingResult() on a continuous target) is already
// computed by the SDK against the ReverbSource's own emitter, and this actor skips raytracing
// entirely - so there is nothing for a VAEmitter* of its own to do. It is a thin UE audio
// component wrapper that reads the ReverbSource's current result and applies it every tick.
UCLASS(DisplayName = "VAudio Relative Source")
class VAUDIOUNREAL_API AVAudioRelativeSource : public AActor
{
	GENERATED_BODY()

public:
	AVAudioRelativeSource();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// The sound file to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	// Whether the sound is spawned attached to this actor (following its position) or as a plain
	// 2D sound. Attached playback still has no directionality/attenuation of its own - VA-driven
	// reverb/muffling here is always non-directional, matching today's ApplyListenerReverb().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bAttachToSelf = true;

	// Either an AVAudioListener (reuse its own non-directional reverb) or an AVAudioContinuous
	// (reuse that emitter's already-raytraced muffling result). Validated on BeginPlay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	AVAudioEmitterBase* ReverbSource = nullptr;

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

private:
	// This actor has no VAEmitter* of its own (see class comment) but still needs a root
	// component so bAttachToSelf spawning has something to attach the audio component to.
	UPROPERTY(VisibleAnywhere, Category = "Vercidium Audio|Source")
	USceneComponent* SourceRootComponent = nullptr;

	// Cached downcasts of ReverbSource, resolved once at BeginPlay - exactly one of these is set
	// after a valid BeginPlay (or both null if ReverbSource was misconfigured).
	UPROPERTY(Transient)
	AVAudioListener* ListenerReverbSource = nullptr;

	UPROPERTY(Transient)
	AVAudioContinuous* ContinuousReverbSource = nullptr;

	void TrySpawnSourceSound();
	void ApplyReverbSource();
};
