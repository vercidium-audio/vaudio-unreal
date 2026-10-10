#include "VAWorldSubsystem.h"
#include "VAWorld.h"

void UVAWorldSubsystem::RegisterWorld(AVAWorld* world)
{
	VAWorld = world;
	OnWorldRegistered.Broadcast();
}

void UVAWorldSubsystem::UnregisterWorld(AVAWorld* world)
{
	if (VAWorld.Get() != world)
		return;

	VAWorld.Reset();
	OnWorldUnregistered.Broadcast();
}

bool UVAWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
