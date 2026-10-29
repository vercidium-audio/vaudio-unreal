#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

// Hides inherited AVAEmitter properties that Godot's VARaytracedSource doesn't expose. Applies to every AVARaytracedSource subclass
class FVARaytracedSourceDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
