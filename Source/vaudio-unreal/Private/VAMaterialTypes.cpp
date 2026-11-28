// AI-created

#include "VAMaterialTypes.h"
#include "VAMaterial.h"
#include "VAMaterialConversion.h"
#include "VAWorld.h"
#include "VALog.h"

bool FVAMaterialChoice::GetMaterialID(const AVAWorld* world, const UObject* reporter, const FString& context, int32& outMaterialID) const
{
	if (!MaterialAsset)
	{
		outMaterialID = (int32)EVAMaterialToVA(Material);
		return true;
	}

	if (!world || !world->HasMaterial(MaterialAsset))
	{
		VAReport(ELogVerbosity::Warning, reporter, VA_CALL_SITE_KEY(reporter), FString::Printf(TEXT("MaterialAsset '%s' is not in the VAWorld's Materials array%s. Add it to the Materials array of the level's VAWorld."), *MaterialAsset->GetName(), *context));
		return false;
	}

	return MaterialAsset->GetMaterialID(world, outMaterialID);
}
