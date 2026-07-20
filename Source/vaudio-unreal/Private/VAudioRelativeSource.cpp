#include "VAudioRelativeSource.h"
#include "VAudioEmitterBase.h"
#include "VAudioListener.h"
#include "VAudioContinuous.h"
#include "Kismet/GameplayStatics.h"

extern "C" {
#include "vaudio.h"
}

#include "VaRawLog.h"

const float MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
const float MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;

AVAudioRelativeSource::AVAudioRelativeSource()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AVAudioRelativeSource::BeginPlay()
{
	Super::BeginPlay();

	if (!SourceSound)
	{
		VALog(L"RelativeSource has no SourceSound assigned - it will do nothing");
		return;
	}

	ListenerReverbSource = Cast<AVAudioListener>(ReverbSource);
	ContinuousReverbSource = Cast<AVAudioContinuous>(ReverbSource);

	if (!ListenerReverbSource && !ContinuousReverbSource)
	{
		VALog(L"ReverbSource is not assigned to an AVAudioListener or AVAudioContinuous - this relative source will play with no reverb/muffling. Assign ReverbSource in the Details panel.");
	}

	TrySpawnSourceSound();
}

void AVAudioRelativeSource::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;

		VALog(L"Stopped relative source sound");
	}
}

void AVAudioRelativeSource::TrySpawnSourceSound()
{
	if (bAttachToSelf)
	{
		SourceAudioComponent = UGameplayStatics::SpawnSoundAttached(SourceSound, GetRootComponent(), NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, false, 1.0f, 1.0f, 0.0f, nullptr, nullptr, true);
	}
	else
	{
		SourceAudioComponent = UGameplayStatics::SpawnSound2D(GetWorld(), SourceSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);
	}

	if (SourceAudioComponent)
	{
		SourceAudioComponent->SetLowPassFilterEnabled(true);
	}
	else
	{
		VALog(L"Failed to play sound");
	}
}

void AVAudioRelativeSource::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ApplyReverbSource();
}

void AVAudioRelativeSource::ApplyReverbSource()
{
	if (!SourceAudioComponent)
		return;

	if (ListenerReverbSource)
	{
		// Same mechanism as AVAudioListener::ApplyListenerReverb(), but applied as a per-source
		// low pass filter/volume (this actor has no reverb submix of its own to route through).
		VAEmitter* ListenerEmitter = ListenerReverbSource->GetVAEmitter();

		if (!ListenerEmitter)
			return;

		VAEAXReverb* EAX = vaEmitterGetEAX(ListenerEmitter);

		// Raytracing has not completed at least once yet
		if (!EAX)
			return;

		SourceAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, EAX->gainHF));
		SourceAudioComponent->SetVolumeMultiplier(EAX->gainLF);
	}
	else if (ContinuousReverbSource)
	{
		VALowPassFilter* MufflingResult = ContinuousReverbSource->GetMufflingResult();

		// Continuous target hasn't been raytraced by the listener yet
		if (!MufflingResult)
			return;

		SourceAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, MufflingResult->gainHF));
		SourceAudioComponent->SetVolumeMultiplier(MufflingResult->gainLF);
	}
}
