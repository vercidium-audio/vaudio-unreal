#include "VAEmitterHandle.h"
#include "VAEmitter.h"
#include "VAWorld.h"
#include "VAListener.h"
#include "VASourcePlayback.h"
#include "VALog.h"
#include "Misc/Guid.h"

// vaEmitterSetUserData() stashes the owning actor on the VAEmitter* itself, so these trampolines
// can resolve identity directly instead of needing a side registry.
static void VAOnRaytracingCompleteTrampoline(VAEmitter* emitter)
{
	if (AVAEmitter* owner = static_cast<AVAEmitter*>(vaEmitterGetUserData(emitter)))
		owner->QueueRaytracingComplete();
}

static void VAOnRaytracedByAnotherEmitterTrampoline(VAEmitter* source, VAEmitter* target)
{
	AVAEmitter* owner = static_cast<AVAEmitter*>(vaEmitterGetUserData(target));

	if (!owner)
		return;

	VALowPassFilter* filter = nullptr;

	if (vaEmitterGetTargetFilter(source, target, &filter) == VA_SUCCESS)
		owner->QueueRaytracedByListener(filter->gainLF, filter->gainHF);
}

static void VAOnRemovedTrampoline(VAEmitter* emitter)
{
	if (AVAEmitter* owner = static_cast<AVAEmitter*>(vaEmitterGetUserData(emitter)))
	{
		owner->OnEmitterRemoved(emitter);
		return;
	}

	// The actor already released this handle while its removal was pending
	AVAWorld::OnOrphanedEmitterRemoved(emitter);
}

VAEmitter* VACreateEmitterHandle()
{
	VAEmitter* handle = vaEmitterCreate();

	vaEmitterSetLogCallback(handle, &VASdkEmitterLogCallback);
	vaEmitterSetLogErrorCallback(handle, &VASdkEmitterLogErrorCallback);
	vaEmitterSetOnRaytracingCompleteCallback(handle, &VAOnRaytracingCompleteTrampoline);
	vaEmitterSetOnRaytracedByAnotherEmitterCallback(handle, &VAOnRaytracedByAnotherEmitterTrampoline);
	vaEmitterSetOnRemovedCallback(handle, &VAOnRemovedTrampoline);

	return handle;
}

VAResult VADestroyEmitterHandle(VAEmitter* handle)
{
	vaEmitterSetUserData(handle, nullptr);
	return vaEmitterDestroy(handle);
}

int32 VARandomScatteringSeed()
{
#if UE_VERSION_OLDER_THAN(5, 0, 0)
	// 4.27 has no Rand32, and its Rand() is only 15 bits on MSVC
	return (int32)(FGuid::NewGuid().A & 0x7fffffff);
#else
	return (int32)(FMath::Rand32() & 0x7fffffff);
#endif
}

VALowPassFilter* VAGetMufflingResult(VAEmitter* handle, const AVAWorld* world)
{
	if (!world || !handle)
		return nullptr;

	AVAListener* listener = world->GetMainListener();

	if (!listener || !listener->GetVAEmitter())
		return nullptr;

	VALowPassFilter* filter = nullptr;
	vaEmitterGetTargetFilter(listener->GetVAEmitter(), handle, &filter);
	return filter;
}

bool VAIsHandleReadyToPlay(VAEmitter* handle, const AVAWorld* world)
{
	if (!VAGetMufflingResult(handle, world))
		return false;

	// Its grouped EAX slot, and so its reverb send, isn't known until it casts its own reverb rays
	if (vaEmitterGetAffectsGroupedEAX(handle) && vaEmitterGetReverbEnabled(handle))
		return vaEmitterGetEAX(handle) != nullptr;

	return true;
}

bool VAResolveReverbSend(VAEmitter* handle, const AVAWorld* world, int32 groupedEAXIndex, bool useListenerReverb, const UObject* logContext, USoundSubmix*& outSubmix, float& outSendLevel)
{
	outSubmix = nullptr;
	outSendLevel = 0.0f;

	// Not added to a world yet, or already removed
	if (!world || !handle)
		return false;

	VAWorld* vaWorld = world->GetVAWorld();
	AVAListener* listener = world->GetMainListener();

	// The listener ended play
	if (!vaWorld || !listener || !listener->GetVAEmitter())
		return false;

	if (groupedEAXIndex >= 0)
	{
		int groupedEAXCount = vaWorldGetGroupedEAXCount(vaWorld);

		if (groupedEAXIndex >= groupedEAXCount)
		{
			VAReport(ELogVerbosity::Warning, logContext, VA_CALL_SITE_KEY(logContext), FString::Printf(TEXT("Has an invalid grouped EAX index: %d. There are only %d grouped EAX submixes available"), groupedEAXIndex, groupedEAXCount));
			return false;
		}

		// A grouped EAX index is only assigned once this emitter's own reverb rays have completed, so its grouped EAX is guaranteed to be available
		const VAEAXReverb* EAX = vaWorldGetGroupedEAX(vaWorld)[groupedEAXIndex];

		// Only relative gain is supported. Can't do directional reverb in Unreal :(
		const float* relativeGain = vaEAXReverbGetRelativeGain(EAX, listener->GetVAEmitter());

		if (!relativeGain)
			return false;

		// Null if the user assigned a null submix to World.groupedEAX[]. A warning is already logged in VAWorld.cpp
		outSubmix = world->GetGroupedEAXSubmix(groupedEAXIndex);
		outSendLevel = *relativeGain;
		return true;
	}

	if (useListenerReverb && listener->ListenerReverbSubmix)
	{
		outSubmix = listener->ListenerReverbSubmix;
		outSendLevel = 1.0f;
	}

	return true;
}

void VAApplyHandleResults(FVASourcePlayback& playback, VAEmitter* handle, const AVAWorld* world, int32 groupedEAXIndex, bool useListenerReverb, const UObject* logContext)
{
	// Null until the listener has raytraced the handle. The last result is kept
	if (VALowPassFilter* lowPassFilter = VAGetMufflingResult(handle, world))
		playback.SetFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);

	USoundSubmix* submix = nullptr;
	float sendLevel = 0.0f;

	// Set after the filter, as the send level is compensated by gainLF
	if (VAResolveReverbSend(handle, world, groupedEAXIndex, useListenerReverb, logContext, submix, sendLevel))
		playback.SetReverbSend(submix, sendLevel);
}
