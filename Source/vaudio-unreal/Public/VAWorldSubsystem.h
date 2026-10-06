#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VAWorldSubsystem.generated.h"

class AVAWorld;

// Tracks the single VAWorld in a game world, so actors and components find it without a reference, like Godot's find_va_world
UCLASS()
class VAUDIOUNREAL_API UVAWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Null until a VAWorld begins play
	AVAWorld* GetVAWorld() const { return VAWorld.Get(); }

	// Called from AVAWorld::BeginPlay once its vaWorld exists. AVAWorld rejects a second VAWorld before calling this
	void RegisterWorld(AVAWorld* world);
	void UnregisterWorld(AVAWorld* world);

	// Broadcast when a VAWorld begins play, so actors that began play before it can join it
	FSimpleMulticastDelegate OnWorldRegistered;

protected:
	// Editor worlds never begin play, so actors there never look for a VAWorld
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TWeakObjectPtr<AVAWorld> VAWorld;
};
