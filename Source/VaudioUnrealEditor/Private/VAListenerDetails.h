#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

// Hides inherited AVAEmitter properties that have no effect on a listener, like Godot's VAListener::_validate_property
class FVAListenerDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
