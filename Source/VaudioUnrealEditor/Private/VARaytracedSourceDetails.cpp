#include "VARaytracedSourceDetails.h"
#include "DetailLayoutBuilder.h"
#include "VAEmitter.h"

TSharedRef<IDetailCustomization> FVARaytracedSourceDetails::MakeInstance()
{
	return MakeShareable(new FVARaytracedSourceDetails);
}

void FVARaytracedSourceDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Relative reverb is for listeners, and a source has no debug colours of its own
	const FName hidden[] =
	{
		GET_MEMBER_NAME_CHECKED(AVAEmitter, bHasRelativeReverb),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, bRandomTrailColor),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, TrailColor),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, ReverbColor),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, OcclusionColor),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, PermeationColor),
		GET_MEMBER_NAME_CHECKED(AVAEmitter, AmbientPermeationColor),
	};

	for (const FName& name : hidden)
		DetailBuilder.HideProperty(name, AVAEmitter::StaticClass());
}
