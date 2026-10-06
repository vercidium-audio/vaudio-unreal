#include "VALog.h"

#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Misc/Crc.h"

DEFINE_LOG_CATEGORY(LogVAudio);

const TCHAR* VAResultToString(VAResult result)
{
	switch (result)
	{
		case VA_SUCCESS:                   return TEXT("VA_SUCCESS");
		case VA_INVALID_VALUE:             return TEXT("VA_INVALID_VALUE");
		case VA_OUT_OF_RANGE:              return TEXT("VA_OUT_OF_RANGE");
		case VA_ALREADY_EXISTS:            return TEXT("VA_ALREADY_EXISTS");
		case VA_FEATURE_DISABLED:          return TEXT("VA_FEATURE_DISABLED");
		case VA_ERROR_IN_USE:              return TEXT("VA_ERROR_IN_USE");
		case VA_INVALID_COUNT:             return TEXT("VA_INVALID_COUNT");
		case VA_WORLD_CONFLICT:            return TEXT("VA_WORLD_CONFLICT");
		case VA_ERROR_FILE_OPEN:           return TEXT("VA_ERROR_FILE_OPEN");
		case VA_ERROR_FILE_WRITE:          return TEXT("VA_ERROR_FILE_WRITE");
		case VA_ERROR_FILE_VERSION:        return TEXT("VA_ERROR_FILE_VERSION");
		case VA_ERROR_FILE_CORRUPT:        return TEXT("VA_ERROR_FILE_CORRUPT");
		case VA_INVALID_MATERIAL:          return TEXT("VA_INVALID_MATERIAL");
		case VA_MATERIAL_DOES_NOT_EXIST:   return TEXT("VA_MATERIAL_DOES_NOT_EXIST");
		case VA_NOT_ADDED_TO_WORLD:        return TEXT("VA_NOT_ADDED_TO_WORLD");
		case VA_INSUFFICIENT_VERTICES:     return TEXT("VA_INSUFFICIENT_VERTICES");
		case VA_INVALID_VERTEX_COUNT:      return TEXT("VA_INVALID_VERTEX_COUNT");
		case VA_INVALID_ARRAY:             return TEXT("VA_INVALID_ARRAY");
		case VA_INVALID_POINTER:           return TEXT("VA_INVALID_POINTER");
		case VA_UNCHANGED:                 return TEXT("VA_UNCHANGED");
		case VA_NOT_FOUND:                 return TEXT("VA_NOT_FOUND");
		case VA_STILL_RUNNING:             return TEXT("VA_STILL_RUNNING");
		case VA_PENDING_REMOVAL:           return TEXT("VA_PENDING_REMOVAL");
		case VA_CONFIG_ERROR:              return TEXT("VA_CONFIG_ERROR");
		case VA_TRUE:                      return TEXT("VA_TRUE");
		case VA_FALSE:                     return TEXT("VA_FALSE");
		case VA_WRONG_DIMENSION:           return TEXT("VA_WRONG_DIMENSION");
		case VA_MISSING_MATERIAL_CALLBACK: return TEXT("VA_MISSING_MATERIAL_CALLBACK");
		default:                           return TEXT("UNKNOWN");
	}
}

FString VAObjectName(const UObject* object)
{
	if (!object)
		return TEXT("<null>");

	if (const AActor* actor = Cast<AActor>(object))
		return actor->GetActorNameOrLabel();

	if (const UActorComponent* component = Cast<UActorComponent>(object))
	{
		if (const AActor* owner = component->GetOwner())
			return FString::Printf(TEXT("%s.%s"), *owner->GetActorNameOrLabel(), *component->GetName());
	}

	return object->GetName();
}

// Top byte tags the key as ours, the next 32 bits are the object's unique ID, and the low 24 bits are the slot. Call-site slots set bit 23 so they never collide with EVAMessageSlot
static constexpr uint64 MessageKeyTag = 0x56ull << 56;
static constexpr uint32 SlotMask = 0xFFFFFF;
static constexpr uint32 CallSiteSlotBit = 0x800000;

uint64 VAMessageKey(const UObject* object, uint32 slot)
{
	uint64 id = object ? (uint64)(uint32)object->GetUniqueID() : 0xFFFFFFFFull;
	return MessageKeyTag | (id << 24) | (slot & SlotMask);
}

uint64 VAMessageKey(const UObject* object, EVAMessageSlot slot, uint32 offset)
{
	return VAMessageKey(object, ((uint32)slot + offset) & ~CallSiteSlotBit);
}

uint32 VACallSiteSlot(const char* file, int32 line)
{
	return ((FCrc::StrCrc32(file) * 31u + (uint32)line) & SlotMask) | CallSiteSlotBit;
}

void VAShowMessage(uint64 key, float seconds, const FColor& color, const FString& message)
{
	if (GEngine)
		GEngine->AddOnScreenDebugMessage(key, seconds, color, message);
}

void VAClearMessage(uint64 key)
{
	if (GEngine)
		GEngine->RemoveOnScreenDebugMessage(key);
}

void VAReport(ELogVerbosity::Type verbosity, const UObject* object, uint64 messageKey, const FString& message)
{
	FString line = object ? FString::Printf(TEXT("%s: %s"), *VAObjectName(object), *message) : message;

	// UE_LOG needs a compile-time verbosity
	switch (verbosity)
	{
		case ELogVerbosity::Error:
			UE_LOG(LogVAudio, Error, TEXT("%s"), *line);
			VAShowMessage(messageKey, VAMessageSeconds, FColor::Red, TEXT("[VA] ") + line);
			break;
		case ELogVerbosity::Warning:
			UE_LOG(LogVAudio, Warning, TEXT("%s"), *line);
			VAShowMessage(messageKey, VAMessageSeconds, FColor::Orange, TEXT("[VA] ") + line);
			break;
		default:
			UE_LOG(LogVAudio, Log, TEXT("%s"), *line);
			break;
	}
}

// The user data is null for a handle that isn't attached to an actor, e.g. during teardown
static FString VASdkLogPrefix(void* userData)
{
	return userData ? FString::Printf(TEXT("[SDK] %s: "), *VAObjectName(static_cast<const UObject*>(userData))) : FString(TEXT("[SDK] "));
}

void VASdkWorldLogCallback(VAWorld* world, const char* message)
{
	UE_LOG(LogVAudio, Log, TEXT("%s%hs"), *VASdkLogPrefix(vaWorldGetUserData(world)), message ? message : "(null)");
}

void VASdkEmitterLogCallback(VAEmitter* emitter, const char* message)
{
	UE_LOG(LogVAudio, Log, TEXT("%s%hs"), *VASdkLogPrefix(vaEmitterGetUserData(emitter)), message ? message : "(null)");
}

void VASdkEmitterLogErrorCallback(VAEmitter* emitter, const char* message)
{
	UE_LOG(LogVAudio, Error, TEXT("%s%hs"), *VASdkLogPrefix(vaEmitterGetUserData(emitter)), message ? message : "(null)");
}
