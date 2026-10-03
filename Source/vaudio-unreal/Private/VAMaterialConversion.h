#pragma once

#include "CoreMinimal.h"
#include "VAMaterialComponent.h"

extern "C" {
#include "vaudio.h"
}

// Converts the Blueprint-facing material enum to the SDK's VAMaterialType.
VAMaterialType EVAMaterialToVA(EVAMaterial Material);
