#pragma once

extern "C" {
#include "vaudio.h"
}

// The plugin only supports the double precision SDK, so FVector's doubles go into the SDK without narrowing
static_assert(VA_FLOAT_SIZE == 8, "vaudio-unreal needs the double precision Vercidium Audio SDK (3d/native-double), but Source/ThirdParty/vaudio/include/vaudio.h is the single precision one");

static inline VAVector FVectorToVAVector(const FVector& vector)
{
	return vaVectorCreate((VAFloat)vector.X, (VAFloat)vector.Y, (VAFloat)vector.Z);
}

// FVector position helpers
static inline void vaEmitterSetPositionUnreal(VAEmitter* emitter, const FVector& position)
{
	vaEmitterSetPosition(emitter, FVectorToVAVector(position));
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
	vaSpherePrimitiveSetCenter(sphere, FVectorToVAVector(center));
}

static inline void vaPrismPrimitiveSetSizeUnreal(VAPrismPrimitive* prism, const FVector& size)
{
	vaPrismPrimitiveSetSize(prism, FVectorToVAVector(size));
}

static inline void vaWorldSetPositionUnreal(VAWorld* world, const FVector& position)
{
	vaWorldSetPosition(world, FVectorToVAVector(position));
}

static inline void vaWorldSetSizeUnreal(VAWorld* world, const FVector& position)
{
	vaWorldSetSize(world, FVectorToVAVector(position));
}

static VAMatrix MakeRotTransMatrix(const FTransform& transform)
{
	FQuat   rot = transform.GetRotation();
	FVector pos = transform.GetTranslation();

	FVector AxX = rot.GetAxisX();
	FVector AxY = rot.GetAxisY();
	FVector AxZ = rot.GetAxisZ();

	return vaMatrixCreate(
		(VAFloat)AxX.X, (VAFloat)AxX.Y, (VAFloat)AxX.Z, 0,
		(VAFloat)AxY.X, (VAFloat)AxY.Y, (VAFloat)AxY.Z, 0,
		(VAFloat)AxZ.X, (VAFloat)AxZ.Y, (VAFloat)AxZ.Z, 0,
		(VAFloat)pos.X, (VAFloat)pos.Y, (VAFloat)pos.Z, 1
	);
}

static VAMatrix MakeScaleRotTransMatrix(const FTransform& T)
{
	VAMatrix RotTrans = MakeRotTransMatrix(T);
	FVector Scale = T.GetScale3D();
	VAMatrix ScaleMat = vaMatrixCreateScale((VAFloat)Scale.X, (VAFloat)Scale.Y, (VAFloat)Scale.Z);
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