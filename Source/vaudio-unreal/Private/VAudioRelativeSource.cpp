#include "VAudioRelativeSource.h"
#include "VAudioEmitterBase.h"
#include "VAudioListener.h"
#include "VAudioContinuous.h"
#include "VAudioWorld.h"
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

	SourceRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SourceRootComponent);
}

void AVAudioRelativeSource::BeginPlay()
{
	Super::BeginPlay();

	if (SourceSounds.Num() == 0)
	{
		VALog(L"RelativeSource has no SourceSounds assigned - it will do nothing");
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
	USoundBase* ChosenSound = SourceSounds[FMath::RandHelper(SourceSounds.Num())];

	if (!ChosenSound)
	{
		VALog(L"Failed to play sound - SourceSounds contains a null entry at the chosen index");
		return;
	}

	if (bAttachToSelf)
	{
		if (!GetRootComponent())
		{
			VALog(L"Failed to play sound - RelativeSource has no root component to attach to");
			return;
		}

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
		VALog(L"Failed to play sound - %s returned null for SourceSound '%s' (invalid/empty sound asset?)", bAttachToSelf ? L"SpawnSoundAttached" : L"SpawnSound2D", *GetNameSafe(ChosenSound));
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

		// The dry signal shaping above doesn't put any of the sound into the listener's reverb
		// submix - without this send, ListenerReverbSubmix's reverb effect never receives audio
		// and this source is heard fully dry regardless of ListenerReverbSubmix being configured.
		if (ListenerReverbSource->ListenerReverbSubmix)
			SourceAudioComponent->SetSubmixSend(ListenerReverbSource->ListenerReverbSubmix, 1.0f);
	}
	else if (ContinuousReverbSource)
	{
		VALowPassFilter* MufflingResult = ContinuousReverbSource->GetMufflingResult();

		// Continuous target hasn't been raytraced by the listener yet
		if (!MufflingResult)
			return;

		SourceAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, MufflingResult->gainHF));
		SourceAudioComponent->SetVolumeMultiplier(MufflingResult->gainLF);

		// Send to the same grouped-EAX submix the continuous emitter is already blended into, so
		// this source shares its reverb rather than playing fully dry (see the ListenerReverbSource
		// branch above for why the send is needed).
		if (ContinuousReverbSource->AudioWorld)
		{
			if (USoundSubmix* Submix = ContinuousReverbSource->AudioWorld->GetGroupedEAXSubmix(ContinuousReverbSource->GetGroupedEAXIndex()))
				SourceAudioComponent->SetSubmixSend(Submix, 1.0f);
		}
	}
}
