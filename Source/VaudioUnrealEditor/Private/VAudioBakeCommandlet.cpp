#include "VAudioBakeCommandlet.h"
#include "VAudioWorld.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "EditorWorldUtils.h"
#include "EngineUtils.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogVAudioBakeCommandlet, Log, All);

int32 UVAudioBakeCommandlet::Main(const FString& Params)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/true);

	FARFilter Filter;
	Filter.bIncludeOnlyOnDiskAssets = true;
	Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());

	TArray<FAssetData> WorldAssets;
	AssetRegistry.GetAssets(Filter, WorldAssets);

	UE_LOG(LogVAudioBakeCommandlet, Log, TEXT("VAudioBake: found %d level(s) to check"), WorldAssets.Num());

	int32 LevelsBaked = 0;
	int32 WorldsBaked = 0;

	for (const FAssetData& WorldAsset : WorldAssets)
	{
		const FString LongPackageName = WorldAsset.PackageName.ToString();

		UPackage* WorldPackage = LoadWorldPackageForEditor(LongPackageName);
		if (!WorldPackage)
		{
			UE_LOG(LogVAudioBakeCommandlet, Warning, TEXT("VAudioBake: failed to load '%s', skipping"), *LongPackageName);
			continue;
		}

		UWorld* World = UWorld::FindWorldInPackage(WorldPackage);
		if (!World) continue;

		bool bAnyBaked = false;
		for (TActorIterator<AVAudioWorld> It(World); It; ++It)
		{
			AVAudioWorld* VAWorld = *It;
#if WITH_EDITOR
			VAWorld->BakeGeometry();
#endif
			bAnyBaked = true;
			++WorldsBaked;
		}

		if (!bAnyBaked) continue;

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetMapPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		WorldPackage->MarkPackageDirty();
		const bool bSaved = UPackage::SavePackage(WorldPackage, World, *PackageFileName, SaveArgs);

		if (bSaved)
		{
			++LevelsBaked;
			UE_LOG(LogVAudioBakeCommandlet, Log, TEXT("VAudioBake: baked and saved '%s'"), *LongPackageName);
		}
		else
		{
			UE_LOG(LogVAudioBakeCommandlet, Error, TEXT("VAudioBake: failed to save '%s' after baking"), *LongPackageName);
		}
	}

	UE_LOG(LogVAudioBakeCommandlet, Log, TEXT("VAudioBake: done. %d VA Audio World actor(s) baked across %d level(s) saved."), WorldsBaked, LevelsBaked);
	return 0;
}
