#include "VASourceRelative.h"
#include "VAEmitterBase.h"
#include "VAListener.h"
#include "VAEmitter.h"
#include "VAWorld.h"
#include "AudioDevice.h"

extern "C" {
#include "vaudio.h"
}

#include "VALog.h"
#include "VAConstants.h"

AVASourceRelative::AVASourceRelative()
{
	PrimaryActorTick.bCanEverTick = true;

	SourceRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SourceRootComponent);
}

void AVASourceRelative::BeginPlay()
{
	Super::BeginPlay();

	// Disable the actor if validation fails
	if (SourceSounds.Num() == 0)
	{
		VA_WARN_NAMED(TEXT("Has no SourceSounds and will not play sound"));
		SetActorTickEnabled(false);
		return;
	}

	ListenerEmitter = Cast<AVAListener>(ReverbSource);
	ContinuousEmitter = Cast<AVAEmitter>(ReverbSource);

	if (ListenerEmitter)
	{
		if (!ListenerEmitter->ListenerReverbSubmix)
		{
			VA_WARN_NAMED(TEXT("Will have no reverb as the Listener has no reverb submix"));
		}
	}

	for (int32 i = 0; i < SourceSounds.Num(); i++)
	{
		USoundBase* sound = SourceSounds[i];

		if (!sound)
		{
			VA_WARN_NAMED(TEXT("Will not play as it has a null sound assigned to index %d"), i);
			SetActorTickEnabled(false);
			return;
		}

		if (!sound->IsPlayWhenSilent())
		{
			VA_WARN_NAMED(TEXT("SourceSound '%s' must have Virtualization Mode set to 'Play When Silent', else it may not play correctly when fully muffled"), *sound->GetName());
			break;
		}

		// If assigned to a Continuous emitter, it must be spatialised
		if (ContinuousEmitter && !sound->AttenuationSettings)
		{
			VA_WARN_NAMED(TEXT("SourceSound '%s' has no Sound Attenuation - it will not fall off with distance"), *sound->GetName());
		}
	}

	if (!ListenerEmitter && !ContinuousEmitter)
	{
		VA_WARN_NAMED(TEXT("ReverbSource is not assigned to an AVAListener or AVAEmitter, meaning this sound will have no reverb or muffling."));
	}

	if (bAttachToSelf && !GetRootComponent())
	{
		VA_WARN_NAMED(TEXT("Will not play as it has AttachToSelf = true, but has no root component. Assign this RelativeSource to an actor"));
		SetActorTickEnabled(false);
		return;
	}

	Filter.Initialize(this);
	bSourcePendingSpawn = true;

	if (!ContinuousEmitter)
		TrySpawnSourceSound();
}

void AVASourceRelative::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;
	}
}

void AVASourceRelative::TrySpawnSourceSound()
{
	// If attached to a ContinuousEmitter, wait until it has it has been raytraced before playing a sound
	VALowPassFilter* vaLowPassFilter = nullptr;

	if (ContinuousEmitter)
	{
		vaLowPassFilter = ContinuousEmitter->GetMufflingResult();

		// Wait for raytracing to complete
		if (!vaLowPassFilter)
			return;
	}
	else
	{
		// TODO - wait for listener to raytrace once and have valid EAX?
	}

	bSourcePendingSpawn = false;

	USoundBase* ChosenSound = SourceSounds[FMath::RandHelper(SourceSounds.Num())];

	// Build the component without starting playback, so the low pass filter can be configured before Play()
	FAudioDevice::FCreateComponentParams Params(GetWorld(), this);

	// TODO - rename 'Self' to something that makes more sense
	if (bAttachToSelf)
	{
		Params.SetLocation(GetRootComponent()->GetComponentLocation());
	}

	SourceAudioComponent = FAudioDevice::CreateComponent(ChosenSound, Params);

	if (SourceAudioComponent)
	{
		if (bAttachToSelf)
			SourceAudioComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

		SourceAudioComponent->bAutoDestroy = true;

		if (vaLowPassFilter)
			Filter.Apply(nullptr, vaLowPassFilter->gainLF, vaLowPassFilter->gainHF);

		Filter.Attach(SourceAudioComponent);
		SourceAudioComponent->Play();
	}
	else
	{
		VA_WARN_NAMED(TEXT("Play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
	}
}

void AVASourceRelative::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ApplyReverbSource();
}

void AVASourceRelative::ApplyReverbSource()
{
	if (bSourcePendingSpawn)
		TrySpawnSourceSound();

	// TODO - line 198, SourceAudioComponent was null
	if (!SourceAudioComponent)
		return;

	if (ListenerEmitter)
	{
		VAEmitter* vaEmitter = ListenerEmitter->GetVAEmitter();

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

		Filter.Apply(SourceAudioComponent, vaLowPassFilter->gainLF, vaLowPassFilter->gainHF);

		// Apply the continuous emitter's grouped EAX reverb to this sound
		if (ContinuousEmitter->AudioWorld)
		{
			if (USoundSubmix* Submix = ContinuousEmitter->AudioWorld->GetGroupedEAXSubmix(ContinuousEmitter->GetGroupedEAXIndex()))
				SourceAudioComponent->SetSubmixSend(Submix, 1.0f);
		}
	}
}
