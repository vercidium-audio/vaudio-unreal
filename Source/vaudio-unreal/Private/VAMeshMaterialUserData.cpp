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

	// Editing the mesh during PIE. Every running world is rebuilt rather than tracking which ones use this mesh, as it's an editor-only, one-off cost
	for (const TWeakObjectPtr<AVAWorld>& weakWorld : AVAWorld::RunningWorlds)
	{
		if (AVAWorld* world = weakWorld.Get())
			world->RebuildPrimitives();
	}
}
#endif
