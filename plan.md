# Overview

The overall goal is:
- Less null propagation: lots of places have "if (!something) return;" with no reasoning why it could be null. Rather than letting null checks permeate the codebase, fix them early. If it genuinely can be null, leave a comment explaining why
- Logging cleanup: always use VALog() instead of directly calling VaRawLog or UE_LOG. VALog() automatically appends the name of the current object (emitter, world, etc), file name and function name


# TODO: Delay source sound playback until raytracing completes

Source: `Source/vaudio-unreal/Private/VAudioEmitter.cpp`, `AVAudioEmitter::TryInitializeEmitter()`
(the `else if (SourceSound)` branch, around the `SpawnSoundAtLocation` call).

## Problem

When a source emitter starts playing, `SourceAudioComponent` is spawned immediately via
`SpawnSoundAtLocation`, before the VA SDK has cast its first occlusion/permeation rays. The
low-pass filter (`SourceLPFPreset`) starts at `MAX_LOW_PASS_CUTOFF_FREQUENCY` (fully open), so the
sound briefly plays "clear" for the first frame(s) and is then abruptly muffled once
`Tick()` -> `ApplySourceFilter()` receives the first real `VALowPassFilter` result from
`vaEmitterGetTargetFilter()`. This is audible as a pop/jump in tone right at sound start,
particularly noticeable if the source starts heavily occluded (e.g. behind a wall).

## Solution

**Delay spawn entirely** - don't call `SpawnSoundAtLocation` until the first raytrace completes.

## Notes for implementation

- Readiness signal: `vaEmitterHasRaytracedTarget(ListenerEmitter, ThisEmitter)` from the listener's
  side (see the loop in `Tick()` that calls `ApplySourceFilter`), or track a local "have we ever
  received a non-null filter" flag inside `AVAudioEmitter` itself.
- Need to decide whether this only applies to the *listener's* view of *this* emitter, since
  filtering is computed per (listener, target) pair - a source could have multiple listeners in
  theory (though this plugin currently assumes one main listener).
- Whatever is chosen, needs to interact correctly with `bLooping` sounds and with `SetDryOutputEnabled()`
  (used for `bReverbOnly` mode in `AVAudioWorld::Tick`).

# TODO: Fall back to collision shapes when a UStaticMeshComponent has no mesh assigned

Source: `Source/vaudio-unreal/Private/VAudioWorld.cpp`, `AVAudioWorld::ScanAndAddPrimitives()`
(the `if (!Mesh) continue;` check).

## Problem

`ScanAndAddPrimitives()` currently skips a `UStaticMeshComponent` entirely if `GetStaticMesh()`
returns null, even though the component could still carry a capsule/sphere/box collision shape
that the rest of the function knows how to convert into a VA primitive (the `Agg.SphylElems` /
`SphereElems` / `BoxElems` loops immediately below already do this, just keyed off `Mesh->GetBodySetup()`).

## Options considered (not yet decided)

1. Read collision shapes from a `UShapeComponent` (`UCapsuleComponent`/`USphereComponent`/`UBoxComponent`)
   sibling on the same actor, separately from the `UStaticMeshComponent` loop - these carry their
   own `BodyInstance`/shape data without needing a static mesh at all.
2. Decide whether a mesh-less `UStaticMeshComponent` can even carry simple-collision `AggGeom`
   data independent of its (absent) `UStaticMesh` - if not, option 1 is the only real path.

## Notes for implementation

- This changes what actors are picked up by the scan (currently mesh-less components are silently
  skipped), so needs a decision on whether that silent skip is relied upon anywhere before
  widening the scan. ANSWER: actors with a VAMaterialComponent is what controls whether they are added to the raytraced world. If it has a collision mesh and a VAMaterialComponent, it should be added to the world.

