#include "VASubmixSend.h"
#include "ActiveSound.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundSubmix.h"

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

	AudioDevice->SendCommandToActiveSounds(Component->GetAudioComponentID(), [SubmixSendInfo](FActiveSound& ActiveSound)
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

	AudioDevice->SendCommandToActiveSounds(Component->GetAudioComponentID(), [bEnabled](FActiveSound& ActiveSound)
	{
		ActiveSound.bHasActiveMainSubmixOutputOverride = true;
		ActiveSound.bEnableMainSubmixOutputOverride = bEnabled;
	});
}
