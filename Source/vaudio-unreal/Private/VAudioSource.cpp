#include "VAudioSource.h"
#include "VAudioWorld.h"
#include "VAudioListener.h"
#include "ActiveSound.h"
#include "AudioDevice.h"

extern "C" {
#include "vaudio.h"
}

#include "VARawLog.h"
#include "VADebugMessageKeys.h"
#include "VAConstants.h"

const float LOW_PASS_RESONANCE = 0.707f; // Butterworth Q constant - maximally flat passband, no resonant peak at the cutoff

AVAudioSource::AVAudioSource()
{
}

void AVAudioSource::InitializeTypeSpecific()
{
	Super::InitializeTypeSpecific();

	if (!SourceSound)
	{
		VALog(L"Source has no SourceSound assigned - it will do nothing");
		return;
	}

	if (bAffectsGroupedEAX && (ReverbRayCount == 0 || ReverbBounceCount == 0))
	{
		DisplayWarning(TEXT("[VA] Source '%s' has affectsGroupedEAX=true, but does not cast reverb rays"), *GetActorNameOrLabel());
	}

	// Build the source effect chain (LPF only on the dry path; reverb submix taps the pre-effect signal).
	SourceLPFPreset = NewObject<USourceEffectFilterPreset>(this);

	FSourceEffectFilterSettings LPFSettings;
	LPFSettings.FilterCircuit    = ESourceEffectFilterCircuit::StateVariable;
	LPFSettings.FilterType       = ESourceEffectFilterType::LowPass;
	LPFSettings.CutoffFrequency  = MAX_LOW_PASS_CUTOFF_FREQUENCY;
	LPFSettings.FilterQ          = LOW_PASS_RESONANCE;
	SourceLPFPreset->SetSettings(LPFSettings);

	SourceEffectChain = NewObject<USoundEffectSourcePresetChain>(this);
	FSourceEffectChainEntry ChainEntry;
	ChainEntry.Preset = SourceLPFPreset;
	ChainEntry.bBypass = false;
	SourceEffectChain->Chain.Add(ChainEntry);

	// Spawn is deferred to Tick()/TrySpawnSourceSound() until the main listener has raytraced
	// this emitter at least once - otherwise the sound starts clear (LPF fully open) and pops
	// to muffled a few frames later once the first real filter result arrives.
	bSourcePendingSpawn = true;
}

void AVAudioSource::DeinitializeTypeSpecific()
{
	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;

		VALog(L"Stopped source sound");
	}

	// No need to separately zero the submix send gain: Stop() above tears down the audio
	// component (SpawnSound* defaults to bAutoDestroy), which releases its submix send too.
	Super::DeinitializeTypeSpecific();
}

void AVAudioSource::TickTypeSpecific(float DeltaTime)
{
	Super::TickTypeSpecific(DeltaTime);

	if (bSourcePendingSpawn)
		TrySpawnSourceSound();

	if (bAffectsGroupedEAX)
		UpdateSourceSubmix();

	// ApplySourceFilter() can drive this sound's volume down to 0 (occluded) and back up later as
	// the emitter's raytraced gainLF changes - if SourceSound isn't set to play when silent, Unreal
	// can decide the sound is inaudible at 0 volume and never resume it. Checked every tick (rather
	// than once in TrySpawnSourceSound()) so the on-screen warning doesn't expire after one frame -
	// AddOnScreenDebugMessage's TimeToDisplay of 0.0f means "reissue every tick to keep it alive".
	if (SourceSound && !SourceSound->IsPlayWhenSilent())
	{
		uint64 messageID = VAEmitterMessageBase + GetEmitterIndex() * VAEmitterMessageStride + VAEmitterVirtualizationStatus;
		GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source '%s': SourceSound '%s' must have Virtualization Mode = 'Play When Silent', else it may stop playing when fully muffled"), *GetActorNameOrLabel(), *SourceSound->GetName()));
	}
}

void AVAudioSource::TrySpawnSourceSound()
{
	// This plugin currently assumes a single main listener - if that ever changes, spawn
	// readiness would need to consider raytrace state from every listener targeting this emitter.
	AVAudioListener* Listener = AudioWorld ? AudioWorld->GetMainListener() : nullptr;

	// Listener may not have registered yet (actor init order isn't guaranteed) - retry next Tick.
	if (!Listener || !Listener->GetVAEmitter())
		return;

	// Wait until the listener has raytraced this emitter at least once, so the LPF has a real
	// gain value to start from instead of momentarily playing at full volume/unfiltered.
	if (!vaEmitterHasRaytracedTarget(Listener->GetVAEmitter(), Emitter))
		return;

	bSourcePendingSpawn = false;

	// UGameplayStatics::SpawnSoundAtLocation() calls AudioComponent->Play() internally before
	// returning, and Play() snapshots the component's SourceEffectChain into the FActiveSound at
	// that moment (see UAudioComponent::PlayInternal() -> FActiveSound::SetSourceEffectChain()) -
	// it's never re-read afterwards. So SetSourceEffectChain() has to happen before Play() or the
	// LPF preset is never actually wired into the playing sound. FAudioDevice::CreateComponent()
	// is what SpawnSoundAtLocation() uses internally to build the component without playing it
	// (Params.bPlay defaults to false), which lets us assign the effect chain first.
	FAudioDevice::FCreateComponentParams Params(GetWorld(), this);
	Params.SetLocation(GetActorLocation());

	SourceAudioComponent = FAudioDevice::CreateComponent(SourceSound, Params);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->SetWorldLocationAndRotation(GetActorLocation(), FRotator::ZeroRotator);
		SourceAudioComponent->SetVolumeMultiplier(1.0f);
		SourceAudioComponent->SetPitchMultiplier(1.0f);
		SourceAudioComponent->bAllowSpatialization = Params.ShouldUseAttenuation();

		// bAutoDestroy, not bLooping: UAudioComponent has no per-instance loop override - looping
		// is inherent to the SourceSound asset (USoundWave::bLooping / a Sound Cue's Looping node).
		// bAutoDestroy only controls whether the component tears itself down once the sound finishes,
		// so a one-shot (!bLooping) should auto-destroy, while a looping sound never finishes on its
		// own and shouldn't be auto-destroyed out from under itself.
		SourceAudioComponent->bAutoDestroy = !bLooping;
		SourceAudioComponent->bStopWhenOwnerDestroyed = false;
		SourceAudioComponent->SetSourceEffectChain(SourceEffectChain);
		SourceAudioComponent->Play();

		if (!SourceAudioComponent->AttenuationSettings)
		{
			uint64 messageID = VAEmitterMessageBase + GetEmitterIndex() * VAEmitterMessageStride + VAEmitterAttenuationStatus;
			GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source '%s' has no Sound Attenuation - it will not fall off with distance"), *GetActorNameOrLabel()));
		}

		// Prime the filter immediately so the very first played frame already reflects the
		// raytraced result rather than the MAX_LOW_PASS_CUTOFF_FREQUENCY default.
		VALowPassFilter* lowPassFilter = vaEmitterGetTargetFilter(Listener->GetVAEmitter(), Emitter);

		if (ensureMsgf(lowPassFilter, TEXT("VAudioSource '%s': vaEmitterGetTargetFilter() returned null despite vaEmitterHasRaytracedTarget() being true"), *GetActorNameOrLabel()))
			ApplySourceFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);

		// Submix assignment is deferred to Tick once the SDK assigns a groupedEAXIndex.
		VALog(L"Low pass filter created and sound played");
	}
	else
	{
		VALog(L"Failed to play sound");
	}
}

void AVAudioSource::ApplySourceFilter(float GainLF, float GainHF)
{
	// GainLF/GainHF come from VALowPassFilter, documented in vaudio.h as always in [0, 1] -
	// this is a public entry point though (also reachable from Blueprints), so guard against misuse.
	ensureMsgf(GainLF >= 0.0f && GainLF <= 1.0f, TEXT("VAudioSource::ApplySourceFilter: GainLF %f out of range [0,1]"), GainLF);
	ensureMsgf(GainHF >= 0.0f && GainHF <= 1.0f, TEXT("VAudioSource::ApplySourceFilter: GainHF %f out of range [0,1]"), GainHF);

	// SourceAudioComponent/SourceLPFPreset are only created in InitializeTypeSpecific() when
	// SourceSound is assigned - null here just means this emitter has no source sound to filter.
	if (!SourceAudioComponent || !SourceLPFPreset)
		return;

	FSourceEffectFilterSettings settings;
	settings.FilterCircuit   = ESourceEffectFilterCircuit::StateVariable;
	settings.FilterType      = ESourceEffectFilterType::LowPass;
	settings.CutoffFrequency = FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, GainHF);
	settings.FilterQ		 = LOW_PASS_RESONANCE;
	SourceLPFPreset->SetSettings(settings);

	SourceAudioComponent->SetVolumeMultiplier(GainLF);
}

void AVAudioSource::UpdateSourceSubmix()
{
	// SourceAudioComponent is only spawned when SourceSound is assigned; AudioWorld/Emitter are
	// only null before TryInitializeEmitter() has completed (see BeginPlay/Tick) - all are normal
	// transient states, not errors.
	if (!SourceAudioComponent || !AudioWorld || !Emitter)
		return;

	int32 GroupedEAXIndex = GetGroupedEAXIndex();

	if (GroupedEAXIndex < 0)
		return;

	USoundSubmix* Submix = AudioWorld->GetGroupedEAXSubmix(GroupedEAXIndex);

	// AVAudioWorld always sets the SDK's maximumGroupedEAXCount to at least 2 (see
	// AVAudioWorld::BeginPlay), even if GroupedEAXSubmixes has fewer than 2 entries - so the SDK
	// can hand back an index for which there's no configured submix. Warn so the user knows to add
	// more entries to GroupedEAXSubmixes on the VAudioWorld actor.
	if (!Submix)
	{
		VALog(L"no submix configured at GroupedEAX index %d - add more entries to GroupedEAXSubmixes on the VAudioWorld actor (needs at least 2).", GroupedEAXIndex);
		return;
	}

	// Default send level is overridden below if the listener has relative reverb data
	float SendLevel = 1.0f;

	VAWorld* vaWorld = AudioWorld->GetVAWorld();
	AVAudioListener* Listener = AudioWorld->GetMainListener();

	if (!vaWorld)
	{
		VALog(L"Unable to access grouped EAX data as vaWorld is null");
	}
	// Wait for raytracing to run at least once
	else if (!vaWorldGetInitialising(vaWorld))
	{
		const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(vaWorld);

		if (!ensureMsgf(GroupedEAX, TEXT("VAudioSource '%s': vaWorldGetGroupedEAX() returned null after reverb was calculated - vaWorldSetMaximumGroupedEAXCount() is always called with >= 2 (see AVAudioWorld::BeginPlay), so this shouldn't happen"), *GetActorNameOrLabel()))
		{
			VALog(L"Reverb is calculated but vaWorldGetGroupedEAX() returned null");
		}
		else if (GroupedEAX[GroupedEAXIndex])
		{
			const VAEAXReverb* EAX = GroupedEAX[GroupedEAXIndex];

			if (!Listener)
			{
				VALog(L"Unable to access relative EAX gain as Listener is null");
			}
			else
			{
				VAEmitter* ListenerVA = Listener->GetVAEmitter();

				// UE-LIMITATION - only relative gain is supported. Can't do directional reverb
				float* Gain = vaEAXReverbGetRelativeGain(EAX, ListenerVA);

				if (Gain)
				{
					// Assert gain is >= 0 and <= 1
					SendLevel = *Gain;
				}
			}
		}
		else
		{
			VALog(L"Reverb is calculated but GroupedEAX[GroupedEAXIndex] is null");
		}
	}
	else
	{
		VALog(L"Unable to access grouped EAX data as vaWorldGetReverbCalculated() is false");
	}

	// TODO - can we control the overall send level of the submix, rather than filtering how much each sound sends to the submix?
	SourceAudioComponent->SetSubmixSend(Submix, SendLevel);
}

static void SetAudioComponentDryOutputEnabled(UAudioComponent* Comp, bool bEnabled)
{
	// Comp is SourceAudioComponent, which is only spawned if SourceSound is assigned
	// (see AVAudioSource::SetDryOutputEnabled) - null here just means it isn't in use.
	if (!Comp) return;

	FAudioDevice* AudioDevice = Comp->GetAudioDevice();

	// No active audio device (e.g. audio disabled, or the component's sound already stopped) - nothing to update.
	if (!AudioDevice) return;

	uint64 AudioComponentID = Comp->GetAudioComponentID();
	AudioDevice->SendCommandToActiveSounds(AudioComponentID, [bEnabled](FActiveSound& ActiveSound)
	{
		// Kill/restore the master submix output without touching submix send routing.
		// This lets reverb submix sends stay alive while silencing the dry signal.
		ActiveSound.bHasActiveMainSubmixOutputOverride = true;
		ActiveSound.bEnableMainSubmixOutputOverride = bEnabled;
	});
}

void AVAudioSource::SetDryOutputEnabled(bool bEnabled)
{
	if (bEnabled == bCurrentDryEnabled) return;
	bCurrentDryEnabled = bEnabled;
	SetAudioComponentDryOutputEnabled(SourceAudioComponent, bEnabled);
}
