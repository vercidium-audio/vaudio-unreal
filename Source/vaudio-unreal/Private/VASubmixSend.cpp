#include "VASubmixSend.h"
#include "ActiveSound.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundSubmix.h"
#include "AudioMixerDevice.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AudioThread.h"
#include "Engine/World.h"
#include "VAEngineCompat.h"

// 4.27 has no FAudioDevice::SendCommandToActiveSounds, so do what its UAudioComponent::SetSubmixSend does: find the component's active sound on the audio thread
static void VASendToActiveSounds(FAudioDevice* AudioDevice, uint64 AudioComponentID, TFunction<void(FActiveSound&)> Command)
{
#if UE_VERSION_OLDER_THAN(5, 0, 0)
	FAudioThread::RunCommandOnAudioThread([AudioDevice, AudioComponentID, Command = MoveTemp(Command)]()
	{
		if (FActiveSound* ActiveSound = AudioDevice->FindActiveSound(AudioComponentID))
			Command(*ActiveSound);
	});
#else
	AudioDevice->SendCommandToActiveSounds(AudioComponentID, MoveTemp(Command));
#endif
}

void VASetReverbSend(UAudioComponent* Component, USoundSubmix* Submix, float SendLevel)
{
	// Sound not played yet - still waiting for raytracing
	if (!Component)
		return;

	FAudioDevice* AudioDevice = Component->GetAudioDevice();

	if (!AudioDevice)
		return;

	FSoundSubmixSendInfo SubmixSendInfo;
	SubmixSendInfo.SoundSubmix = Submix;
	SubmixSendInfo.SendLevel = SendLevel;
	SubmixSendInfo.SendLevelControlMethod = ESendLevelControlMethod::Manual;
	SubmixSendInfo.SendStage = ESubmixSendStage::PreDistanceAttenuation;

	VASendToActiveSounds(AudioDevice, Component->GetAudioComponentID(), [SubmixSendInfo](FActiveSound& ActiveSound)
	{
		ActiveSound.SetSubmixSend(SubmixSendInfo);
	});
}

void VASetDryOutputEnabled(UAudioComponent* Component, bool bEnabled)
{
	FAudioDevice* AudioDevice = Component->GetAudioDevice();

	// No active audio device (e.g. audio disabled, or the component's sound already stopped)
	if (!AudioDevice)
		return;

	VASendToActiveSounds(AudioDevice, Component->GetAudioComponentID(), [bEnabled](FActiveSound& ActiveSound)
	{
		ActiveSound.bHasActiveMainSubmixOutputOverride = true;
		ActiveSound.bEnableMainSubmixOutputOverride = bEnabled;
	});
}

// Waits for every command already sent to the audio thread, e.g. a submix registration
static void VAFlushAudioCommands()
{
	FAudioCommandFence Fence;
	Fence.BeginFence();
	Fence.Wait();
}

void VAAddSubmixEffect(const UObject* WorldContext, USoundSubmix* Submix, USoundEffectSubmixPreset* Preset)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	Audio::FMixerDevice* MixerDevice = World ? static_cast<Audio::FMixerDevice*>(World->GetAudioDeviceRaw()) : nullptr;

	// Registering a submix that's already registered re-initialises it, which clears effects added at runtime. A loaded submix queues its own registration, so let that run before checking
	if (MixerDevice && Submix)
	{
		VAFlushAudioCommands();

		// AddSubmixEffect looks the submix up straight away, so wait for the registration
		if (!MixerDevice->GetSubmixInstance(Submix).IsValid())
		{
			MixerDevice->RegisterSoundSubmix(Submix, true);
			VAFlushAudioCommands();
		}
	}

	UAudioMixerBlueprintLibrary::AddSubmixEffect(WorldContext, Submix, Preset);
}
