#include "VABakeCommandlet.h"
#include "VAWorld.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "EngineUtils.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if UE_VERSION_OLDER_THAN(5, 0, 0)
#include "Editor.h"
#else
#include "EditorWorldUtils.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogVABakeCommandlet, Log, All);

#if UE_VERSION_OLDER_THAN(5, 0, 0)
// 4.27 doesn't have FScopedEditorWorld, so this does the same as the 5.x one
class FScopedEditorWorld
{
public:
	FScopedEditorWorld(const FString& LongPackageName, const UWorld::InitializationValues& IVS)
	{
		const FName PackageName(*LongPackageName);
		UWorld::WorldTypePreLoadMap.FindOrAdd(PackageName) = EWorldType::Editor;
		UPackage* Package = LoadPackage(nullptr, *LongPackageName, LOAD_None);
		UWorld::WorldTypePreLoadMap.Remove(PackageName);

		World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;

		if (!World)
			return;

		World->AddToRoot();

		GEditor->GetEditorWorldContext(true).SetCurrentWorld(World);
		PrevGWorld = GWorld;
		GWorld = World;

		World->WorldType = EWorldType::Editor;
		World->InitWorld(IVS);
		World->PersistentLevel->UpdateModelComponents();
		World->UpdateWorldComponents(true, false);
		World->UpdateLevelStreaming();
	}

	~FScopedEditorWorld()
	{
		if (!World)
			return;

		GEditor->Cleanse(true, true, FText::FromString(TEXT("Resetting the transaction system after baking")));

		World->DestroyWorld(false);
		World->RemoveFromRoot();

		GEditor->GetEditorWorldContext(true).SetCurrentWorld(PrevGWorld);
		GWorld = PrevGWorld;
	}

	UWorld* GetWorld() const { return World; }

private:
	UWorld* World = nullptr;
	UWorld* PrevGWorld = nullptr;
};
#endif

int32 UVABakeCommandlet::Main(const FString& Params)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/true);

	FARFilter Filter;
	Filter.bIncludeOnlyOnDiskAssets = true;
#if UE_VERSION_OLDER_THAN(5, 0, 0)
	Filter.ClassNames.Add(UWorld::StaticClass()->GetFName());
#else
	Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
#endif

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

#if UE_VERSION_OLDER_THAN(5, 0, 0)
			const bool bSaved = UPackage::SavePackage(Package, bIsMap ? World : nullptr, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_NoError);
#else
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;

			const bool bSaved = UPackage::SavePackage(Package, bIsMap ? World : nullptr, *PackageFileName, SaveArgs);
#endif

			if (bSaved)
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
