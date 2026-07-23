extern "C" {
#include "vaudio.h"
}

#include "VAudioContinuous.h"
#include "VAudioWorld.h"
#include "VAudioListener.h"
#include "VARawLog.h"

AVAudioContinuous::AVAudioContinuous()
{
}

void AVAudioContinuous::InitializeTypeSpecific()
{
	ApplyRayPropertiesToEmitter();

	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX);

	// This actor is never the world's reference point - only AVAudioListener sets this true.
	vaEmitterSetHasRelativeReverb(Emitter, false);
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

	int32 NewIndex = vaEmitterGetGroupedEAXIndex(Emitter);

	if (NewIndex != CurrentGroupedEAXIndex)
	{
		VALog(L"groupedEAXIndex changed from %d to %d", CurrentGroupedEAXIndex, NewIndex);
		CurrentGroupedEAXIndex = NewIndex;
	}
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

void AVAudioContinuous::ApplyRayPropertiesToEmitter()
{
	vaEmitterSetReverbRayCount(Emitter, ReverbRayCount);
	vaEmitterSetReverbBounceCount(Emitter, ReverbBounceCount);
	vaEmitterSetReverbEnergyCap(Emitter, ReverbEnergyCap);
	vaEmitterSetMaxEchogramTime(Emitter, MaxEchogramTime);
	vaEmitterSetEchogramGranularity(Emitter, EchogramGranularity);

	vaEmitterSetOcclusionRayCount(Emitter, OcclusionRayCount);
	vaEmitterSetOcclusionBounceCount(Emitter, OcclusionBounceCount);
	vaEmitterSetOcclusionEnergyCap(Emitter, OcclusionEnergyCap);

	vaEmitterSetPermeationRayCount(Emitter, PermeationRayCount);
	vaEmitterSetPermeationBounceCount(Emitter, PermeationBounceCount);
	vaEmitterSetPermeationEnergyCap(Emitter, PermeationEnergyCap);

	vaEmitterSetAmbientOcclusionRayCount(Emitter, AmbientOcclusionRayCount);
	vaEmitterSetAmbientOcclusionBounceCount(Emitter, AmbientOcclusionBounceCount);
	vaEmitterSetAmbientOcclusionEnergyCap(Emitter, AmbientOcclusionEnergyCap);
	vaEmitterSetAmbientPermeationRayCount(Emitter, AmbientPermeationRayCount);
	vaEmitterSetAmbientPermeationBounceCount(Emitter, AmbientPermeationBounceCount);
	vaEmitterSetAmbientPermeationEnergyCap(Emitter, AmbientPermeationEnergyCap);

	vaEmitterSetRefreshRayCount(Emitter, RefreshRayCount);
	vaEmitterSetRefreshDistanceThreshold(Emitter, RefreshDistanceThreshold);

	vaEmitterSetVisualisationRayCount(Emitter, VisualisationRayCount);
	vaEmitterSetVisualisationBounceCount(Emitter, VisualisationBounceCount);
	vaEmitterSetVisualisationUpdateFrequency(Emitter, VisualisationUpdateFrequency);

	vaEmitterSetType(Emitter, EmitterType);
	vaEmitterSetClampPosition(Emitter, bClampPosition);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);
	vaEmitterSetMinimumPermeationEnergy(Emitter, MinimumPermeationEnergy);
}

#if WITH_EDITOR
void AVAudioContinuous::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Emitter only exists while PIE/game is running and TryInitializeEmitter() has completed -
	// editing properties on a placed actor in the editor (not PIE) hits this every time.
	if (!Emitter) return;

	ApplyRayPropertiesToEmitter();

	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX);
}
#endif
