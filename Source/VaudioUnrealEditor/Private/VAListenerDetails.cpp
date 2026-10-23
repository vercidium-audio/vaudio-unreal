#include "VAListenerDetails.h"
#include "DetailLayoutBuilder.h"
#include "VAEmitter.h"

TSharedRef<IDetailCustomization> FVAListenerDetails::MakeInstance()
{
	return MakeShareable(new FVAListenerDetails);
}

void FVAListenerDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Grouped EAX and occlusion/permeation energy caps only apply to emitters the listener raytraces, and bRaytraceOnce removes an emitter once the listener has raytraced it
	const FName hidden[] =
	{
		GET_MEMBER_NAME_CHECKED(AVAEmitter, bHasRelativeReverb),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, bAffectsGroupedEAX),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, bKeepReverbTailAlive),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, OcclusionEnergyCap),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, PermeationEnergyCap),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, bRaytraceOnce),
	};

	for (const FName& name : hidden)
		DetailBuilder.HideProperty(name, AVAEmitter::StaticClass());
}
