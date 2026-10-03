#include "VAEmitter.h"
#include "VAWorld.h"
#include "VAListener.h"
#include "VALog.h"

extern "C" {
#include "vaudio.h"
}

AVAEmitter::AVAEmitter()
{
}

void AVAEmitter::InitializeTypeSpecific()
{
	if (bAffectsGroupedEAX && (ReverbRayCount == 0 || ReverbBounceCount == 0))
	{
		VA_WARN_NAMED(TEXT("Has affectsGroupedEAX=true, but does not cast reverb rays"));
	}

	UpdateVAEmitter();

	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX);
	vaEmitterSetKeepReverbTailAlive(Emitter, bKeepReverbTailAlive);
	vaEmitterSetHasRelativeReverb(Emitter, false);
}

void AVAEmitter::DeinitializeTypeSpecific()
{
	CurrentGroupedEAXIndex = -1;
}

void AVAEmitter::TickTypeSpecific(float DeltaTime)
{
	if (bAffectsGroupedEAX)
		UpdateGroupedEAXIndex();
}

void AVAEmitter::UpdateGroupedEAXIndex()
{
	if (!Emitter)
		return;

	CurrentGroupedEAXIndex = vaEmitterGetGroupedEAXIndex(Emitter);
}

VALowPassFilter* AVAEmitter::GetMufflingResult() const
{
	if (!AudioWorld || !Emitter)
		return nullptr;

	AVAListener* Listener = AudioWorld->GetMainListener();

	if (!Listener || !Listener->GetVAEmitter())
		return nullptr;

	if (!vaEmitterHasRaytracedTarget(Listener->GetVAEmitter(), Emitter))
		return nullptr;

	return vaEmitterGetTargetFilter(Listener->GetVAEmitter(), Emitter);
}

void AVAEmitter::GetMufflingFilterResult(bool& bSuccess, float& GainLF, float& GainHF) const
{
	VALowPassFilter* MufflingFilter = GetMufflingResult();

	// Raytracing has not completed at least once yet
	if (!MufflingFilter)
	{
		bSuccess = false;
		GainLF = 0.0f;
		GainHF = 0.0f;
		return;
	}

	bSuccess = true;
	GainLF = MufflingFilter->gainLF;
	GainHF = MufflingFilter->gainHF;
}

#if WITH_EDITOR
void AVAEmitter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Emitter only exists while PIE/game is running and TryInitializeEmitter() has completed -
	// editing properties on a placed actor in the editor (not PIE) hits this every time.
	if (!Emitter)
		return;

	UpdateVAEmitter();

	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX);
	vaEmitterSetKeepReverbTailAlive(Emitter, bKeepReverbTailAlive);
}
#endif
