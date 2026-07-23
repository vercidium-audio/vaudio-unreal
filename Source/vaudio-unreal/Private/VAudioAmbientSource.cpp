#include "VAudioAmbientSource.h"
#include "VAudioWorld.h"
#include "VAudioListener.h"
#include "Kismet/GameplayStatics.h"

extern "C" {
#include "vaudio.h"
}

#include "VaRawLog.h"
#include "VADebugMessageKeys.h"
#include <HAL/Platform.h>
#include <Math/Color.h>
#include <Math/UnrealMathUtility.h>
#include <Misc/CString.h>
#include <Templates/UnrealTemplate.h>
#include <Engine/Engine.h>
#include <Engine/EngineTypes.h>
#include <cstdarg>
#include <VAudioEmitterBase.h>

const float MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
const float MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;

void AVAudioAmbientSource::DisplayWarning(const TCHAR* fmt, ...) const
{
	if (!GEngine)
		return;

	// Format the string
	va_list args;
	va_start(args, fmt);
	TCHAR buffer[1024];
	FCString::GetVarArgs(buffer, UE_ARRAY_COUNT(buffer), fmt, args);
	va_end(args);

	uint64 messageID = VANonEmitterSourceMessageBase + GetUniqueID();
	GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, buffer);
}

AVAudioAmbientSource::AVAudioAmbientSource()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AVAudioAmbientSource::BeginPlay()
{
	Super::BeginPlay();

	if (!AudioWorld)
	{
		DisplayWarning(TEXT("[VA] AmbientSource '%s' does not have an AudioWorld assigned and will not play"), *GetActorNameOrLabel());
		return;
	}

	if (!SourceSound)
	{
		DisplayWarning(TEXT("[VA] AmbientSource '%s' does not have a sound file assigned and will not play"), *GetActorNameOrLabel());
		return;
	}

	// If VirtualisationMode is not 'Play When Silent', and the sound starts with 0 LF gain, it won't play when LF gain increases later
	if (!SourceSound->IsPlayWhenSilent())
	{
		DisplayWarning(TEXT("[VA] AmbientSource '%s': SourceSound '%s' must have Virtualization Mode set to 'Play When Silent', else it may stop playing when fully muffled"), *GetActorNameOrLabel(), *SourceSound->GetName());
		return;
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
		// TODO - why could SourceAudioComponent be null?
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
	// Not configured correctly - do not play
	if (!AudioWorld || !SourceSound)
		return;

	AVAudioListener* Listener = AudioWorld->GetMainListener();

	// User did not add a VAudioListener actor to this world
	if (!Listener)
		return;

	VAEmitter* vaListener = Listener->GetVAEmitter();

	// TODO - unsure how this could be null
	if (!vaListener)
		return;

	// Warn the user that their listener does not have ambient rays
	if (!vaEmitterGetAmbientOcclusionEnabled(vaListener) && !vaEmitterGetAmbientOcclusionEnabled(vaListener))
	{
		GEngine->AddOnScreenDebugMessage(VAListenerNoAmbientRaysMessage, 0.0f, FColor::Orange,
			FString::Printf(TEXT("[VA] AmbientSource '%s' will not be muffled as the listener does not cast ambient occlusion or ambient permeation rays"), *GetActorNameOrLabel()));
	}

	VALowPassFilter* AmbientFilter = vaEmitterGetAmbientFilter(vaListener);

	// Raytracing has not completed yet - don't play the sound
	if (!AmbientFilter)
		return;

	if (!SourceAudioComponent)
	{
		TrySpawnSourceSound(AmbientFilter);
	}
	else
	{
		SourceAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, AmbientFilter->gainHF));
		SourceAudioComponent->SetVolumeMultiplier(AmbientFilter->gainLF);
	}
}
