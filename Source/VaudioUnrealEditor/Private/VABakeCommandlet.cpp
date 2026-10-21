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

	int32 PackagesSaved = 0;
	int32 WorldsBaked = 0;

	for (const FAssetData& WorldAsset : WorldAssets)
	{
		const FString LongPackageName = WorldAsset.PackageName.ToString();

		// Initialised, as World Partition needs it to load unloaded actors while baking
		UWorld::InitializationValues IVS;
		IVS.RequiresHitProxies(false);
		IVS.ShouldSimulatePhysics(false);
		IVS.EnableTraceCollision(false);
		IVS.CreateNavigation(false);
		IVS.CreateAISystem(false);
		IVS.AllowAudioPlayback(false);
		IVS.CreatePhysicsScene(true);
		FScopedEditorWorld EditorWorld(LongPackageName, IVS);
		UWorld* World = EditorWorld.GetWorld();

		if (!World)
		{
			UE_LOG(LogVABakeCommandlet, Warning, TEXT("VABake: failed to load '%s', skipping"), *LongPackageName);
			continue;
		}

		for (TActorIterator<AVAWorld> It(World); It; ++It)
		{
			AVAWorld* VAWorld = *It;
			VAWorld->BakeGeometry();
			++WorldsBaked;

			// The level's package, or in a World Partition map the VAWorld's own external actor package
			UPackage* Package = VAWorld->GetPackage();
			const bool bIsMap = Package == World->GetPackage();
			const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), bIsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;

			if (UPackage::SavePackage(Package, bIsMap ? World : nullptr, *PackageFileName, SaveArgs))
			{
				++PackagesSaved;
				UE_LOG(LogVABakeCommandlet, Log, TEXT("VABake: baked '%s' and saved '%s'"), *LongPackageName, *Package->GetName());
			}
			else
			{
				UE_LOG(LogVABakeCommandlet, Error, TEXT("VABake: failed to save '%s' after baking"), *Package->GetName());
			}
		}
	}

	UE_LOG(LogVABakeCommandlet, Log, TEXT("VABake: done. %d VAWorld actor(s) baked, %d package(s) saved."), WorldsBaked, PackagesSaved);
	return 0;
}
