// AI-created

#include "VAAmbientSoundConversion.h"
#include "Components/AudioComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/EngineVersionComparison.h"
#include "ScopedTransaction.h"
#include "Sound/AmbientSound.h"
#include "Sound/SoundBase.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "VASource.h"
#include "VASourceAmbient.h"

#define LOCTEXT_NAMESPACE "VAAmbientSoundConversion"

DEFINE_LOG_CATEGORY_STATIC(LogVAAmbientSoundConversion, Log, All);

// Spawns T in the ambient sound's place, lets configure copy the audio component's settings before construction finishes, then moves the label, folder, tags and attachments over and destroys the ambient sound
template <typename T>
static T* Replace(AAmbientSound* ambientSound, TArray<FString>& outWarnings, TFunctionRef<void(T*, UAudioComponent*, const FString&)> configure)
{
	UWorld* world = ambientSound ? ambientSound->GetWorld() : nullptr;

	if (!world)
		return nullptr;

	const FString label = ambientSound->GetActorLabel();
	const FString className = T::StaticClass()->GetDisplayNameText().ToString();

	FActorSpawnParameters params;
	params.OverrideLevel = ambientSound->GetLevel();
	params.ObjectFlags = RF_Transactional;
	params.bDeferConstruction = true;

	const FTransform transform = ambientSound->GetActorTransform();
	T* actor = world->SpawnActor<T>(T::StaticClass(), transform, params);

	if (!actor)
		return nullptr;

	UAudioComponent* component = ambientSound->GetAudioComponent();

	if (component)
	{
		actor->SourceSound = component->Sound;
		actor->VolumeMultiplier = component->VolumeMultiplier;
		actor->PitchMultiplier = component->PitchMultiplier;

#if UE_VERSION_OLDER_THAN(5, 0, 0)
		if (component->bEnableLowPassFilter)
			outWarnings.Add(FString::Printf(TEXT("'%s' has a low pass filter on its audio component. A %s's low pass filter is its muffling, so it wasn't kept"), *label, *className));
#else
		if (component->bEnableLowPassFilter || component->bEnableHighPassFilter)
			outWarnings.Add(FString::Printf(TEXT("'%s' has a low or high pass filter on its audio component. A %s's low pass filter is its muffling, so these weren't kept"), *label, *className));
#endif
	}

	configure(actor, component, label);

	if (!actor->SourceSound)
		outWarnings.Add(FString::Printf(TEXT("'%s' has no sound, so the %s won't play until SourceSound is set"), *label, *className));

	actor->FinishSpawning(transform);

	actor->Tags = ambientSound->Tags;
	actor->SetFolderPath(ambientSound->GetFolderPath());

	if (AActor* parent = ambientSound->GetAttachParentActor())
		actor->AttachToActor(parent, FAttachmentTransformRules::KeepWorldTransform, ambientSound->GetAttachParentSocketName());

	TArray<AActor*> children;
	ambientSound->GetAttachedActors(children);

	for (AActor* child : children)
	{
		child->Modify();
		child->AttachToActor(actor, FAttachmentTransformRules::KeepWorldTransform);
	}

	world->EditorDestroyActor(ambientSound, true);

	// After the ambient sound is gone, so the label doesn't get a number suffix
	actor->SetActorLabel(label);
	return actor;
}

AVASource* VAAmbientSoundConversion::ConvertToSource(AAmbientSound* ambientSound, TArray<FString>& outWarnings)
{
	return Replace<AVASource>(ambientSound, outWarnings, [&outWarnings](AVASource* source, UAudioComponent* component, const FString& label)
	{
		// An Ambient Sound outlives its sound, and a level may play it again
		source->bAutoDestroy = false;

		if (!component)
			return;

		source->bAutoPlay = component->bAutoActivate;

		// A VASource only uses its sound's own Sound Attenuation
		if (component->bOverrideAttenuation)
			outWarnings.Add(FString::Printf(TEXT("'%s' overrides attenuation on its audio component. A VASource uses the Sound Attenuation of its sound, so add one there"), *label));
		else if (component->AttenuationSettings && (!component->Sound || component->Sound->AttenuationSettings != component->AttenuationSettings))
			outWarnings.Add(FString::Printf(TEXT("'%s' sets Attenuation Settings '%s' on its audio component. A VASource uses the Sound Attenuation of its sound, so set it there"), *label, *component->AttenuationSettings->GetName()));

		if (!component->bAllowSpatialization)
			outWarnings.Add(FString::Printf(TEXT("'%s' isn't spatialised. A VASource always is, use a VASourceRelative or VASourceAmbient for a 2D sound"), *label));
	});
}

AVASourceAmbient* VAAmbientSoundConversion::ConvertToAmbient(AAmbientSound* ambientSound, TArray<FString>& outWarnings)
{
	return Replace<AVASourceAmbient>(ambientSound, outWarnings, [&outWarnings](AVASourceAmbient* ambient, UAudioComponent* component, const FString& label)
	{
		if (!component)
			return;

		if (!component->bAutoActivate)
			outWarnings.Add(FString::Printf(TEXT("'%s' has Auto Activate off. A VASourceAmbient always plays once play begins"), *label));

		// A VASourceAmbient plays in 2D, so attenuation set on the component does nothing
		if (component->bOverrideAttenuation || (component->AttenuationSettings && (!component->Sound || component->Sound->AttenuationSettings != component->AttenuationSettings)))
			outWarnings.Add(FString::Printf(TEXT("'%s' sets attenuation on its audio component. A VASourceAmbient isn't spatialised, so it wasn't kept"), *label));
	});
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

template <typename T>
static void ConvertSelected(T* (*convert)(AAmbientSound*, TArray<FString>&))
{
	TArray<AAmbientSound*> ambientSounds = GetSelectedAmbientSounds();

	if (ambientSounds.Num() == 0)
		return;

	const FText className = T::StaticClass()->GetDisplayNameText();
	FScopedTransaction transaction(FText::Format(LOCTEXT("ConvertTransaction", "Convert to {0}"), className));

	TArray<FString> warnings;
	TArray<AActor*> converted;

	GEditor->SelectNone(false, true);

	for (AAmbientSound* ambientSound : ambientSounds)
		if (AActor* actor = convert(ambientSound, warnings))
			converted.Add(actor);

	for (AActor* actor : converted)
		GEditor->SelectActor(actor, true, false);

	GEditor->NoteSelectionChange();

	for (const FString& warning : warnings)
		UE_LOG(LogVAAmbientSoundConversion, Warning, TEXT("%s"), *warning);

	FText message = warnings.Num() == 0
		? FText::Format(LOCTEXT("Converted", "Converted {0} ambient sound(s) to {1}"), converted.Num(), className)
		: FText::Format(LOCTEXT("ConvertedWithWarnings", "Converted {0} ambient sound(s) to {1} with {2} warning(s), see the Output Log"), converted.Num(), className, warnings.Num());

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
			FUIAction(FExecuteAction::CreateLambda([]() { ConvertSelected<AVASource>(&VAAmbientSoundConversion::ConvertToSource); })));
		section.AddMenuEntry(
			"ConvertToVASourceAmbient",
			LOCTEXT("ConvertAmbientLabel", "Convert to VASourceAmbient"),
			LOCTEXT("ConvertAmbientTooltip", "Replaces the selected ambient sounds with non-spatialised VASourceAmbients that are muffled by the listener's ambient filter, keeping their sound, transform and volume and pitch multipliers"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]() { ConvertSelected<AVASourceAmbient>(&VAAmbientSoundConversion::ConvertToAmbient); })));
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
