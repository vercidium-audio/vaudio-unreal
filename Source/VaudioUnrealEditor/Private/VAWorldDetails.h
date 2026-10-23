#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

// Replaces the Transform section with a Location-only one, since the bounds are always an axis-aligned box. Like Godot's VAWorld::_validate_property
class FVAWorldDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
