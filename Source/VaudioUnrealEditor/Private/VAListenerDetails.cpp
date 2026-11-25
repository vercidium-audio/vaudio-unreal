// Human-created

#include "VAListenerDetails.h"
#include "DetailLayoutBuilder.h"
#include "VAEmitter.h"

TSharedRef<IDetailCustomization> FVAListenerDetails::MakeInstance()
{
	return MakeShareable(new FVAListenerDetails);
}

void FVAListenerDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Hide VAListener properties that the user should not be able to customise
	const FName hidden[] =
	{
		GET_MEMBER_NAME_CHECKED(AVAEmitter, bClusteringEnabled),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, ClusteringDistance),
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
