#include "VAEditorSprite.h"

#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IPluginManager.h"

#if WITH_EDITORONLY_DATA
void VASetEditorSprite(UBillboardComponent* billboard, const UClass* actorClass)
{
	// Like the engine's own sprites, so cooking doesn't pull the editor-only textures in
	if (!billboard || IsRunningCommandlet())
		return;

	static const FString iconPath = [] {
		TSharedPtr<IPlugin> plugin = IPluginManager::Get().FindPlugin(TEXT("vaudio-unreal"));
		return plugin ? plugin->GetMountedAssetPath() + TEXT("Icons/T_") : FString();
	}();

	if (iconPath.IsEmpty())
		return;

	for (const UClass* type = actorClass; type && type != AActor::StaticClass(); type = type->GetSuperClass())
	{
		if (!type->HasAnyClassFlags(CLASS_Native))
			continue;

		FString name = type->GetName();
		FString path = iconPath + name + TEXT(".T_") + name;

		// Loaded by the class default object, so other instances (which may be constructed on the async loading thread) only need to find it
		UTexture2D* sprite = FindObject<UTexture2D>(nullptr, *path);

		if (!sprite && IsInGameThread())
			sprite = LoadObject<UTexture2D>(nullptr, *path, nullptr, LOAD_NoWarn | LOAD_Quiet);

		if (sprite)
		{
			billboard->Sprite = sprite;
			billboard->SpriteInfo.Category = TEXT("Sounds");
			billboard->SpriteInfo.DisplayName = NSLOCTEXT("SpriteCategory", "Sounds", "Sounds");
			return;
		}
	}
}
#endif
