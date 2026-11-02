#pragma once

#include "GameFramework/Actor.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"

// Mesh vertex data is float on every engine, but 4.27 has no FVector3f/FVector2f because its FVector/FVector2D are already float
#if UE_VERSION_OLDER_THAN(5, 0, 0)
using FVAVector3f = FVector;
using FVAVector2f = FVector2D;
#else
using FVAVector3f = FVector3f;
using FVAVector2f = FVector2f;
#endif

// 4.27 UHT rejects double in Blueprint-exposed members, so those are float there (declared with marker blocks)
#if UE_VERSION_OLDER_THAN(5, 0, 0)
using VABlueprintReal = float;
#else
using VABlueprintReal = double;
#endif

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
