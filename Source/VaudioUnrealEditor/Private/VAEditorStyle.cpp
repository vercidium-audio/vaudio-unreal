#include "VAEditorStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "VAEmitter.h"
#include "VASourceAmbient.h"
#include "VASourceLeech.h"
#include "VASourceRelative.h"
#include "VAWorld.h"

static TSharedPtr<FSlateStyleSet> StyleSet;

// 4.27 has no SVG brushes, so it uses PNGs rendered from VALogo.svg. They're not in Icons, as every PNG in there is imported as a billboard sprite
static FSlateBrush* CreateLogoBrush(int32 size)
{
#if UE_VERSION_OLDER_THAN(5, 0, 0)
	return new FSlateImageBrush(StyleSet->RootToContentDir(FString::Printf(TEXT("VALogo%d"), size), TEXT(".png")), FVector2D(size, size));
#else
	return new FSlateVectorImageBrush(StyleSet->RootToContentDir(TEXT("Icons/VALogo"), TEXT(".svg")), FVector2D(size, size));
#endif
}

void VAEditorStyle::Register()
{
	TSharedPtr<IPlugin> plugin = IPluginManager::Get().FindPlugin(TEXT("vaudio-unreal"));

	if (StyleSet.IsValid() || !plugin)
		return;

	StyleSet = MakeShared<FSlateStyleSet>(TEXT("VaudioUnrealStyle"));
	StyleSet->SetContentRoot(FPaths::Combine(plugin->GetBaseDir(), TEXT("Resources")));

	const UClass* classes[] = {
		AVAWorld::StaticClass(),
		AVAEmitter::StaticClass(),
		AVASourceLeech::StaticClass(),
		AVASourceRelative::StaticClass(),
		AVASourceAmbient::StaticClass(),
	};

	for (const UClass* type : classes)
	{
		StyleSet->Set(*(TEXT("ClassIcon.") + type->GetName()), CreateLogoBrush(16));
		StyleSet->Set(*(TEXT("ClassThumbnail.") + type->GetName()), CreateLogoBrush(64));
	}

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void VAEditorStyle::Unregister()
{
	if (!StyleSet.IsValid())
		return;

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
	StyleSet.Reset();
}
