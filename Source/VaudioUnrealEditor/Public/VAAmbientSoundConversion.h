// AI-created

#pragma once

#include "CoreMinimal.h"

class AAmbientSound;
class AVASource;
class AVASourceAmbient;

// The level editor's "Convert to VASource" and "Convert to VASourceAmbient" right-click actions on AAmbientSound actors
namespace VAAmbientSoundConversion
{
	// Replaces the ambient sound with a VASource in the same level, with its sound, transform, label, folder, tags, attach parent and attached children. Auto Activate becomes bAutoPlay, and the volume and pitch multipliers carry over. Settings a VASource can't express (e.g. attenuation overrides) are added to outWarnings, and the ambient sound is destroyed either way. Call inside a transaction to make it undoable
	VAUDIOUNREALEDITOR_API AVASource* ConvertToSource(AAmbientSound* ambientSound, TArray<FString>& outWarnings);

	// Same as ConvertToSource, but replaces it with a non-spatialised VASourceAmbient that's muffled by the listener's ambient filter. A VASourceAmbient always plays once play begins, so Auto Activate being off is a warning
	VAUDIOUNREALEDITOR_API AVASourceAmbient* ConvertToAmbient(AAmbientSound* ambientSound, TArray<FString>& outWarnings);

	// Adds the actions to the level editor's actor context menu once UToolMenus is ready, and removes them again
	void RegisterMenus();
	void UnregisterMenus();
}
