#pragma once

#include "CoreMinimal.h"
#include "VAConstants.h"

class AVAWorld;
class USoundSubmix;
struct FVASourcePlayback;

// Helpers on a raw VAEmitter*, shared by AVAEmitter and the actor-free one-shots that have no actor to own their handle

// A handle with the log and callback trampolines set. A handle with null user data is treated as orphaned by OnRemoved
VAEmitter* VACreateEmitterHandle();

// Clears the user data and destroys a handle that the SDK no longer references, e.g. one never added to a world
VAResult VADestroyEmitterHandle(VAEmitter* handle);

int32 VARandomScatteringSeed();

// Pushes the properties FVAOneShotSettings shares with AVAEmitter. TSource needs their names
template <typename TSource>
void VAPushOneShotProperties(VAEmitter* handle, const TSource& source)
{
	vaEmitterSetReverbRayCount(handle, source.ReverbRayCount);
	vaEmitterSetReverbBounceCount(handle, source.ReverbBounceCount);
	vaEmitterSetReverbEnergyCap(handle, source.ReverbEnergyCap);
	vaEmitterSetMinimumReverbEnergy(handle, source.MinimumReverbEnergy);
	vaEmitterSetMaxVolume(handle, source.MaxVolume);
	vaEmitterSetMaxEchogramTime(handle, source.MaxEchogramTime);
	vaEmitterSetEchogramGranularity(handle, source.EchogramGranularity);
	vaEmitterSetAffectsGroupedEAX(handle, source.bAffectsGroupedEAX);
	vaEmitterSetKeepReverbTailAlive(handle, source.bKeepReverbTailAlive);

	vaEmitterSetOcclusionEnergyCap(handle, source.OcclusionEnergyCap);
	vaEmitterSetPermeationEnergyCap(handle, source.PermeationEnergyCap);

	vaEmitterSetClusteringEnabled(handle, source.bClusteringEnabled);
	vaEmitterSetClusteringDistance(handle, FMath::Max(0.01f, source.ClusteringDistance));

	vaEmitterSetType(handle, source.Type);
}

// The AVAEmitter properties FVAOneShotSettings doesn't have
template <typename TSource>
void VAPushNonOneShotProperties(VAEmitter* handle, const TSource& source)
{
	vaEmitterSetHasRelativeReverb(handle, source.bHasRelativeReverb);

	vaEmitterSetAmbientOcclusionRayCount(handle, source.AmbientOcclusionRayCount);
	vaEmitterSetAmbientOcclusionBounceCount(handle, source.AmbientOcclusionBounceCount);
	vaEmitterSetAmbientOcclusionEnergyCap(handle, source.AmbientOcclusionEnergyCap);
	vaEmitterSetMinimumAmbientOcclusionEnergy(handle, source.MinimumAmbientOcclusionEnergy);
	vaEmitterSetAmbientPermeationRayCount(handle, source.AmbientPermeationRayCount);
	vaEmitterSetAmbientPermeationBounceCount(handle, source.AmbientPermeationBounceCount);
	vaEmitterSetAmbientPermeationEnergyCap(handle, source.AmbientPermeationEnergyCap);
	vaEmitterSetMinimumAmbientPermeationEnergy(handle, source.MinimumAmbientPermeationEnergy);

	vaEmitterSetTrailRefreshCount(handle, source.TrailRefreshCount);
	vaEmitterSetRefreshDistanceThreshold(handle, source.RefreshDistanceThreshold);
	vaEmitterSetScatteringSeed(handle, source.ScatteringSeed);
	vaEmitterSetClampPosition(handle, source.bClampPosition);

	vaEmitterSetRandomTrailColor(handle, source.bRandomTrailColor);
	vaEmitterSetTrailColor(handle, FColorToVA(source.TrailColor));
	vaEmitterSetReverbColor(handle, FColorToVA(source.ReverbColor));
	vaEmitterSetOcclusionColor(handle, FColorToVA(source.OcclusionColor));
	vaEmitterSetPermeationColor(handle, FColorToVA(source.PermeationColor));
	vaEmitterSetAmbientPermeationColor(handle, FColorToVA(source.AmbientPermeationColor));
}

// Pushes every AVAEmitter property to the handle. TSource needs AVAEmitter's property names
template <typename TSource>
void VAPushEmitterProperties(VAEmitter* handle, const TSource& source)
{
	VAPushOneShotProperties(handle, source);
	VAPushNonOneShotProperties(handle, source);
}

// The main listener's muffling filter for this handle, or null until the listener has raytraced it
VALowPassFilter* VAGetMufflingResult(VAEmitter* handle, const AVAWorld* world);

// Muffled by the listener and, if it affects grouped EAX with reverb rays, has its own EAX
bool VAIsHandleReadyToPlay(VAEmitter* handle, const AVAWorld* world);

// See AVAEmitter::ResolveReverbSend. groupedEAXIndex is -1 when the handle doesn't affect grouped EAX. Warnings are prefixed with logContext's name
bool VAResolveReverbSend(VAEmitter* handle, const AVAWorld* world, int32 groupedEAXIndex, bool useListenerReverb, const UObject* logContext, USoundSubmix*& outSubmix, float& outSendLevel);

// Pushes the muffling filter, then the reverb send, to playback. The last result is kept while either is unavailable
void VAApplyHandleResults(FVASourcePlayback& playback, VAEmitter* handle, const AVAWorld* world, int32 groupedEAXIndex, bool useListenerReverb, const UObject* logContext);
