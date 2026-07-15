#include "VAudioMaterialConversion.h"

VAMaterialType EVAudioMaterialToVA(EVAudioMaterial Material)
{
	switch (Material)
	{
	case EVAudioMaterial::Brick:            return VAMaterialBrick;
	case EVAudioMaterial::Cloth:            return VAMaterialCloth;
	case EVAudioMaterial::Concrete:         return VAMaterialConcrete;
	case EVAudioMaterial::ConcretePolished: return VAMaterialConcretePolished;
	case EVAudioMaterial::Dirt:             return VAMaterialDirt;
	case EVAudioMaterial::Glass:            return VAMaterialGlass;
	case EVAudioMaterial::Grass:            return VAMaterialGrass;
	case EVAudioMaterial::Gravel:           return VAMaterialGravel;
	case EVAudioMaterial::Gyprock:          return VAMaterialGyprock;
	case EVAudioMaterial::Ice:              return VAMaterialIce;
	case EVAudioMaterial::Leaf:             return VAMaterialLeaf;
	case EVAudioMaterial::Marble:           return VAMaterialMarble;
	case EVAudioMaterial::Metal:            return VAMaterialMetal;
	case EVAudioMaterial::Mud:              return VAMaterialMud;
	case EVAudioMaterial::Rock:             return VAMaterialRock;
	case EVAudioMaterial::Sand:             return VAMaterialSand;
	case EVAudioMaterial::Snow:             return VAMaterialSnow;
	case EVAudioMaterial::Tile:             return VAMaterialTile;
	case EVAudioMaterial::Tree:             return VAMaterialTree;
	case EVAudioMaterial::Water:            return VAMaterialWater;
	case EVAudioMaterial::WoodIndoor:       return VAMaterialWoodIndoor;
	case EVAudioMaterial::WoodOutdoor:      return VAMaterialWoodOutdoor;
	default:                                return VAMaterialConcrete;
	}
}
