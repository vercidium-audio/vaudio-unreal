// AI-created

#include "VAMeshMaterialUserData.h"
#include "VAWorld.h"
#include "VAEngineCompat.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "Components/PrimitiveComponent.h"
#include "LandscapeProxy.h"

FString UVAMeshMaterialUserData::GetContext() const
{
	return FString::Printf(TEXT(" (VAMeshMaterial on mesh '%s')"), *GetNameSafe(GetOuter()));
}

bool UVAMeshMaterialUserData::GetMaterialID(const AVAWorld* world, const UObject* reporter, int32& outMaterialId) const
{
	return Material.GetMaterialID(world, reporter, GetContext(), outMaterialId);
}

bool UVAMeshMaterialUserData::FindSlotMaterialID(const AVAWorld* world, const UObject* reporter, FName slotName, bool& outFound, int32& outMaterialID) const
{
	const FVAMeshMaterialSlot* slot = SlotMaterials.FindByPredicate([&](const FVAMeshMaterialSlot& entry) { return entry.SlotName == slotName; });
	outFound = slot != nullptr;

	if (!slot)
		return true;

	if (slot->bIgnore)
	{
		outMaterialID = INDEX_NONE;
		return true;
	}

	return slot->Material.GetMaterialID(world, reporter, FString::Printf(TEXT(" (slot '%s' of the VAMeshMaterial on mesh '%s')"), *slot->SlotName.ToString(), *GetNameSafe(GetOuter())), outMaterialID);
}

bool UVAMeshMaterialUserData::GetPermeationMaterialID(const AVAWorld* world, const UObject* reporter, int32& outMaterialId) const
{
	outMaterialId = 0;
	return !bOverridePermeationMaterial || PermeationMaterial.GetMaterialID(world, reporter, FString::Printf(TEXT(" (PermeationMaterial of the VAMeshMaterial on mesh '%s')"), *GetNameSafe(GetOuter())), outMaterialId);
}

TArray<FString> UVAMeshMaterialUserData::GetMaterialSlotNames() const
{
	TArray<FString> names;

	if (const UStaticMesh* mesh = GetTypedOuter<UStaticMesh>())
		for (const FStaticMaterial& staticMaterial : mesh->GetStaticMaterials())
			names.Add(staticMaterial.MaterialSlotName.ToString());

	return names;
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
	static const FName parentName(TEXT("Parent"));
	static const FName physMaterialName(TEXT("PhysMaterial"));

	// Renaming, adding or removing a material slot changes which slot each SlotMaterials entry matches, and changing a slot's render material can change what it's matched to
	FName propertyName = VAMemberPropertyName(event);

	if (propertyName == assetUserDataName || propertyName == UStaticMesh::GetStaticMaterialsName())
		RefreshRunningWorlds(Cast<UStaticMesh>(object));

	static const FName defaultPhysMaterialName(TEXT("DefaultPhysMaterial"));
	static const FName bodyInstanceName(TEXT("BodyInstance"));

	// A render material's parent or physical material decides what its slots are matched to, for every mesh using it. A body setup's or component's physical material decides simple collision and shapes, and a landscape's its heightmaps
	bool renderMaterialChanged = Cast<UMaterialInterface>(object) && (propertyName == parentName || propertyName == physMaterialName);
	bool bodyMaterialChanged = (Cast<UBodySetup>(object) && propertyName == physMaterialName) || (Cast<UPrimitiveComponent>(object) && propertyName == bodyInstanceName) || (Cast<ALandscapeProxy>(object) && propertyName == defaultPhysMaterialName);

	if (renderMaterialChanged || bodyMaterialChanged)
	{
		for (const TWeakObjectPtr<AVAWorld>& weakWorld : AVAWorld::RunningWorlds)
			if (AVAWorld* world = weakWorld.Get())
				world->RebuildPrimitives();
	}
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
