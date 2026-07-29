#include "VAudioListenerComponent.h"
#include "VAudioListener.h"
#include "VADebugMessageKeys.h"

void UVAudioListenerComponent::DisplayWarning(const TCHAR* fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	DisplayDebugWarningArgs(VAEmitterMessageBase + GetUniqueID(), fmt, args);
	va_end(args);
}

UVAudioListenerComponent::UVAudioListenerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AVAudioListener* UVAudioListenerComponent::FindListenerFromAttachedActors() const
{
	AActor* Owner = GetOwner();

	if (!Owner)
		return nullptr;

	TArray<AActor*> AttachedActors;
	Owner->GetAttachedActors(AttachedActors);

	for (AActor* AttachedActor : AttachedActors)
	{
		if (AVAudioListener* Listener = Cast<AVAudioListener>(AttachedActor))
			return Listener;
	}

	return nullptr;
}

void UVAudioListenerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (VAudioListener.IsNull())
		VAudioListener = FindListenerFromAttachedActors();

	if (!GetVAudioListener())
	{
		DisplayWarning(TEXT("[VA] ListenerComponent on '%s' has no VAudioListener assigned"), *GetOwner()->GetActorNameOrLabel());
	}
}
