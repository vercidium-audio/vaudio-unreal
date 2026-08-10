#pragma once

extern "C" {
#include "vaudio.h"
}

const float MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
const float MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;

// FVector position helpers
static inline void vaEmitterSetPositionUnreal(VAEmitter* emitter, const FVector& position)
{
	vaEmitterSetPosition(emitter, vaVectorCreate((float)position.X, (float)position.Y, (float)position.Z));
}

// VAColor (byte RGBA) <-> FColor (byte RGBA) helpers. FColor rather than FLinearColor since these
// are raw debug-visualisation bytes with no gamma/lighting meaning - a direct copy, no sRGB round-trip.
static inline VAColor FColorToVA(const FColor& color)
{
	return vaColorCreate(color.R, color.G, color.B, color.A);
}

static inline FColor VAColorToFColor(const VAColor& color)
{
	return FColor(color.r, color.g, color.b, color.a);
}

static inline FVector VAVectorToFVector(const VAVector& vector)
{
	return FVector(vector.x, vector.y, vector.z);
}

static inline void vaSpherePrimitiveSetCenterUnreal(VASpherePrimitive* sphere, const FVector& center)
{
	vaSpherePrimitiveSetCenter(sphere, vaVectorCreate((float)center.X, (float)center.Y, (float)center.Z));
}

static inline void vaPrismPrimitiveSetSizeUnreal(VAPrismPrimitive* prism, const FVector& size)
{
	vaPrismPrimitiveSetSize(prism, vaVectorCreate((float)size.X, (float)size.Y, (float)size.Z));
}

static inline void vaWorldSetPositionUnreal(VAWorld* world, const FVector& position)
{
	vaWorldSetPosition(world, vaVectorCreate((float)position.X, (float)position.Y, (float)position.Z));
}

static inline void vaWorldSetSizeUnreal(VAWorld* world, const FVector& position)
{
	vaWorldSetSize(world, vaVectorCreate((float)position.X, (float)position.Y, (float)position.Z));
}

static VAMatrix MakeRotTransMatrix(const FTransform& transform)
{
	FQuat   rot = transform.GetRotation();
	FVector pos = transform.GetTranslation();

	FVector AxX = rot.GetAxisX();
	FVector AxY = rot.GetAxisY();
	FVector AxZ = rot.GetAxisZ();

	return vaMatrixCreate(
		(float)AxX.X, (float)AxX.Y, (float)AxX.Z, 0.f,
		(float)AxY.X, (float)AxY.Y, (float)AxY.Z, 0.f,
		(float)AxZ.X, (float)AxZ.Y, (float)AxZ.Z, 0.f,
		(float)pos.X, (float)pos.Y, (float)pos.Z, 1.f
	);
}

static VAMatrix MakeScaleRotTransMatrix(const FTransform& T)
{
	VAMatrix RotTrans = MakeRotTransMatrix(T);
	FVector Scale = T.GetScale3D();
	VAMatrix ScaleMat = vaMatrixCreateScale((float)Scale.X, (float)Scale.Y, (float)Scale.Z);
	return vaMatrixMultiply(&ScaleMat, &RotTrans);
}

static inline void vaCapsulePrimitiveSetTransformUnreal(VACapsulePrimitive* capsule, const FTransform& transform)
{
	VAMatrix matrix = MakeRotTransMatrix(transform);
	vaCapsulePrimitiveSetTransform(capsule, &matrix);
}

static inline void vaPrismPrimitiveSetTransformUnreal(VAPrismPrimitive* prism, const FTransform& transform)
{
	VAMatrix matrix = MakeRotTransMatrix(transform);
	vaPrismPrimitiveSetTransform(prism, &matrix);
}

static inline void vaMeshPrimitiveSetTransformUnreal(VAMeshPrimitive* mesh, const FTransform& transform)
{
	VAMatrix matrix = MakeScaleRotTransMatrix(transform);
	vaMeshPrimitiveSetTransform(mesh, &matrix);
}