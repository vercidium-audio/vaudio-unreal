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

	// Called at the end of AVAWorld::EndPlay. Does nothing for a VAWorld that was never registered, e.g. a rejected second one
	void UnregisterWorld(AVAWorld* world);

	// Broadcast when a VAWorld begins play, so actors that began play before it can join it
	FSimpleMulticastDelegate OnWorldRegistered;

	// Broadcast after the VAWorld ends play and its vaWorld is destroyed, so actors that joined it let go and wait for the next one
	FSimpleMulticastDelegate OnWorldUnregistered;

protected:
	// Editor worlds never begin play, so actors there never look for a VAWorld
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TWeakObjectPtr<AVAWorld> VAWorld;
};
