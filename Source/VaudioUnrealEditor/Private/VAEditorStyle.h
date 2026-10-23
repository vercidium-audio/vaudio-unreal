#pragma once

#include "CoreMinimal.h"

// The VA logo as the class icon (Outliner, Place Actors, Details) of every placeable actor. Subclasses of a registered class, e.g. AVAListener and the raytraced sources under AVAEmitter, inherit its icon
namespace VAEditorStyle
{
	void Register();
	void Unregister();
}
