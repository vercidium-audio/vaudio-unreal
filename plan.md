# Overview

The overall goal is:
- Less null propagation: lots of places have "if (!something) return;" with no reasoning why it could be null. Rather than letting null checks permeate the codebase, fix them early. If it genuinely can be null, leave a comment explaining why
- Logging cleanup: always use VALog() instead of directly calling VaRawLog or UE_LOG. VALog() automatically appends the name of the current object (emitter, world, etc), file name and function name


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

