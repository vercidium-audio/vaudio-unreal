#include "VASourceLeech.h"
#include "VAEmitter.h"
#include "VAListener.h"
#include "AudioDevice.h"

extern "C" {
#include "vaudio.h"
}

#include "VALog.h"
#include "VASubmixSend.h"

AVASourceLeech::AVASourceLeech()
{
	PrimaryActorTick.bCanEverTick = true;

	SourceRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SourceRootComponent);
}

void AVASourceLeech::BeginPlay()
{
	Super::BeginPlay();

	// Disable the actor if validation fails
	if (SourceSounds.Num() == 0)
	{
		VA_WARN_NAMED(TEXT("Has no SourceSounds and will not play sound"));
		SetActorTickEnabled(false);
		return;
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
			VA_WARN_NAMED(TEXT("SourceSound '%s' must have Virtualization Mode set to 'Play When Silent', else it may not play correctly when fully muffled"), *sound->GetName());

		if (!sound->AttenuationSettings)
			VA_WARN_NAMED(TEXT("SourceSound '%s' has no Sound Attenuation - it will not fall off with distance"), *sound->GetName());
	}

	// The emitter is resolved in Tick, since this actor may be attached to it after spawning
	bSourcePendingSpawn = true;
}

void AVASourceLeech::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;
	}
}

AVAEmitter* AVASourceLeech::GetLeechedEmitter() const
{
	if (AVAEmitter* parent = Cast<AVAEmitter>(GetAttachParentActor()))
		return parent;

	return IsValid(Emitter) ? Emitter : nullptr;
}

void AVASourceLeech::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	AVAEmitter* emitter = GetLeechedEmitter();

	// Keeps the last filter and send while there's no emitter, e.g. after it was destroyed
	if (!emitter)
	{
		if (!bWarnedNoEmitter)
		{
			bWarnedNoEmitter = true;
			VA_WARN_NAMED(TEXT("Is not attached to an AVAEmitter and has no Emitter assigned, so it will not play"));
		}

		return;
	}

	// A listener never raytraces itself, so it has no muffling for a leech to read
	if (emitter->IsA<AVAListener>())
	{
		VA_WARN_NAMED(TEXT("Leeches the listener '%s', which has no muffling result, so it will not play. Use a VASourceRelative instead"), *emitter->GetActorNameOrLabel());
		SetActorTickEnabled(false);
		return;
	}

	bWarnedNoEmitter = false;

	// Null until the listener has raytraced the emitter
	VALowPassFilter* lowPassFilter = emitter->GetMufflingResult();

	if (!lowPassFilter)
		return;

	Filter.Apply(SourceAudioComponent, lowPassFilter->gainLF, lowPassFilter->gainHF);
	UpdateSourceSubmix(emitter);

	// Wait for the emitter's reverb results too, so the sound never starts without reverb
	if (bSourcePendingSpawn && emitter->IsReadyToPlay())
		TrySpawnSourceSound(emitter);
}

void AVASourceLeech::TrySpawnSourceSound(AVAEmitter* emitter)
{
	bSourcePendingSpawn = false;

	USoundBase* chosenSound = SourceSounds[FMath::RandHelper(SourceSounds.Num())];

	// Build the component without starting playback, so the low pass filter and reverb send are set before Play()
	FAudioDevice::FCreateComponentParams Params(GetWorld(), this);
	Params.SetLocation(GetActorLocation());

	SourceAudioComponent = FAudioDevice::CreateComponent(chosenSound, Params);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		SourceAudioComponent->bAllowSpatialization = true;
		SourceAudioComponent->bAutoDestroy = true;

		Filter.Attach(SourceAudioComponent);

		if (ReverbSubmix)
			VASetReverbSend(SourceAudioComponent, ReverbSubmix, ReverbSendLevel);

		SourceAudioComponent->Play();
	}
	else
	{
		VA_WARN_NAMED(TEXT("Play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
	}
}

void AVASourceLeech::UpdateSourceSubmix(AVAEmitter* emitter)
{
	USoundSubmix* submix = nullptr;
	float sendLevel = 0.0f;

	if (!emitter->ResolveReverbSend(submix, sendLevel))
		return;

	// Moved to another submix (e.g. the emitter changed grouped EAX slot), so silence the old send
	if (ReverbSubmix && ReverbSubmix != submix)
		VASetReverbSend(SourceAudioComponent, ReverbSubmix, 0.0f);

	ReverbSubmix = submix;
	ReverbSendLevel = Filter.CompensateReverbSendLevel(sendLevel);

	if (ReverbSubmix)
		VASetReverbSend(SourceAudioComponent, ReverbSubmix, ReverbSendLevel);
}
