#include "VAudioRelativeSource.h"
#include "VAudioEmitterBase.h"
#include "VAudioListener.h"
#include "VAudioContinuous.h"
#include "VAudioWorld.h"
#include "Kismet/GameplayStatics.h"

extern "C" {
#include "vaudio.h"
}

#include "VARawLog.h"
#include "VADebugMessageKeys.h"

const float MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
const float MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;

void AVAudioRelativeSource::DisplayWarning(const TCHAR* fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	DisplayDebugWarningArgs(VAEmitterMessageBase + GetUniqueID(), fmt, args);
	va_end(args);
}

AVAudioRelativeSource::AVAudioRelativeSource()
{
	PrimaryActorTick.bCanEverTick = true;

	SourceRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SourceRootComponent);
}

void AVAudioRelativeSource::BeginPlay()
{
	Super::BeginPlay();

	if (SourceSounds.Num() == 0)
	{
		DisplayWarning(TEXT("[VA] RelativeSource '%s' has no SourceSounds and will play nothing"), *GetActorNameOrLabel());
		return;
	}

	ListenerEmitter = Cast<AVAudioListener>(ReverbSource);
	ContinuousEmitter = Cast<AVAudioContinuous>(ReverbSource);

	for (int32 i = 0; i < SourceSounds.Num(); i++)
	{
		USoundBase* sound = SourceSounds[i];

		if (!sound)
		{
			DisplayWarning(TEXT("[VA] RelativeSource '%s': SourceSounds has an invalid sound assigned to index %d"), *GetActorNameOrLabel(), i);
			break;
		}

		if (!sound->IsPlayWhenSilent())
		{
			DisplayWarning(TEXT("[VA] RelativeSource '%s': SourceSound '%s' must have Virtualization Mode set to 'Play When Silent', else it may not play correctly when fully muffled"), *GetActorNameOrLabel(), *sound->GetName());
			break;
		}

		// If assigned to a Continuous emitter, it must be spatialised
		if (ContinuousEmitter && !SourceAudioComponent->AttenuationSettings)
		{
			DisplayWarning(TEXT("[VA] RelativeSource '%s': SourceSound has no Sound Attenuation - it will not fall off with distance"), *GetActorNameOrLabel(), *sound->GetName());
		}
	}

	if (!ListenerEmitter && !ContinuousEmitter)
	{
		DisplayWarning(TEXT("[VA] RelativeSource '%s': ReverbSource is not assigned to an AVAudioListener or AVAudioContinuous, meaning this sound will have no reverb or muffling."), *GetActorNameOrLabel());
	}

	if (bAttachToSelf && !GetRootComponent())
	{
		DisplayWarning(TEXT("[VA] RelativeSource '%s' has Attach To Self = true, but has no root component. Assign this RelativeSource to an actor"), *GetActorNameOrLabel());
	}

	if (ListenerEmitter && !ListenerEmitter->ListenerReverbSubmix)
	{
		DisplayWarning(TEXT("[VA] RelativeSource '%s' will have no reverb as the Listener has no reverb submix"), *GetActorNameOrLabel());
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
	}
}

void AVAudioRelativeSource::TrySpawnSourceSound()
{
	USoundBase* ChosenSound = SourceSounds[FMath::RandHelper(SourceSounds.Num())];

	// User assigned an invalid sound to the array. This is already logged above in BeginPlay()
	if (!ChosenSound)
		return;

	// TODO - rename 'Self' to something that makes more sense
	if (bAttachToSelf)
	{
		if (!GetRootComponent())
			return;

		SourceAudioComponent = UGameplayStatics::SpawnSoundAttached(ChosenSound, GetRootComponent(), NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, false, 1.0f, 1.0f, 0.0f, nullptr, nullptr, true);
	}
	else
	{
		SourceAudioComponent = UGameplayStatics::SpawnSound2D(GetWorld(), ChosenSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);
	}

	if (SourceAudioComponent)
	{
		SourceAudioComponent->SetLowPassFilterEnabled(true);
	}
	else
	{
		// TODO - why did it fail to play a sound? We've already checked the sounds in BeginPlay() above. Any other config we didn't check?
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

	if (ListenerEmitter)
	{
		VAEmitter* vaEmitter = ListenerEmitter->GetVAEmitter();

		// TODO - why could this be null?
		if (!vaEmitter)
			return;

		// Already logged above if ListenerReverbSubmix is null
		if (ListenerEmitter->ListenerReverbSubmix)
			SourceAudioComponent->SetSubmixSend(ListenerEmitter->ListenerReverbSubmix, 1.0f);
	}
	else if (ContinuousEmitter)
	{
		VALowPassFilter* vaLowPassFilter = ContinuousEmitter->GetMufflingResult();

		// Continuous target hasn't been raytraced by the listener yet
		if (!vaLowPassFilter)
			return;

		SourceAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, vaLowPassFilter->gainHF));
		SourceAudioComponent->SetVolumeMultiplier(vaLowPassFilter->gainLF);

		// Apply the continuous emitter's grouped EAX reverb to this sound
		if (ContinuousEmitter->AudioWorld)
		{
			if (USoundSubmix* Submix = ContinuousEmitter->AudioWorld->GetGroupedEAXSubmix(ContinuousEmitter->GetGroupedEAXIndex()))
				SourceAudioComponent->SetSubmixSend(Submix, 1.0f);
		}
	}
}
