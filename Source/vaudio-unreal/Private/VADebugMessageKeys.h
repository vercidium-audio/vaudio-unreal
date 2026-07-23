#pragma once

#include "CoreMinimal.h"

// Warnings are always orange with a 0.0f lifetime (reissued every tick to stay alive), so this covers the common case.
// va_list overload lets callers that already collected their own '...' (e.g. a subclass's own DisplayWarning helper) forward it here.
inline void DisplayDebugWarningArgs(uint64 messageID, const TCHAR* fmt, va_list args)
{
	if (!GEngine)
		return;

	TCHAR buffer[1024];
	FCString::GetVarArgs(buffer, UE_ARRAY_COUNT(buffer), fmt, args);

	GEngine->AddOnScreenDebugMessage(messageID, 60.0f, FColor::Orange, buffer);
}

inline void DisplayDebugWarning(uint64 messageID, const TCHAR* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	DisplayDebugWarningArgs(messageID, fmt, args);
	va_end(args);
}

enum EVADebugMessageKey : uint64
{
	VAKeyBase						= 0x5641554400000000ULL,
	VARaytracingTimeMessage			= VAKeyBase + 1,
	VAListenerStatusMessage			= VAKeyBase + 2,
	VAAmbientFilterMessage			= VAKeyBase + 3,
	VANoMainListenerMessage			= VAKeyBase + 4,
	VAPrimitiveStatusMessage		= VAKeyBase + 5,
	VAGroupedEAXStatusMessage		= VAKeyBase + 6,
	VAInvalidMaterialsMessage		= VAKeyBase + 7,
	VAListenerEAXMessage			= VAKeyBase + 8,
	VAListenerNoAmbientRaysMessage  = VAKeyBase + 9,

	VAEmitterStatus					= VAKeyBase + 0x1000,
	VAGroupedEAXMessageBase			= VAKeyBase + 0x2000,
	VAEmitterMessageBase			= VAKeyBase + 0x100000,
	VAEmitterMessageStride			= 1000,
};

// Offsets within an emitter's message stride
enum EVAEmitterMessageOffset : uint32
{
	VAEmitterSubmixStatus         = 0,
	VAEmitterTargetStatus         = 1,
	VAEmitterAttenuationStatus    = 2,
	VAEmitterSourceStatus         = 3,
	VAEmitterVirtualizationStatus = 4,
};
