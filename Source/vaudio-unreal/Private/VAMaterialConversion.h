#pragma once

#include "CoreMinimal.h"
#include "VAMaterialComponent.h"

extern "C" {
#include "vaudio.h"
}

class AVAWorld;
class UVAMaterialBase;

// Converts the Blueprint-facing material enum to the SDK's VAMaterialType.
VAMaterialType EVAMaterialToVA(EVAMaterial Material);

// The SDK material ID for asset if set, otherwise for the built-in material. Warns as reporter and returns false if asset isn't in world's Materials array. context is appended to the warning, e.g. " (override for component 'Mesh')"
bool VAResolveMaterialId(const UVAMaterialBase* asset, EVAMaterial material, const AVAWorld* world, const UObject* reporter, const FString& context, int32& outMaterialId);
