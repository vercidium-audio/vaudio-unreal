#include "VAudioRelativeSource.h"
#include "VAudioEmitterBase.h"
#include "VAudioListener.h"
#include "VAudioContinuous.h"
#include "VAudioWorld.h"
#include "AudioDevice.h"

extern "C" {
#include "vaudio.h"
}

#include "VARawLog.h"
#include "VADebugMessageKeys.h"
#include "VAConstants.h"

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
		if (ContinuousEmitter && !sound->AttenuationSettings)
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

	// If attached to a ContinuousEmitter, the low pass filter must be primed from that emitter's
	// muffling result before Play() - Tick()/ApplyReverbSource() defers spawning until that result
	// is available. Otherwise (Listener or misconfigured) there is nothing to wait for.
	bSourcePendingSpawn = true;

	if (!ContinuousEmitter)
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
	// If attached to a ContinuousEmitter, wait until it has it has been raytraced before playing a sound
	VALowPassFilter* vaLowPassFilter = nullptr;

	if (ContinuousEmitter)
	{
		vaLowPassFilter = ContinuousEmitter->GetMufflingResult();

		if (!vaLowPassFilter)
			return;
	}

	bSourcePendingSpawn = false;

	USoundBase* ChosenSound = SourceSounds[FMath::RandHelper(SourceSounds.Num())];

	// User assigned an invalid sound to the array. This is already logged above in BeginPlay()
	if (!ChosenSound)
		return;

	// Build the component without starting playback, so the low pass filter can be configured before Play()
	FAudioDevice::FCreateComponentParams Params(GetWorld(), this);

	// TODO - rename 'Self' to something that makes more sense
	if (bAttachToSelf)
	{
		if (!GetRootComponent())
			return;

		Params.SetLocation(GetRootComponent()->GetComponentLocation());
	}

	SourceAudioComponent = FAudioDevice::CreateComponent(ChosenSound, Params);

	if (SourceAudioComponent)
	{
		if (bAttachToSelf)
			SourceAudioComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

		SourceAudioComponent->bAutoDestroy = true;
		SourceAudioComponent->SetLowPassFilterEnabled(true);

		if (vaLowPassFilter)
		{
			SourceAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, vaLowPassFilter->gainHF));
			SourceAudioComponent->SetVolumeMultiplier(vaLowPassFilter->gainLF);
		}

		SourceAudioComponent->Play();
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
	if (bSourcePendingSpawn)
		TrySpawnSourceSound();

	if (!SourceAudioComponent)
		return;

	if (ListenerEmitter)
	{
		VAEmitter* vaEmitter = ListenerEmitter->GetVAEmitter();

		// This is null if the listener was not assigned a World
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
