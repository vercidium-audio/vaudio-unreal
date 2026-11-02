#pragma once

#include "GameFramework/Actor.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"

// 5.8 deprecated the FCoreDelegates::OnPostEngineInit field in favour of GetOnPostEngineInit(), which 5.7 doesn't have
inline FSimpleMulticastDelegate& VAOnPostEngineInit()
{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
	return FCoreDelegates::OnPostEngineInit;
#else
	return FCoreDelegates::GetOnPostEngineInit();
#endif
}

// AActor::GetActorNameOrLabel doesn't exist in 4.27, where labels are only reachable through the editor-only GetActorLabel
inline FString VAActorName(const AActor* actor)
{
#if UE_VERSION_OLDER_THAN(5, 0, 0)
#if WITH_EDITOR
	const FString& label = actor->GetActorLabel();
	if (!label.IsEmpty())
	{
		return label;
	}
#endif
	return actor->GetName();
#else
	return actor->GetActorNameOrLabel();
#endif
}
