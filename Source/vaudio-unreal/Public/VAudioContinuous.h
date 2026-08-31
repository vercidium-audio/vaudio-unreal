#pragma once

#include "CoreMinimal.h"
#include "VAudioEmitterBase.h"
#include "VAudioContinuous.generated.h"

struct VALowPassFilter;

UCLASS(DisplayName = "VAudio Continuous")
class VAUDIOUNREAL_API AVAudioContinuous : public AVAudioEmitterBase
{
	GENERATED_BODY()

public:
	AVAudioContinuous();

protected:
	virtual void InitializeTypeSpecific() override;
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

	int32 GetGroupedEAXIndex() const { return CurrentGroupedEAXIndex; }

	VALowPassFilter* GetMufflingResult() const;

	// Reads this emitter's raytraced muffling result. bSuccess is false (and GainLF/GainHF are zeroed) until the listener has raytraced this emitter at least once
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Muffling")
	void GetMufflingFilterResult(bool& bSuccess, float& GainLF, float& GainHF) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	int32 CurrentGroupedEAXIndex = -1;

	void UpdateGroupedEAXIndex();
};
