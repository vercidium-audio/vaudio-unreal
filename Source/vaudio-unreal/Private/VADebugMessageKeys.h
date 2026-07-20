#pragma once

#include "CoreMinimal.h"

// On-screen debug message keys (GEngine->AddOnScreenDebugMessage slots), shared between
// VAudioWorld.cpp and VAudioEmitter.cpp so their messages can't collide.
//
// AddOnScreenDebugMessage's key is a plain uint64 with no cross-plugin namespacing - any other
// plugin/game code using small numbers (e.g. 1-10, 1001-1004) could collide with ours and cause
// messages to overwrite each other on screen. VAKeyBase is 'VAUD' (0x56415544) shifted into the
// upper 32 bits, which no other plugin is realistically going to also pick, so every key below
// is namespaced under it.
//
// NOTE: UEngine::AddOnScreenDebugMessage(uint64,...) truncates Key down to int32 before storing
// it (see UnrealEngine.cpp), and the messages are drawn by iterating a TMap in insertion order,
// not sorted by key. So key VALUES here don't control on-screen top-to-bottom order at all - only
// the ORDER we call AddOnScreenDebugMessage() each tick does (newest call renders at the top).
// See VAudioWorld::Tick()/AVAudioEmitter::Tick() - calls are sequenced there so the last call each
// tick is the one meant to appear at the top. Keys only need to stay distinct per message slot.
//
// Singleton keys (VARaytracingTimeMessage..VANoMainListenerMessage) are used once per world/tick.
// VAEmitterStatus is the base for VAudioWorld's own per-emitter status line (VAEmitterStatus + EmitterIndex).
// VAGroupedEAXMessageBase is the base for the world's per-zone grouped EAX status lines
// (VAGroupedEAXMessageBase + zone index), which aren't tied to any one emitter.
// VAEmitterMessageBase is the base of a per-emitter band used by AVAudioEmitter-related messages
// (see EVAEmitterMessageOffset below), sized so no two registered emitters can collide:
// key = VAEmitterMessageBase + EmitterIndex * VAEmitterMessageStride + offset.
enum EVADebugMessageKey : uint64
{
	VAKeyBase                 = 0x5641554400000000ULL,
	VARaytracingTimeMessage   = VAKeyBase + 1,
	VAListenerStatusMessage   = VAKeyBase + 2,
	VAAmbientFilterMessage    = VAKeyBase + 3,
	VANoMainListenerMessage   = VAKeyBase + 4,
	VAPrimitiveStatusMessage  = VAKeyBase + 5,
	VAGroupedEAXStatusMessage = VAKeyBase + 6,
	VAInvalidMaterialsMessage = VAKeyBase + 7,
	VAEmitterStatus           = VAKeyBase + 0x1000,
	VAGroupedEAXMessageBase   = VAKeyBase + 0x2000,
	VAEmitterMessageBase      = VAKeyBase + 0x100000,
	VAEmitterMessageStride    = 1000,
};

// Offsets within one emitter's band (see VAEmitterMessageBase above). TargetStatus is further
// offset by the target's index within TargetEmitters (an emitter's TargetEmitters list is
// expected to stay well under VAEmitterMessageStride entries).
enum EVAEmitterMessageOffset : uint32
{
	VAEmitterSubmixStatus      = 0,
	VAEmitterTargetStatus      = 1,
	VAEmitterAttenuationStatus = 2,
	VAEmitterSourceStatus      = 3,
};
