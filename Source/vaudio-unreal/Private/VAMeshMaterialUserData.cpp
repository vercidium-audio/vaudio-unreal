// AI-created

#include "VAMeshMaterialUserData.h"
#include "VAMaterialConversion.h"
#include "VAWorld.h"
#include "Engine/StaticMesh.h"

bool UVAMeshMaterialUserData::GetMaterialId(const AVAWorld* world, const UObject* reporter, int32& outMaterialId) const
{
	return VAResolveMaterialId(MaterialAsset, Material, world, reporter, FString::Printf(TEXT(" (VAMeshMaterial on mesh '%s')"), *GetNameSafe(GetOuter())), outMaterialId);
}

UVAMeshMaterialUserData* UVAMeshMaterialUserData::Find(const UStaticMesh* mesh)
{
	// GetAssetUserDataOfClass isn't const
	return mesh ? Cast<UVAMeshMaterialUserData>(const_cast<UStaticMesh*>(mesh)->GetAssetUserDataOfClass(UVAMeshMaterialUserData::StaticClass())) : nullptr;
}

#if WITH_EDITOR
void UVAMeshMaterialUserData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshRunningWorlds(GetTypedOuter<UStaticMesh>());
}

void UVAMeshMaterialUserData::OnObjectPropertyChanged(UObject* object, FPropertyChangedEvent& event)
{
	static const FName assetUserDataName(TEXT("AssetUserData"));

	if (event.GetMemberPropertyName() == assetUserDataName)
		RefreshRunningWorlds(Cast<UStaticMesh>(object));
}

void UVAMeshMaterialUserData::RefreshRunningWorlds(const UStaticMesh* mesh)
{
	if (!mesh)
		return;

	for (const TWeakObjectPtr<AVAWorld>& weakWorld : AVAWorld::RunningWorlds)
	{
		if (AVAWorld* world = weakWorld.Get())
			world->RefreshMeshDefaults(mesh);
	}
}
#endif
