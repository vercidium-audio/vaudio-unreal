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

static TSharedPtr<FSlateStyleSet> Style;

void VAEditorStyle::Register()
{
	TSharedPtr<IPlugin> plugin = IPluginManager::Get().FindPlugin(TEXT("vaudio-unreal"));

	if (Style.IsValid() || !plugin)
		return;

	Style = MakeShared<FSlateStyleSet>(TEXT("VaudioUnrealStyle"));
	Style->SetContentRoot(FPaths::Combine(plugin->GetBaseDir(), TEXT("Resources")));

	const UClass* classes[] = {
		AVAWorld::StaticClass(),
		AVAEmitter::StaticClass(),
		AVASourceLeech::StaticClass(),
		AVASourceRelative::StaticClass(),
		AVASourceAmbient::StaticClass(),
	};

	for (const UClass* type : classes)
	{
		Style->Set(*(TEXT("ClassIcon.") + type->GetName()), new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/VALogo"), TEXT(".svg")), FVector2D(16.0, 16.0)));
		Style->Set(*(TEXT("ClassThumbnail.") + type->GetName()), new FSlateVectorImageBrush(Style->RootToContentDir(TEXT("Icons/VALogo"), TEXT(".svg")), FVector2D(64.0, 64.0)));
	}

	FSlateStyleRegistry::RegisterSlateStyle(*Style);
}

void VAEditorStyle::Unregister()
{
	if (!Style.IsValid())
		return;

	FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
	Style.Reset();
}
