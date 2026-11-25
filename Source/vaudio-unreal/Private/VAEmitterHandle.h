// AI-created

#pragma once

#include "CoreMinimal.h"
#include "VAConstants.h"
#include "VAHeldMuffling.h"

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

// The main listener's live muffling filter for this handle, or null until the listener has raytraced it or while it isn't a target
VALowPassFilter* VAGetListenerMuffling(VAEmitter* handle, const AVAWorld* world);

// The muffling held while the handle is suspended, otherwise the live filter, falling back to the held one until the listener has raytraced it again
const VALowPassFilter* VAGetMufflingResult(VAEmitter* handle, const AVAWorld* world, const FVAHeldMuffling& held);

// Stops the handle casting rays and takes it out of the listener's targets, holding its last muffling. Call once it has its results
void VASuspendHandle(VAEmitter* handle, const AVAWorld* world, FVAHeldMuffling& held);

// Casts its rays again and makes it a listener target again, keeping the held muffling until the listener has raytraced it. With no listener, WirePendingTargets adds it when one begins play
void VAResumeHandle(VAEmitter* handle, const AVAWorld* world, FVAHeldMuffling& held, const FString& name);

// Drops the held muffling once a resumed handle has been raytraced by the listener again. Call every tick
void VAUpdateHeldMuffling(VAEmitter* handle, const AVAWorld* world, FVAHeldMuffling& held);

// AVAWorld::IsOutsideBounds at the handle's position and bClampPosition
bool VAIsHandleOutsideBounds(VAEmitter* handle, const AVAWorld* world);

// Muffled by the listener and, if it affects grouped EAX with reverb rays, has its own EAX
bool VAHasHandleResults(VAEmitter* handle, const AVAWorld* world, const FVAHeldMuffling& held);

// VAHasHandleResults, plus with useListenerReverb and no grouped EAX (groupedEAXIndex -1), the listener's first EAX (VAIsListenerReverbReady). A handle outside the bounds waits for neither, as it isn't raytraced there and has no reverb send
bool VAIsHandleReadyToPlay(VAEmitter* handle, const AVAWorld* world, const FVAHeldMuffling& held, int32 groupedEAXIndex, bool useListenerReverb);

// True once the current listener has its EAX, so a relative sound never plays through the default reverb of its ListenerReverbSubmix. Also true if the listener has no submix, casts no reverb rays or is outside the bounds, as there's nothing to wait for. False with no listener
bool VAIsListenerReverbReady(const AVAWorld* world);

// See AVAEmitter::ResolveReverbSend. groupedEAXIndex is -1 when the handle doesn't affect grouped EAX. Outside the bounds there's no send. Warnings are prefixed with logContext's name
bool VAResolveReverbSend(VAEmitter* handle, const AVAWorld* world, int32 groupedEAXIndex, bool useListenerReverb, const UObject* logContext, USoundSubmix*& outSubmix, float& outSendLevel);

// Pushes the muffling filter, then the reverb send, to playback. The last result is kept while either is unavailable
void VAApplyHandleResults(FVASourcePlayback& playback, VAEmitter* handle, const AVAWorld* world, const FVAHeldMuffling& held, int32 groupedEAXIndex, bool useListenerReverb, const UObject* logContext);
