#include "VAudioContinuous.h"
#include "VAudioWorld.h"
#include "VAudioListener.h"
#include "VARawLog.h"

extern "C" {
#include "vaudio.h"
}

AVAudioContinuous::AVAudioContinuous()
{
}

bool AVAudioContinuous::InitializeTypeSpecific()
{
	if (bAffectsGroupedEAX && (ReverbRayCount == 0 || ReverbBounceCount == 0))
	{
		DisplayWarning(TEXT("[VA] Continuous Emitter '%s' has affectsGroupedEAX=true, but does not cast reverb rays"), *GetActorNameOrLabel());
	}

	UpdateVAEmitter();

	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX);
	vaEmitterSetHasRelativeReverb(Emitter, false);

	return true;
}

void AVAudioContinuous::DeinitializeTypeSpecific()
{
	CurrentGroupedEAXIndex = -1;
}

void AVAudioContinuous::TickTypeSpecific(float DeltaTime)
{
	if (bAffectsGroupedEAX)
		UpdateGroupedEAXIndex();
}

void AVAudioContinuous::UpdateGroupedEAXIndex()
{
	if (!Emitter)
		return;

	CurrentGroupedEAXIndex = vaEmitterGetGroupedEAXIndex(Emitter);
}

VALowPassFilter* AVAudioContinuous::GetMufflingResult() const
{
	if (!AudioWorld || !Emitter)
		return nullptr;

	AVAudioListener* Listener = AudioWorld->GetMainListener();

	if (!Listener || !Listener->GetVAEmitter())
		return nullptr;

	if (!vaEmitterHasRaytracedTarget(Listener->GetVAEmitter(), Emitter))
		return nullptr;

	return vaEmitterGetTargetFilter(Listener->GetVAEmitter(), Emitter);
}

#if WITH_EDITOR
void AVAudioContinuous::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Emitter only exists while PIE/game is running and TryInitializeEmitter() has completed -
	// editing properties on a placed actor in the editor (not PIE) hits this every time.
	if (!Emitter)
		return;

	UpdateVAEmitter();

	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX);
}
#endif
