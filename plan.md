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
