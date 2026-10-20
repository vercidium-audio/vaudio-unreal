#include "VAAmbientSoundConversion.h"
#include "Components/AudioComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ScopedTransaction.h"
#include "Sound/AmbientSound.h"
#include "Sound/SoundBase.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "VASource.h"

#define LOCTEXT_NAMESPACE "VAAmbientSoundConversion"

DEFINE_LOG_CATEGORY_STATIC(LogVAAmbientSoundConversion, Log, All);

AVASource* VAAmbientSoundConversion::Convert(AAmbientSound* ambientSound, TArray<FString>& outWarnings)
{
	UWorld* world = ambientSound ? ambientSound->GetWorld() : nullptr;

	if (!world)
		return nullptr;

	const FString label = ambientSound->GetActorLabel();

	FActorSpawnParameters params;
	params.OverrideLevel = ambientSound->GetLevel();
	params.ObjectFlags = RF_Transactional;
	params.bDeferConstruction = true;

	const FTransform transform = ambientSound->GetActorTransform();
	AVASource* source = world->SpawnActor<AVASource>(AVASource::StaticClass(), transform, params);

	if (!source)
		return nullptr;

	if (UAudioComponent* component = ambientSound->GetAudioComponent())
	{
		source->SourceSound = component->Sound;
		source->bAutoPlay = component->bAutoActivate;
		source->VolumeMultiplier = component->VolumeMultiplier;
		source->PitchMultiplier = component->PitchMultiplier;

		// A VASource only uses its sound's own Sound Attenuation, and its low pass filter is the muffling
		if (component->bOverrideAttenuation)
			outWarnings.Add(FString::Printf(TEXT("'%s' overrides attenuation on its audio component. A VASource uses the Sound Attenuation of its sound, so add one there"), *label));
		else if (component->AttenuationSettings && (!component->Sound || component->Sound->AttenuationSettings != component->AttenuationSettings))
			outWarnings.Add(FString::Printf(TEXT("'%s' sets Attenuation Settings '%s' on its audio component. A VASource uses the Sound Attenuation of its sound, so set it there"), *label, *component->AttenuationSettings->GetName()));

		if (!component->bAllowSpatialization)
			outWarnings.Add(FString::Printf(TEXT("'%s' isn't spatialised. A VASource always is, use a VASourceRelative or VASourceAmbient for a 2D sound"), *label));

		if (component->bEnableLowPassFilter || component->bEnableHighPassFilter)
			outWarnings.Add(FString::Printf(TEXT("'%s' has a low or high pass filter on its audio component. A VASource's low pass filter is its muffling, so these weren't kept"), *label));
	}

	if (!source->SourceSound)
		outWarnings.Add(FString::Printf(TEXT("'%s' has no sound, so the VASource won't play until SourceSound is set"), *label));

	source->FinishSpawning(transform);

	source->Tags = ambientSound->Tags;
	source->SetFolderPath(ambientSound->GetFolderPath());

	if (AActor* parent = ambientSound->GetAttachParentActor())
		source->AttachToActor(parent, FAttachmentTransformRules::KeepWorldTransform, ambientSound->GetAttachParentSocketName());

	TArray<AActor*> children;
	ambientSound->GetAttachedActors(children);

	for (AActor* child : children)
	{
		child->Modify();
		child->AttachToActor(source, FAttachmentTransformRules::KeepWorldTransform);
	}

	world->EditorDestroyActor(ambientSound, true);

	// After the ambient sound is gone, so the label doesn't get a number suffix
	source->SetActorLabel(label);
	return source;
}

static TArray<AAmbientSound*> GetSelectedAmbientSounds()
{
	TArray<AAmbientSound*> ambientSounds;

	if (!GEditor)
		return ambientSounds;

	for (FSelectionIterator it(GEditor->GetSelectedActorIterator()); it; ++it)
	{
		AAmbientSound* ambientSound = Cast<AAmbientSound>(*it);

		// Converting a PIE copy would be lost when PIE ends
		if (ambientSound && ambientSound->GetWorld() && !ambientSound->GetWorld()->IsGameWorld())
			ambientSounds.Add(ambientSound);
	}

	return ambientSounds;
}

static void ConvertSelected()
{
	TArray<AAmbientSound*> ambientSounds = GetSelectedAmbientSounds();

	if (ambientSounds.Num() == 0)
		return;

	FScopedTransaction transaction(LOCTEXT("ConvertTransaction", "Convert to VASource"));

	TArray<FString> warnings;
	TArray<AVASource*> sources;

	GEditor->SelectNone(false, true);

	for (AAmbientSound* ambientSound : ambientSounds)
		if (AVASource* source = VAAmbientSoundConversion::Convert(ambientSound, warnings))
			sources.Add(source);

	for (AVASource* source : sources)
		GEditor->SelectActor(source, true, false);

	GEditor->NoteSelectionChange();

	for (const FString& warning : warnings)
		UE_LOG(LogVAAmbientSoundConversion, Warning, TEXT("%s"), *warning);

	FText message = warnings.Num() == 0
		? FText::Format(LOCTEXT("Converted", "Converted {0} ambient sound(s) to VASource"), sources.Num())
		: FText::Format(LOCTEXT("ConvertedWithWarnings", "Converted {0} ambient sound(s) to VASource with {1} warning(s), see the Output Log"), sources.Num(), warnings.Num());

	FNotificationInfo info(message);
	info.ExpireDuration = warnings.Num() == 0 ? 4.0f : 8.0f;
	FSlateNotificationManager::Get().AddNotification(info);
}

static const FName MenuOwner("VAAmbientSoundConversion");
static FDelegateHandle StartupCallbackHandle;

static void ExtendActorContextMenu()
{
	FToolMenuOwnerScoped ownerScoped(MenuOwner);
	UToolMenu* menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu");

	menu->AddDynamicSection("VAAmbientSoundConversion", FNewToolMenuDelegate::CreateLambda([](UToolMenu* dynamicMenu)
	{
		if (GetSelectedAmbientSounds().Num() == 0)
			return;

		FToolMenuSection& section = dynamicMenu->AddSection("VercidiumAudio", LOCTEXT("SectionLabel", "Vercidium Audio"));
		section.AddMenuEntry(
			"ConvertToVASource",
			LOCTEXT("ConvertLabel", "Convert to VASource"),
			LOCTEXT("ConvertTooltip", "Replaces the selected ambient sounds with raytraced VASources, keeping their sound, transform, Auto Activate and volume and pitch multipliers"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&ConvertSelected)));
	}));
}

void VAAmbientSoundConversion::RegisterMenus()
{
	StartupCallbackHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&ExtendActorContextMenu));
}

void VAAmbientSoundConversion::UnregisterMenus()
{
	UToolMenus::UnRegisterStartupCallback(StartupCallbackHandle);

	if (UToolMenus::TryGet())
		UToolMenus::UnregisterOwner(MenuOwner);
}

#undef LOCTEXT_NAMESPACE
