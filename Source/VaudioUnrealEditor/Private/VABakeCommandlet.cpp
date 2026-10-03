#include "VABakeCommandlet.h"
#include "VAWorld.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "EditorWorldUtils.h"
#include "EngineUtils.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogVABakeCommandlet, Log, All);

int32 UVABakeCommandlet::Main(const FString& Params)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/true);

	FARFilter Filter;
	Filter.bIncludeOnlyOnDiskAssets = true;
	Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());

	TArray<FAssetData> WorldAssets;
	AssetRegistry.GetAssets(Filter, WorldAssets);

	UE_LOG(LogVABakeCommandlet, Log, TEXT("VABake: found %d level(s) to check"), WorldAssets.Num());

	int32 LevelsBaked = 0;
	int32 WorldsBaked = 0;

	for (const FAssetData& WorldAsset : WorldAssets)
	{
		const FString LongPackageName = WorldAsset.PackageName.ToString();

		UPackage* WorldPackage = LoadWorldPackageForEditor(LongPackageName);
		if (!WorldPackage)
		{
			UE_LOG(LogVABakeCommandlet, Warning, TEXT("VABake: failed to load '%s', skipping"), *LongPackageName);
			continue;
		}

		// Null if the package loaded but doesn't actually contain a UWorld (asset registry
		// listed it as a world asset, but the loaded package disagrees) - nothing to bake.
		UWorld* World = UWorld::FindWorldInPackage(WorldPackage);
		if (!World) continue;

		bool bAnyBaked = false;
		for (TActorIterator<AVAWorld> It(World); It; ++It)
		{
			AVAWorld* VAWorld = *It;
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
			UE_LOG(LogVABakeCommandlet, Log, TEXT("VABake: baked and saved '%s'"), *LongPackageName);
		}
		else
		{
			UE_LOG(LogVABakeCommandlet, Error, TEXT("VABake: failed to save '%s' after baking"), *LongPackageName);
		}
	}

	UE_LOG(LogVABakeCommandlet, Log, TEXT("VABake: done. %d VAWorld actor(s) baked across %d level(s) saved."), WorldsBaked, LevelsBaked);
	return 0;
}
