#pragma once

#include "CoreMinimal.h"
#include "VAudioMaterialComponent.h"

extern "C" {
#include "vaudio.h"
}

// Converts the Blueprint-facing material enum to the SDK's VAMaterialType.
VAMaterialType EVAudioMaterialToVA(EVAudioMaterial Material);
