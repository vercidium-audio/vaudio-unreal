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

TArray<FString> VASplitMaterialName(const FString& name)
{
	TArray<FString> words;
	FString word;

	for (int32 i = 0; i < name.Len(); i++)
	{
		TCHAR c = name[i];

		if (!FChar::IsAlnum(c))
		{
			if (word.Len() > 0)
				words.Add(MoveTemp(word));

			word.Reset();
			continue;
		}

		if (word.Len() > 0)
		{
			TCHAR previous = name[i - 1];
			bool nextIsLower = i + 1 < name.Len() && FChar::IsLower(name[i + 1]);

			// "concreteP", "brick01", "01brick" and the "G" in "MIGlass" start a new word
			bool boundary = (FChar::IsLower(previous) && FChar::IsUpper(c)) || (FChar::IsDigit(previous) != FChar::IsDigit(c)) || (FChar::IsUpper(previous) && FChar::IsUpper(c) && nextIsLower);

			if (boundary)
			{
				words.Add(MoveTemp(word));
				word.Reset();
			}
		}

		word.AppendChar(FChar::ToLower(c));
	}

	if (word.Len() > 0)
		words.Add(MoveTemp(word));

	return words;
}

bool VAMatchMaterialName(const FString& name, const TArray<FVAMaterialNameCandidate>& candidates, int32& outMaterialID)
{
	TArray<FString> words = VASplitMaterialName(name);
	int32 bestLength = 0;

	for (const FVAMaterialNameCandidate& candidate : candidates)
	{
		int32 count = candidate.Words.Num();

		if (count == 0 || count > words.Num())
			continue;

		int32 length = 0;

		for (const FString& word : candidate.Words)
			length += word.Len();

		// Ties go to the earlier candidate
		if (length <= bestLength)
			continue;

		for (int32 start = 0; start + count <= words.Num(); start++)
		{
			bool matches = true;

			for (int32 i = 0; i < count && matches; i++)
				matches = words[start + i] == candidate.Words[i];

			if (matches)
			{
				bestLength = length;
				outMaterialID = candidate.MaterialID;
				break;
			}
		}
	}

	return bestLength > 0;
}

const TArray<FVAMaterialNameCandidate>& VABuiltInMaterialNames()
{
	static TArray<FVAMaterialNameCandidate> candidates = []()
	{
		TArray<FVAMaterialNameCandidate> result;
		const UEnum* materialEnum = StaticEnum<EVAMaterial>();

		// The last entry is the generated _MAX
		for (int32 i = 0; i < materialEnum->NumEnums() - 1; i++)
		{
			FVAMaterialNameCandidate candidate;
			candidate.Words = VASplitMaterialName(materialEnum->GetNameStringByIndex(i));
			candidate.MaterialID = (int32)EVAMaterialToVA((EVAMaterial)materialEnum->GetValueByIndex(i));
			result.Add(MoveTemp(candidate));
		}

		return result;
	}();

	return candidates;
}
