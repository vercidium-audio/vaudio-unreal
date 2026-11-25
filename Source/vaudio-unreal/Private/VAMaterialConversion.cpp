// AI-created

#include "VAMaterialConversion.h"
#include "VAMaterial.h"
#include "VAWorld.h"
#include "VALog.h"


VAMaterialType EVAMaterialToVA(EVAMaterial Material)
{
	switch (Material)
	{
	case EVAMaterial::Brick:            return VAMaterialBrick;
	case EVAMaterial::Cloth:            return VAMaterialCloth;
	case EVAMaterial::Concrete:         return VAMaterialConcrete;
	case EVAMaterial::ConcretePolished: return VAMaterialConcretePolished;
	case EVAMaterial::Dirt:             return VAMaterialDirt;
	case EVAMaterial::Glass:            return VAMaterialGlass;
	case EVAMaterial::Grass:            return VAMaterialGrass;
	case EVAMaterial::Gravel:           return VAMaterialGravel;
	case EVAMaterial::Gyprock:          return VAMaterialGyprock;
	case EVAMaterial::Ice:              return VAMaterialIce;
	case EVAMaterial::Leaf:             return VAMaterialLeaf;
	case EVAMaterial::Marble:           return VAMaterialMarble;
	case EVAMaterial::Metal:            return VAMaterialMetal;
	case EVAMaterial::Mud:              return VAMaterialMud;
	case EVAMaterial::Rock:             return VAMaterialRock;
	case EVAMaterial::Sand:             return VAMaterialSand;
	case EVAMaterial::Snow:             return VAMaterialSnow;
	case EVAMaterial::Tile:             return VAMaterialTile;
	case EVAMaterial::Tree:             return VAMaterialTree;
	case EVAMaterial::Water:            return VAMaterialWater;
	case EVAMaterial::WoodIndoor:       return VAMaterialWoodIndoor;
	case EVAMaterial::WoodOutdoor:      return VAMaterialWoodOutdoor;
	default:                                return VAMaterialConcrete;
	}
}

bool VAResolveMaterialID(const UVAMaterialBase* asset, EVAMaterial material, const AVAWorld* world, const UObject* reporter, const FString& context, int32& outMaterialId)
{
	if (!asset)
	{
		outMaterialId = (int32)EVAMaterialToVA(material);
		return true;
	}

	if (!world || !world->HasMaterial(asset))
	{
		VAReport(ELogVerbosity::Warning, reporter, VA_CALL_SITE_KEY(reporter), FString::Printf(TEXT("MaterialAsset '%s' is not in the VAWorld's Materials array%s. Add it to the Materials array of the level's VAWorld."), *asset->GetName(), *context));
		return false;
	}

	return asset->GetMaterialID(world, outMaterialId);
}
