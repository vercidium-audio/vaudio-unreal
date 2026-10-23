#include "VAEditorStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "VAEmitter.h"
#include "VASourceAmbient.h"
#include "VASourceLeech.h"
#include "VASourceRelative.h"
#include "VAWorld.h"

static TSharedPtr<FSlateStyleSet> StyleSet;

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
		StyleSet->Set(*(TEXT("ClassIcon.") + type->GetName()), new FSlateVectorImageBrush(StyleSet->RootToContentDir(TEXT("Icons/VALogo"), TEXT(".svg")), FVector2D(16.0, 16.0)));
		StyleSet->Set(*(TEXT("ClassThumbnail.") + type->GetName()), new FSlateVectorImageBrush(StyleSet->RootToContentDir(TEXT("Icons/VALogo"), TEXT(".svg")), FVector2D(64.0, 64.0)));
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
