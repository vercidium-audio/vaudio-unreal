#pragma once

#include "CoreMinimal.h"

class UBillboardComponent;

#if WITH_EDITORONLY_DATA
// Gives a billboard the plugin's Content/Icons/T_<class> sprite for the actor's class, or for its nearest parent with one, so Blueprint subclasses get their native parent's. Call it from the actor's constructor
void VASetEditorSprite(UBillboardComponent* billboard, const UClass* actorClass);
#endif
