#include "VAudioAmbientSource.h"
#include "VAudioWorld.h"
#include "VAudioListener.h"
#include "Kismet/GameplayStatics.h"

extern "C" {
#include "vaudio.h"
}

#include "VaRawLog.h"

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

	TrySpawnSourceSound();
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

void AVAudioAmbientSource::TrySpawnSourceSound()
{
	SourceAudioComponent = UGameplayStatics::SpawnSound2D(GetWorld(), SourceSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->SetLowPassFilterEnabled(true);
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
	if (!SourceAudioComponent || !AudioWorld)
		return;

	AVAudioListener* Listener = AudioWorld->GetMainListener();

	if (!Listener || !Listener->GetVAEmitter())
		return;

	VALowPassFilter* AmbientFilter = vaEmitterGetAmbientFilter(Listener->GetVAEmitter());

	// Raytracing has not completed at least once yet
	if (!AmbientFilter)
		return;

	SourceAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, AmbientFilter->gainHF));
	SourceAudioComponent->SetVolumeMultiplier(AmbientFilter->gainLF);
}
