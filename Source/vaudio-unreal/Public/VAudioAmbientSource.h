#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "VAudioAmbientSource.generated.h"

class AVAudioWorld;
struct VALowPassFilter;

// For rain/wind/room-tone sounds. NOT raytraced, NO reverb - plays SourceSound in 2D and applies
// a low-pass filter driven by the main listener's ambient filter (vaEmitterGetAmbientFilter()),
// same mechanism as AVAudioListener::ApplyAmbientFilter(). Independent instances of this actor
// are how multiple ambient sounds are supported, unlike AVAudioListener's single WIP AmbientSound.
//
// This does not inherit AVAudioEmitterBase and creates no VAEmitter* of its own: it has no
// occlusion/permeation/reverb settings and never raytraces - it only reads a filter already
// computed against the listener's own VAEmitter, so there is nothing for a VAEmitter* of its
// own to do. It is a thin UE audio component wrapper, same reasoning as AVAudioRelativeSource.
UCLASS(DisplayName = "VAudio Ambient Source")
class VAUDIOUNREAL_API AVAudioAmbientSource : public AActor
{
	GENERATED_BODY()

public:
	AVAudioAmbientSource();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// The world whose main listener's ambient filter this source reads from
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	AVAudioWorld* AudioWorld = nullptr;

	// The sound file to play (2D - rain/wind/room-tone has no meaningful position)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Source")
	USoundBase* SourceSound = nullptr;

	UPROPERTY(Transient)
	UAudioComponent* SourceAudioComponent = nullptr;

private:
	void TrySpawnSourceSound(const VALowPassFilter* AmbientFilter);
	void ApplyAmbientFilter();
	void DisplayWarning(const TCHAR* fmt, ...) const;
};
