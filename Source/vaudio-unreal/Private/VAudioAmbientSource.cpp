#include "VAudioAmbientSource.h"
#include "VAudioWorld.h"
#include "VAudioListener.h"
#include "Kismet/GameplayStatics.h"

extern "C" {
#include "vaudio.h"
}

#include "VaRawLog.h"
#include "VADebugMessageKeys.h"

const float MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
const float MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;

AVAudioAmbientSource::AVAudioAmbientSource()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AVAudioAmbientSource::BeginPlay()
{
	Super::BeginPlay();

	if (!SourceSound)
	{
		VALog(L"AmbientSource has no SourceSound assigned - it will do nothing");
		return;
	}

	if (!AudioWorld)
	{
		VALog(L"AudioWorld is not assigned - this ambient source will play with no filtering. Assign AudioWorld in the Details panel.");
	}
}

void AVAudioAmbientSource::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;

		VALog(L"Stopped ambient source sound");
	}
}

void AVAudioAmbientSource::TrySpawnSourceSound(const VALowPassFilter* AmbientFilter)
{
	// CreateSound2D() builds the component without starting playback, so we can configure the low pass filter before playing any sound
	SourceAudioComponent = UGameplayStatics::CreateSound2D(GetWorld(), SourceSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->SetLowPassFilterEnabled(true);
		SourceAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, AmbientFilter->gainHF));
		SourceAudioComponent->SetVolumeMultiplier(AmbientFilter->gainLF);
		SourceAudioComponent->Play();
	}
	else
	{
		VALog(L"Failed to play sound");
	}
}

void AVAudioAmbientSource::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ApplyAmbientFilter();
}

void AVAudioAmbientSource::ApplyAmbientFilter()
{
	if (!AudioWorld || !SourceSound)
		return;

	// This actor can start SourceSound at 0 volume (gainLF starts at 0) and ramp it up later as the
	// ambient filter changes - if SourceSound isn't set to play when silent, Unreal can decide the
	// sound is inaudible at spawn and never actually start it, so raising the volume later does
	// nothing. See TrySpawnSourceSound() above. Checked every tick (rather than once in BeginPlay())
	// so the on-screen warning doesn't expire after one frame - AddOnScreenDebugMessage's
	// TimeToDisplay of 0.0f means "reissue every tick to keep it alive", not "show forever".
	if (!SourceSound->IsPlayWhenSilent())
	{
		uint64 messageID = VANonEmitterSourceMessageBase + GetUniqueID();
		GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] AmbientSource '%s': SourceSound '%s' must have Virtualization Mode = 'Play When Silent', else it may stop playing when fully muffled"), *GetActorNameOrLabel(), *SourceSound->GetName()));
	}

	AVAudioListener* Listener = AudioWorld->GetMainListener();

	if (!Listener || !Listener->GetVAEmitter())
		return;

	VALowPassFilter* AmbientFilter = vaEmitterGetAmbientFilter(Listener->GetVAEmitter());

	// Raytracing has not completed at least once yet - don't spawn until there's a real filter to
	// start from, otherwise the sound would be briefly audible unfiltered/at full volume.
	if (!AmbientFilter)
		return;

	if (!SourceAudioComponent)
	{
		// Applies AmbientFilter and starts playback itself, so the very first sample is already
		// filtered - see TrySpawnSourceSound()'s comment.
		TrySpawnSourceSound(AmbientFilter);
		return;
	}

	SourceAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, AmbientFilter->gainHF));
	SourceAudioComponent->SetVolumeMultiplier(AmbientFilter->gainLF);
}
