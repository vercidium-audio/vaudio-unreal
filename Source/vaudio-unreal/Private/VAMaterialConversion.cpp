#include "VAMaterialConversion.h"

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
