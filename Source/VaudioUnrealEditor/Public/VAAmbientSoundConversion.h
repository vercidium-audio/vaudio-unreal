#pragma once

#include "CoreMinimal.h"

class AAmbientSound;
class AVASource;

// The level editor's "Convert to VASource" right-click action on AAmbientSound actors
namespace VAAmbientSoundConversion
{
	// Replaces the ambient sound with a VASource in the same level, with its sound, transform, label, folder, tags, attach parent and attached children. Auto Activate becomes bAutoPlay, and the volume and pitch multipliers carry over. Settings a VASource can't express (e.g. attenuation overrides) are added to outWarnings, and the ambient sound is destroyed either way. Call inside a transaction to make it undoable
	VAUDIOUNREALEDITOR_API AVASource* Convert(AAmbientSound* ambientSound, TArray<FString>& outWarnings);

	// Adds the action to the level editor's actor context menu once UToolMenus is ready, and removes it again
	void RegisterMenus();
	void UnregisterMenus();
}
