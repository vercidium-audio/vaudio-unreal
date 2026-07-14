# Overview

The overall goal is:
- Less null propagation: lots of places have "if (!something) return;" with no reasoning why it could be null. Rather than letting null checks permeate the codebase, fix them early. If it genuinely can be null, leave a comment explaining why
- Logging cleanup: we have a mix of UE_LOG and VaRawLog calls. VaRawLog logs to UE_LOG anyway, so we should use VaRawLog. Also, logs should follow the pattern of VaRawLog(L"fileName: functionName(): worldOrEmitterName: message", *GetName());. Define a helper method in each file if possible, e.g. a VaWorldLog(const wchar_t* message) that automatically adds the file name, GetName() and function name using reflection? is that a thing? 


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

## Options considered (not yet decided)

1. **Start muted, fade in** - spawn with volume 0, ramp to full volume (and the real filter
   values) once raytracing has produced a first result for this target. Avoids a jarring pop but
   adds a brief silence at the very start of playback.
2. **Delay spawn entirely** - don't call `SpawnSoundAtLocation` until the first raytrace
   completes. Simplest to implement, but delays sound start by however many frames raytracing
   takes (could be perceptible, especially for short one-shot sounds).
3. **Something else** - e.g. snap the filter to a conservative/muffled default on spawn instead
   of fully open, so the "before" state is closer to typical steady-state instead of guessing.

## Notes for implementation

- Readiness signal: `vaEmitterHasRaytracedTarget(ListenerEmitter, ThisEmitter)` from the listener's
  side (see the loop in `Tick()` that calls `ApplySourceFilter`), or track a local "have we ever
  received a non-null filter" flag inside `AVAudioEmitter` itself.
- Need to decide whether this only applies to the *listener's* view of *this* emitter, since
  filtering is computed per (listener, target) pair - a source could have multiple listeners in
  theory (though this plugin currently assumes one main listener).
- Whatever is chosen, needs to interact correctly with `bLooping` sounds and with `SetDryOutputEnabled()`
  (used for `bReverbOnly` mode in `AVAudioWorld::Tick`).

# TODO: Single main-listener tracking on AVAudioWorld

Source: `Source/vaudio-unreal/Private/VAudioWorld.cpp`, `AVAudioWorld::Tick()` and
`AVAudioWorld::GetMainListener()`.

## Problem

Every frame, `Tick()` linearly scans `RegisteredEmitters` to find the one with
`bIsMainListener == true` (for the on-screen debug display), and `GetMainListener()` does the
same scan on demand. There can only be one true main listener, but nothing enforces this — if a
second emitter has `bIsMainListener = true`, both scans silently return/use whichever comes first
in registration order, with no warning that the setup is ambiguous.

## Proposed fix

- Cache a `TWeakObjectPtr<AVAudioEmitter> MainListener` on `AVAudioWorld`, set in
  `RegisterEmitter()`/`UnregisterEmitter()` (or when `bIsMainListener` toggles - see
  `AVAudioEmitter::PostEditChangeProperty` for the editor-time hook, plus a runtime setter if
  `bIsMainListener` can change post-BeginPlay).
- When `RegisterEmitter()` is called (or `bIsMainListener` is set true) while `MainListener` is
  already valid and different, log a warning (`UE_LOG(LogTemp, Warning, ...)`) - first-registered
  wins, or last-registered wins; needs a decision either way.
- Replace the scan in `Tick()` and the body of `GetMainListener()` with a direct read of the
  cached pointer.

## Notes for implementation

- Need to decide the conflict-resolution rule (reject the second one vs. replace) and whether
  unsetting `bIsMainListener` at runtime should clear the cached pointer.
- `RegisterEmitter`/`UnregisterEmitter` currently take a bare `AVAudioEmitter*`; this would need
  to read `Emitter->bIsMainListener` at registration time, so ordering vs. property
  initialization on the emitter matters (registration happens in `AVAudioEmitter::BeginPlay`,
  after properties are already set from the level/defaults, so this should be safe).

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
  widening the scan.

