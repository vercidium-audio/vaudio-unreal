#pragma once

#include "CoreMinimal.h"
#include "VAEmitter.h"
#include "VASourcePlayback.h"
#include "VARaytracedSource.generated.h"

// Base for sources that are raytraced emitters themselves (VASource and VAStreamSource). Owns the playback, and pushes this emitter's muffling, multipliers and reverb send to it every tick
UCLASS(Abstract, DisplayName = "VARaytracedSource")
class VAUDIOUNREAL_API AVARaytracedSource : public AVAEmitter
{
	GENERATED_BODY()

public:
	AVARaytracedSource();

protected:
	virtual void DeinitializeTypeSpecific() override;
	virtual void TickTypeSpecific(float DeltaTime) override;

	// Pushes the muffling result, multipliers and reverb send to Playback
	void UpdatePlayback();

	UPROPERTY(Transient)
	FVASourcePlayback Playback;

public:
	// Multiplies the volume set by muffling. The reverb send is scaled by it too
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source", meta = (ClampMin = "0.01", UIMax = "4.0"))
	float PitchMultiplier = 1.0f;

	// Stops every sound this source is playing
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio|Source")
	virtual void Stop();

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Source")
	bool IsPlaying() const { return Playback.IsPlaying(); }

	void SetDryOutputEnabled(bool bEnabled) { Playback.SetDryOutputEnabled(bEnabled); }

	const FVASourcePlayback& GetPlayback() const { return Playback; }

	// The submix this source's reverb is sent to: its grouped EAX submix, the current listener's ListenerReverbSubmix (bUseListenerReverb), or null. Resolved every tick, even with no audio device
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	USoundSubmix* GetReverbSubmix() const { return Playback.GetReverbSubmix(); }

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Reverb")
	float GetReverbSendLevel() const { return Playback.GetReverbSendLevel(); }
};
