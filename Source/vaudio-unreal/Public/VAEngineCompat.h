#pragma once

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
