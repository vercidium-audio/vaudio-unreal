#pragma once

#include "CoreMinimal.h"
#include "VAudioEmitterBase.h"
#include "VAudioContinuous.generated.h"

struct VALowPassFilter;

// A raytracing target that plays no sound of its own - the listener casts occlusion/permeation
// rays towards it every frame, producing an up-to-date muffle/EAX result. Attach this to an actor
// (e.g. an enemy) so that many one-shot AVAudioRelativeSources on that actor (footsteps, barks)
// can reuse a single raytrace via GetGroupedEAXIndex()/ApplySourceFilter() instead of each one
// raytracing independently.
//
// AVAudioSource is-a AVAudioContinuous that also plays a sound. Modelling it as a subclass (rather
// than a sibling sharing some intermediate base) means the raytracing-target plumbing - occlusion/
// permeation UPROPERTYs, grouped-EAX submix routing, target-filter application - is written once
// here and AVAudioSource only adds what's unique to owning a SourceSound.
UCLASS(DisplayName = "VAudio Continuous")
class VAUDIOUNREAL_API AVAudioContinuous : public AVAudioEmitterBase
{
	GENERATED_BODY()

public:
	AVAudioContinuous();

protected:
	virtual bool InitializeTypeSpecific() override;
	virtual void DeinitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

public:
	// When true, this emitter's EAX reverb is blended into the world's grouped EAX submixes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	bool bAffectsGroupedEAX = true;

	// The loudest linear volume (0-1) this emitter's dry source will ever be played at by the consuming application.
	// Used to estimate how long the emitter's reverb tail stays audible - a quieter source's reverb tail finishes sooner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb", meta = (EditCondition = "bAffectsGroupedEAX", ClampMin = "0.0", ClampMax = "1.0"))
	float MaxVolume = 1.0f;

	// --- Runtime access ---

	// The world's grouped-EAX submix slot this emitter is currently blended into, or -1 if not
	// yet assigned/AffectsGroupedEAX is false. Used by AVAudioRelativeSource (item 4) to route
	// footsteps/barks attached to this emitter through the same submix.
	int32 GetGroupedEAXIndex() const { return CurrentGroupedEAXIndex; }

	// The listener's most recently raytraced muffling result for this emitter, or nullptr if the
	// listener hasn't raytraced it yet. Used by AVAudioRelativeSource (item 4) to reuse this
	// emitter's muffling instead of raytracing again for every attached one-shot sound.
	VALowPassFilter* GetMufflingResult() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	int32 CurrentGroupedEAXIndex = -1;

	void UpdateGroupedEAXIndex();
};
