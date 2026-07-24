#include "VAudioAmbientSource.h"
#include "VAudioWorld.h"
#include "VAudioListener.h"
#include "VARawLog.h"
#include "VADebugMessageKeys.h"
#include "VAConstants.h"

#include "Kismet/GameplayStatics.h"

extern "C" {
#include "vaudio.h"
}

void AVAudioAmbientSource::DisplayWarning(const TCHAR* fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	DisplayDebugWarningArgs(VAEmitterMessageBase + GetUniqueID(), fmt, args);
	va_end(args);
}

AVAudioAmbientSource::AVAudioAmbientSource()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AVAudioAmbientSource::BeginPlay()
{
	Super::BeginPlay();

	// Disable the actor if validation fails
	if (!AudioWorld)
	{
		DisplayWarning(TEXT("[VA] AmbientSource '%s' will not play as it does not have an AudioWorld assigned"), *GetActorNameOrLabel());
		SetActorTickEnabled(false);
		return;
	}

	if (!SourceSound)
	{
		DisplayWarning(TEXT("[VA] AmbientSource '%s' will not play as it does not have a sound file assigned"), *GetActorNameOrLabel());
		SetActorTickEnabled(false);
		return;
	}

	AVAudioListener* listener = AudioWorld->GetMainListener();

	if (!listener)
	{
		DisplayWarning(TEXT("[VA] AmbientSource '%s' will not play as the AudioWorld does not have a listener"), *GetActorNameOrLabel());
		SetActorTickEnabled(false);
		return;
	}

	// Warnings
	if (!SourceSound->IsPlayWhenSilent())
	{
		DisplayWarning(TEXT("[VA] AmbientSource '%s': SourceSound '%s' must have Virtualization Mode set to 'Play When Silent', else it may stop playing when fully muffled"), *GetActorNameOrLabel(), *SourceSound->GetName());
	}

	bool occlusionEnabled = listener->AmbientOcclusionRayCount > 0 && listener->AmbientOcclusionBounceCount > 0;
	bool permeationEnabled = listener->AmbientPermeationRayCount > 0 && listener->AmbientPermeationBounceCount > 0;

	// Warn the user that their listener does not have ambient rays
	if (!occlusionEnabled && !permeationEnabled)
	{
		DisplayWarning(TEXT("[VA] AmbientSource '%s' will not be muffled as the listener does not cast ambient occlusion or ambient permeation rays"), *GetActorNameOrLabel());
	}
}

void AVAudioAmbientSource::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;
	}
}

void AVAudioAmbientSource::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// HACK - need to fix the init order madness
	// Bail if the listener failed to initialise
	if (!AudioWorld->GetMainListener() || !AudioWorld->GetMainListener()->GetVAEmitter())
	{
		DisplayWarning(TEXT("[VA] AmbientSource '%s' will not play as the listener failed validation"), *GetActorNameOrLabel());
		return;
	}

	VAEmitter* vaListener = AudioWorld->GetMainListener()->GetVAEmitter();
	VALowPassFilter* AmbientFilter = vaEmitterGetAmbientFilter(vaListener);

	// Raytracing has not completed yet - don't play the sound
	if (!AmbientFilter)
		return;

	if (!SourceAudioComponent)
	{
		// Play the sound
		TrySpawnSourceSound(AmbientFilter);
	}
	else
	{
		// Update the low pass filter
		SourceAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, AmbientFilter->gainHF));
		SourceAudioComponent->SetVolumeMultiplier(AmbientFilter->gainLF);
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
		DisplayWarning(TEXT("[VA] AmbientSource '%s' play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"), *GetActorNameOrLabel());
	}
}
