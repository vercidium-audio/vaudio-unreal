#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

extern "C" {
#include "vaudio.h"
}

DECLARE_LOG_CATEGORY_EXTERN(LogVAudio, Log, All);

const TCHAR* VAResultToString(VAResult result);

// An actor's label, or "<owner label>.<component name>" for a component
FString VAObjectName(const UObject* object);

// On-screen messages are keyed per (object, slot), so different objects and call sites never overwrite each other's messages. Explicit slots (below) are for messages that are updated every tick or cleared later, everything else gets a slot from its call site
enum class EVAMessageSlot : uint32
{
	Status = 1,
	AttenuationStatus,
	SourceStatus,
	AmbientFilter,
	ListenerEAX,
	ListenerStatus,
	PrimitiveStatus,
	RaytracingTime,
	InvalidMaterials,
	NoGroupedEAX,

	// + offset, see EVAVisualisationMaterialWarningOffset
	VisualisationMaterial = 0x100,

	// + grouped EAX zone index
	GroupedEAX = 0x1000,

	// + target index
	TargetStatus = 0x10000,
};

uint64 VAMessageKey(const UObject* object, uint32 slot);
uint64 VAMessageKey(const UObject* object, EVAMessageSlot slot, uint32 offset = 0);
uint32 VACallSiteSlot(const char* file, int32 line);

// How long warnings and errors stay on screen
constexpr float VAMessageSeconds = 10.0f;

// On-screen messages don't exist in Shipping builds, so these are only ever used alongside a log line, or for per-tick debug status
void VAShowMessage(uint64 key, float seconds, const FColor& color, const FString& message);
void VAClearMessage(uint64 key);

// Writes to LogVAudio and, for warnings and errors, to the screen. Use the macros below rather than calling this directly
void VAReport(ELogVerbosity::Type verbosity, const UObject* object, uint64 messageKey, const FString& message);

// Passed to vaWorldSetLogCallback / vaEmitterSetLogCallback / vaEmitterSetLogErrorCallback
void VASdkLogCallback(const char* message);
void VASdkLogErrorCallback(const char* message);

#define VA_CALL_SITE_KEY(Object) VAMessageKey(Object, VACallSiteSlot(__FILE__, __LINE__))
#define VA_RESULT_SUFFIX(Result) FString::Printf(TEXT(" Error code: %s"), VAResultToString(Result))

#define VA_LOG(Format, ...) VAReport(ELogVerbosity::Log, nullptr, 0, FString::Printf(Format, ##__VA_ARGS__))
#define VA_WARN(Format, ...) VAReport(ELogVerbosity::Warning, nullptr, VA_CALL_SITE_KEY(nullptr), FString::Printf(Format, ##__VA_ARGS__))
#define VA_ERROR(Format, ...) VAReport(ELogVerbosity::Error, nullptr, VA_CALL_SITE_KEY(nullptr), FString::Printf(Format, ##__VA_ARGS__))

#define VA_LOG_RESULT(Result, Format, ...) VAReport(ELogVerbosity::Log, nullptr, 0, FString::Printf(Format, ##__VA_ARGS__) + VA_RESULT_SUFFIX(Result))
#define VA_WARN_RESULT(Result, Format, ...) VAReport(ELogVerbosity::Warning, nullptr, VA_CALL_SITE_KEY(nullptr), FString::Printf(Format, ##__VA_ARGS__) + VA_RESULT_SUFFIX(Result))
#define VA_ERROR_RESULT(Result, Format, ...) VAReport(ELogVerbosity::Error, nullptr, VA_CALL_SITE_KEY(nullptr), FString::Printf(Format, ##__VA_ARGS__) + VA_RESULT_SUFFIX(Result))

// Prefixed with VAObjectName(this)
#define VA_LOG_NAMED(Format, ...) VAReport(ELogVerbosity::Log, this, 0, FString::Printf(Format, ##__VA_ARGS__))
#define VA_WARN_NAMED(Format, ...) VAReport(ELogVerbosity::Warning, this, VA_CALL_SITE_KEY(this), FString::Printf(Format, ##__VA_ARGS__))
#define VA_ERROR_NAMED(Format, ...) VAReport(ELogVerbosity::Error, this, VA_CALL_SITE_KEY(this), FString::Printf(Format, ##__VA_ARGS__))

#define VA_LOG_NAMED_RESULT(Result, Format, ...) VAReport(ELogVerbosity::Log, this, 0, FString::Printf(Format, ##__VA_ARGS__) + VA_RESULT_SUFFIX(Result))
#define VA_WARN_NAMED_RESULT(Result, Format, ...) VAReport(ELogVerbosity::Warning, this, VA_CALL_SITE_KEY(this), FString::Printf(Format, ##__VA_ARGS__) + VA_RESULT_SUFFIX(Result))
#define VA_ERROR_NAMED_RESULT(Result, Format, ...) VAReport(ELogVerbosity::Error, this, VA_CALL_SITE_KEY(this), FString::Printf(Format, ##__VA_ARGS__) + VA_RESULT_SUFFIX(Result))

// Same as VA_WARN_NAMED, but keyed by an explicit slot so the message can be cleared later with VAClearMessage(VAMessageKey(this, Slot))
#define VA_WARN_NAMED_SLOT(Slot, Format, ...) VAReport(ELogVerbosity::Warning, this, VAMessageKey(this, Slot), FString::Printf(Format, ##__VA_ARGS__))
