#pragma once

#include "VAEngineCompat.h"

extern "C" {
#include "vaudio.h"
}

// VAFloat matches FVector's precision on each engine, so positions go into the SDK without narrowing
static_assert(VA_FLOAT_SIZE == VA_EXPECTED_FLOAT_SIZE, "vaudio-unreal needs the " VA_SDK_PRECISION_NAME " precision Vercidium Audio SDK (" VA_SDK_FOLDER_NAME ") on this engine version, but Source/ThirdParty/vaudio/include/vaudio.h is the other precision");

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

// A plane's normal is its local Y, with its width along local X and its height along local Z. Cycling those axes puts the normal on the box's flat axis, and is a rotation rather than a mirror, so it can go in the transform. Scale goes in the plane's width and height instead
static inline void vaPlanePrimitiveSetFromBoxUnreal(VAPlanePrimitive* plane, const FVector& boxSize, const FVector& scale, const FTransform& transform)
{
	int32 normalAxis = boxSize.X <= boxSize.Y && boxSize.X <= boxSize.Z ? 0 : (boxSize.Y <= boxSize.Z ? 1 : 2);
	int32 heightAxis = (normalAxis + 1) % 3;
	int32 widthAxis = (normalAxis + 2) % 3;

	FVector size = boxSize * scale;
	vaPlanePrimitiveSetWidth(plane, (VAFloat)size[widthAxis]);
	vaPlanePrimitiveSetHeight(plane, (VAFloat)size[heightAxis]);

	VAMatrix axes = vaMatrixCreate(
		widthAxis == 0, widthAxis == 1, widthAxis == 2, 0,
		normalAxis == 0, normalAxis == 1, normalAxis == 2, 0,
		heightAxis == 0, heightAxis == 1, heightAxis == 2, 0,
		0, 0, 0, 1
	);

	VAMatrix rotTrans = MakeRotTransMatrix(transform);
	VAMatrix matrix = vaMatrixMultiply(&axes, &rotTrans);
	vaPlanePrimitiveSetTransform(plane, &matrix);
}

// A heightmap is Y-up with its grid along local X and Z. Swapping the axes around (local X to Unreal Y, Y to Z, Z to X) stands it up in Unreal's Z-up space, and is a rotation rather than a mirror, so it can go in the transform. Scale goes in the heightmap's own scale instead
static inline void vaHeightmapPrimitiveSetTransformUnreal(VAHeightmapPrimitive* heightmap, const FTransform& transform)
{
	VAMatrix axes = vaMatrixCreate(
		0, 1, 0, 0,
		0, 0, 1, 0,
		1, 0, 0, 0,
		0, 0, 0, 1
	);

	VAMatrix rotTrans = MakeRotTransMatrix(transform);
	VAMatrix matrix = vaMatrixMultiply(&axes, &rotTrans);
	vaHeightmapPrimitiveSetTransform(heightmap, &matrix);
}

static inline void vaMeshPrimitiveSetTransformUnreal(VAMeshPrimitive* mesh, const FTransform& transform)
{
	VAMatrix matrix = MakeScaleRotTransMatrix(transform);
	vaMeshPrimitiveSetTransform(mesh, &matrix);
}