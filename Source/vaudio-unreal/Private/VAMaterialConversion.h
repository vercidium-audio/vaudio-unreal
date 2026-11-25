// Human-created

#pragma once

#include "CoreMinimal.h"
#include "VAMaterialComponent.h"

extern "C" {
#include "vaudio.h"
}

class AVAWorld;
class UVAMaterialBase;

// Convert a Blueprint-facing material enum to the SDK's VAMaterialType.
VAMaterialType EVAMaterialToVA(EVAMaterial Material);

// Get the SDK material ID for the asset (if provided), or the default material.
// Returns false and warns if the asset isn't in the world's Materials array
bool VAResolveMaterialID(const UVAMaterialBase* asset, EVAMaterial material, const AVAWorld* world, const UObject* reporter, const FString& context, int32& outMaterialId);
