#pragma once

#include "CoreMinimal.h"
#include "VAEmitter.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Sound/SoundSubmix.h"
#include "VAListener.generated.h"

UCLASS(DisplayName = "VAListener")
class VAUDIOUNREAL_API AVAListener : public AVAEmitter
{
	GENERATED_BODY()

public:
	AVAListener();

protected:
	virtual void InitializeTypeSpecific() override;
	virtual void DeinitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;
	virtual void UpdateVAEmitter() override;
	virtual bool AttachToWorld() override;
	virtual void DetachFromWorld() override;

public:
	// Every listener in a world shares one SDK emitter, which is controlled by the current listener. Only one listener is current at a time. A listener that begins play with this enabled takes over from the current one
	UPROPERTY(EditAnywhere, BlueprintGetter = IsCurrent, BlueprintSetter = SetCurrent, Category = "Vercidium Audio|Listener")
	bool bCurrent = false;

	UFUNCTION(BlueprintGetter)
	bool IsCurrent() const { return bCurrent; }

	// Disabling current hands the shared emitter to another listener. The only listener in a world stays current
	UFUNCTION(BlueprintSetter)
	void SetCurrent(bool value);

	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Listener")
	void MakeCurrent();

	// Called by AVAWorld. Takes over the shared handle from the previous listener, or creates it if this is the first listener in the world. Targets stay connected to the handle
	bool Activate(VAEmitter* sharedHandle);
	void Deactivate();

	// Automatically move this emitter (and the VA listener position) to the first player controller's camera every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener")
	bool bAutoFollowCamera = true;

	// This submix applies reverb to sounds created by this listener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Listener")
	USoundSubmix* ListenerReverbSubmix = nullptr;

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

	// Low-frequency energy threshold below which occlusion rays stop bouncing to prevent unnecessary traversal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.01"))
	float MinimumOcclusionEnergy = 0.01f;

	// Number of permeation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0"))
	int32 PermeationRayCount = 0;

	// Number of bounces per permeation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0"))
	int32 PermeationBounceCount = 0;

	// Energy threshold below which permeation rays are cancelled to prevent unnecessary traversal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Muffling", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.01"))
	float MinimumPermeationEnergy = 0.01f;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	// Called by AVAWorld for every non-listener emitter, so this listener calculates how muffled it is
	void AddTarget(AVAEmitter* target);

	// Called by AVAWorld from the SDK's OnReverbUpdated callback while this listener is current
	void ApplyListenerReverb();

private:
	UPROPERTY(Transient)
	USubmixEffectReverbPreset* ListenerReverbPreset = nullptr;

	bool warnedNoTargetRays = false;
};
