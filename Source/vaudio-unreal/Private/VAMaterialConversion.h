// Human-created

#pragma once

#include "CoreMinimal.h"
#include "VAMaterialTypes.h"

extern "C" {
#include "vaudio.h"
}

// Convert a Blueprint-facing material enum to the SDK's VAMaterialType
VAMaterialType EVAMaterialToVA(EVAMaterial Material);
