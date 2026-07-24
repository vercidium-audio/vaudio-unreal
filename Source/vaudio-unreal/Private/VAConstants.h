#pragma once

extern "C" {
#include "vaudio.h"
}

const float MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
const float MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;

// Sets this emitter's world-space position directly from an Unreal FVector
static inline void vaEmitterSetPositionUnreal(VAEmitter* Emitter, const FVector& Position)
{
	vaEmitterSetPosition(Emitter, vaVectorCreate((float)Position.X, (float)Position.Y, (float)Position.Z));
}

// Sets this sphere primitive's center directly from an Unreal FVector
static inline void vaSpherePrimitiveSetCenterUnreal(VASpherePrimitive* Sphere, const FVector& Center)
{
	vaSpherePrimitiveSetCenter(Sphere, vaVectorCreate((float)Center.X, (float)Center.Y, (float)Center.Z));
}
