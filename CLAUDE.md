# Overview

This is a public repo for the Vercidium Audio Unreal Engine plugin. It wraps the Vercidium Audio 3D C SDK (`vaudionative`), and plays sound through Unreal's own audio mixer rather than OpenAL: muffling is a low-pass filter and volume on each audio component, and reverb uses `USubmixEffectReverbPreset`.

Supported on Windows (Win64), Linux (x86_64) and Mac (x86_64 for 4.27, universal for 5.7 onwards), with Unreal Engine 4.27, 5.7 and 5.8. Use `UE_VERSION_OLDER_THAN` (e.g. in `VAEngineCompat.h`) where engine APIs differ, so one source tree builds on all of them, and see "Unreal Engine 4.27" below.

The SDK is vendored in `Source/ThirdParty/vaudio/{include,lib/Win64,lib/Linux,lib/Mac}` and is git-ignored, never commit it. `VAudioUnreal.Build.cs` links and stages it (plus the debug window and GLFW for the dev SDK). A Blueprint-only project never builds, so nothing is staged and each platform falls back to loading it from `Source/ThirdParty/vaudio/lib/<platform>`. `FVaudioUnrealModule::StartupModule` checks the SDK's version and precision, and `FVaudioUnrealModule::IsSdkLoaded()` gates every entry point that doesn't go through a world.

The SDK's precision matches `FVector`'s: 5.x uses the double precision SDK and 4.27 the single precision one, picked in `VAEngineCompat.h` and guarded by a `static_assert` in `VAConstants.h` and the `StartupModule` check. Geometry is `VAFloat` in `vaudio.h`, so Unreal positions pass straight through (`FVectorToVAVector`, `VAVectorToFVector`), never cast them with `(float)`. Acoustic values (gains, EAX, materials) are `float`.

The Godot plugins are the reference implementation. `vaudio-godot-native-openal-3d` is the closest match (C++ wrapping the same C SDK), and this plugin's classes, properties and behaviour are ported from it. Keep the two in sync unless the difference is listed under "Intentional differences" below.

# Modules and layout

- `Source/vaudio-unreal` is the `VaudioUnreal` runtime module. `Public/` has one header per class, `Private/` has the implementations plus internal helpers (`VALog.h`, `VASubmixSend.h`, `VAReverbConversion.h`, `VAMaterialConversion.h`, `VAConstants.h`, `VAEditorSprite.h`)
- `Source/VaudioUnrealEditor` is the editor module: material asset factories, "Convert to VASource" for `AAmbientSound`, details customisations, `VAEditorStyle`, `FVAWorldBoundsVisualizer` and `UVABakeCommandlet` (`-run=VABake`)
- `Content/Icons` holds the billboard sprite textures, generated from `Resources/Icons/*.png`. Change the PNGs and re-import them rather than editing the textures in the editor
- `AVAWorld` is split across `VAWorld.cpp` (lifecycle, bounds, emitters, listeners, reverb, materials), `VAWorldPrimitives.cpp` (building geometry) and `VAWorldPrimitiveTracking.cpp` (transforms and removal)

# Classes

Reflected names are exactly the Godot node names. There is no backward compatibility with the pre-1.11 `AVAudio*` names, and no CoreRedirects should be added.

| Class | Godot equivalent | What it is |
|---|---|---|
| `AVAWorld` | `VAWorld` | Owns the SDK world. One per map |
| `AVAEmitter` | `VAEmitter` | A raytraced point with no sound. Base of the listener and the raytraced sources |
| `AVAListener : AVAEmitter` | `VAListener` | The point everything is muffled relative to |
| `AVARaytracedSource : AVAEmitter` (abstract) | `VARaytracedSource` | Base for sources that are emitters themselves. Owns the playback |
| `AVASource : AVARaytracedSource` | `VASource` | Plays a `USoundBase` |
| `AVAStreamSource : AVARaytracedSource` | `VAStreamSource` | Plays PCM pushed at runtime |
| `AVAInputStreamSource : AVAStreamSource` | `VAInputStreamSource` | A stream fed by a capture device |
| `AVANetworkedStreamSource : AVAStreamSource` | `VANetworkedStreamSource` | An empty subclass, so netcode can find it |
| `AVASourceRelative` | `VASourceRelative` | A 2D sound with the listener's reverb. Not raytraced |
| `AVASourceLeech` | `VASourceLeech` | A spatialised sound that reuses another emitter's muffling and reverb |
| `AVASourceAmbient` | `VASourceAmbient` | A 2D sound filtered by the listener's ambient filter |
| `UVAVisualisation` | `VAVisualisation` | Scene component on an emitter that draws where its rays land |
| `UVAMaterialComponent` | `vercidium_audio_material` metadata | Makes an actor's geometry part of the raytraced scene |
| `UVADefaultMaterial`, `UVACustomMaterial` | `VADefaultMaterial`, `VACustomMaterial` | DataAssets listed in `AVAWorld::Materials` |
| `UVAWorldSubsystem` | `find_va_world()` | Tracks the map's `AVAWorld` |
| `FVASourcePlayback` | `ALSource` | Owns a source's audio components, filter and reverb send |

Property names are the Godot names in PascalCase (`b` prefix for bools), in the categories `Reverb`, `Muffling`, `Ambience`, `Advanced` and `Debug Rendering` under `Vercidium Audio|`.

# Architecture

## Finding the world

Nothing holds a reference to the `AVAWorld`. It registers with the map's `UVAWorldSubsystem` on BeginPlay, and everything else calls `AVAWorld::Find`. An emitter or material component that begins play first binds `OnWorldRegistered` and joins when the world arrives. A second `AVAWorld` in the same map logs an error and is ignored.

`OnWorldUnregistered` is broadcast last, after `vaWorldDestroy`, and emitters and material components wait for the next `AVAWorld`. Seamless travel moves kept actors with no EndPlay or BeginPlay, so both rebind through `BoundSubsystem` (`AVAEmitter::PostRename`, `UVAMaterialComponent::OnRegister`).

## Emitter lifecycle

1. `TryInitializeEmitter` resolves the world, runs `ValidateConfig`, then `AttachToWorld`: `CreateEmitter`, `InitializeTypeSpecific` (pushes properties) and `AVAWorld::RegisterEmitter`. Properties are pushed before the handle joins the world
2. Every non-listener emitter is automatically a target of the current listener. There is no target list to configure
3. `LeaveWorld` runs `DeinitializeTypeSpecific`, detaches and calls `ReleaseEmitter`, which calls `vaWorldRemoveEmitter`
4. The SDK calls `OnRemoved` once no raytracing thread reads the emitter (possibly after the reverb tail). The handle goes to `AVAWorld::DeferEmitterDestroy`, or `OrphanedEmitters` if the actor is gone, and is destroyed after the next `vaWorldUpdate`. Never call `vaEmitterDestroy` before `OnRemoved` has fired, it returns `VA_ERROR_IN_USE`
5. `AVAWorld::EndPlay` runs `vaWorldWait`, destroys primitives, `vaWorldDestroy`, then every handle it still owns

`bAutoDestroy` on `AVASource` uses `BeginFinalRemoval`, which keeps the handle so `OnRemoved` reaches the actor, which then destroys itself. If it ends play during that wait, `ReleaseEmitter` must not call `vaWorldRemoveEmitter` again (it returns `VA_NOT_FOUND`).

`bRaytraceOnce` and `SetSuspended(true)` are applied in `AVAEmitter::Tick` on the first tick where `IsReadyToPlay()` is true, never earlier, or the emitter has no reverb to keep. `SetSuspended(false)` also clears `bRaytraceOnce`.

SDK callbacks run inside `vaWorldUpdate`. They only set flags or queue, and `AVAWorld::Tick` broadcasts the Blueprint delegates after `vaWorldUpdate` returns, so a handler can't re-enter the SDK.

With `bEmittersOutsideTheWorldAreMuffled` on, `AVAWorld::IsSilencedByBounds` is the one check for silencing sounds outside the world bounds (`VACanStartOneShot`, `UpdateOneShots`, `VAApplyBoundsRules`). `AVAWorld::IsOutsideBounds` is the same test without the setting. Don't use `vaEmitterGetWithinWorldBounds` for this, it's false until the emitter is first processed.

## Listeners

Every listener in a world shares one SDK emitter, held by the current listener (`bCurrent`, `MakeCurrent()`). `AVAWorld::SetCurrentListener` hands the handle over, so targets stay connected. When the current listener leaves, the first remaining one is promoted. A `UVAVisualisation` on a listener follows the handle (the Godot plugin doesn't do this yet).

## Geometry

`UVAMaterialComponent` is the registration point: `AVAWorld::AddMaterialPrimitives` adds its actor and every attached child actor without its own material component, which covers spawned actors, streamed levels, World Partition cells and level instances. A level instance's actors aren't attached to its `ALevelInstance`, so they're reached through `ULevelInstanceSubsystem` (`AddLevelInstanceContents`, `OnLevelAddedToWorld`). Only add the contents of a visible level, as `AddBinding` doesn't dedupe.

- Shape components and simple collision become SDK prisms, spheres and capsules. Convex hulls and render triangles become mesh primitives, sharing triangle data through `AVAWorld::SharedMeshes`
- A zero-thickness simple collision box (e.g. `/Engine/BasicShapes/Plane`) becomes an SDK plane, as a thin prism leaks bouncing rays. Planes always use flat transmission
- The SDK's mesh triangle test is one-sided. Render triangles already have the right winding, but convex hull winding varies, so each hull triangle is faced away from the hull's centre
- Unreal has no global attach event, so late-attached children, runtime ISM instances and swapped meshes are only picked up by `AVAWorld::SyncPrimitive`
- Landscapes add one SDK heightmap per `ULandscapeHeightfieldCollisionComponent`, read from its collision heightfield, with `AVAWorld::LandscapeLOD`. Heightmaps always use flat transmission
- Skeletal meshes, spline meshes and BSP brushes aren't supported

## Audio seams

An FMOD backend is planned, so raytracing, lifecycle and geometry code must not touch `UAudioComponent` or submixes directly. Audio-mixer calls stay behind:
- `FVASourcePlayback` (audio components, filter, multipliers, dry toggle, reverb send)
- `VAFilterConversion.h` (muffling result to volume and cutoff)
- `AVAWorld::OnReverbUpdated` and `AVAListener::ApplyListenerReverb` (reverb and pan, from the SDK's reverb-updated callback)

Reverb sends always go through `VASetReverbSend` (pre distance attenuation), never `UAudioComponent::SetSubmixSend` (post attenuation). Submix effects always go through `VAAddSubmixEffect`, never `UAudioMixerBlueprintLibrary::AddSubmixEffect`, since in a cooked game the submix may not be registered yet at BeginPlay.

# Intentional differences from the Godot plugins

Don't "fix" these:
- Unreal's audio mixer instead of OpenAL Soft, so no `ALManager`, distance model, master volume or EFX. Distance falloff comes from `USoundAttenuation`, and Sound Cues cover Godot's `streams`, `pitch_randomness`, `volume_randomness_db` and `playback_no_repeat`
- Grouped EAX reverbs map to `GroupedEAXSubmixes`. `FSubmixEffectReverbSettings` has no echo, modulation, LF decay ratio or reference frequency, so those are dropped, and reverb can only be panned left/right
- Reverb sends are pre distance attenuation, clamped to [0, 1] by the engine
- Sounds must use the "Play When Silent" virtualization mode, or a fully muffled sound stops
- `BakeGeometry` and the bake commandlet exist because cooked builds drop CPU mesh data. `FVABakedMesh` is keyed by soft mesh path + `MeshLOD`, and `AddStaticMeshPrimitives` falls back to `FindBakedMesh` when there's no live render data. Don't set `bAllowCPUAccess` on meshes at runtime instead, it doesn't survive cooking and dirties users' assets
- Units are cm (`MetersPerUnit = 0.01`) with `VACoordinateSystemUnreal`
- Materials are DataAssets plus a component rather than scene nodes and metadata
- `bAutoCapture` on `AVAInputStreamSource`, `bAutoFollowCamera` and `ListenerReverbSubmix` on `AVAListener`, `bAirAbsorptionEnabled`, `BoundsFollow` and `BoundsUpdateDistance` on `AVAWorld` are Unreal-only
- PIE is in-process, so there is no debugger relay: `PostEditChangeProperty` on the PIE actor reaches the running world

# Building and testing

The automation tests live in a separate host project (`vaudio-unreal-devproject`, module `VaudioUnrealTests`), as `VAudio.<scenario>` tests matching the Godot plugins' scenario names. They assert on raytracing results, not audio output, so everything must keep working with no audio device (`-nosound`). When that project is available you may build and run the tests yourself, with the Unreal editor closed. Otherwise the user builds and reports errors.

# Coding guidelines

- Log with the macros in `Private/VALog.h` (`VA_LOG`, `VA_WARN`, `VA_ERROR`, plus `_NAMED` and `_RESULT` variants). On-screen messages don't exist in Shipping, so never use `GEngine->AddOnScreenDebugMessage` as the only channel
- Messages are read by someone working in the editor, so say what's wrong and what to change rather than naming internal functions
- Use camelCase for variable names, not capitalised names (`listener`, not `Listener`) or single acronyms (`position`, not `P`)

## Unreal Engine 4.27

The repo source is plain 5.x, and only builds on 4.27 once its marker blocks are resolved (4.27 users get the resolved source from the release zip). 4.27's UHT rejects any `#if` in a parsed header other than `WITH_EDITOR`, `WITH_EDITORONLY_DATA`, `0` or `1`, so anything that differs between engines inside a `UCLASS`/`USTRUCT` body is a marker block, never `UE_VERSION_OLDER_THAN`:

```cpp
#if 0 // VA_UE4
	// 4.27 branch
#else // VA_UE4
	// 5.x branch
#endif // VA_UE4
```

- No `TObjectPtr`, use raw pointers
- No container `IsEmpty()`, use `Num() == 0`
- `VAActorName` instead of `GetActorNameOrLabel`, and the other helpers in `VAEngineCompat.h`
- `FVAVector3f`/`FVAVector2f` instead of `FVector3f`/`FVector2f` outside reflected members
- Mixing float and double literals in one call still needs care, e.g. `FMath::Clamp` with a float value and `1.0` bounds is ambiguous
- 4.27's UHT rejects `double` in Blueprint-exposed members, so those use a marker block, and `VABlueprintReal` is the matching type in plain C++
- World Partition and Level Instances are compiled out
- Launcher 4.27 is PhysX, so convex hulls and `FVALandscapeHeightfield` have a `UE_VERSION_OLDER_THAN(5, 0, 0) && !WITH_CHAOS` path
- Slate has no SVG brushes, so `VAEditorStyle` uses PNGs rendered from `VALogo.svg`
- Assets saved by 5.x don't load, so the committed `Content/Icons` are 5.x and the release zip's are generated by 4.27

## Actor Initialisation

If an actor is configured incorrectly, disable it with `SetActorTickEnabled(false)`, rather than letting `if (!AudioWorld)` or `if (!Emitter)` checks pollute the rest of the code, e.g. in `VASourceRelative.cpp`:

```cpp
if (SourceSounds.Num() == 0)
{
	VA_WARN_NAMED(TEXT("Has no SourceSounds and will not play sound"));
	SetActorTickEnabled(false);
	return;
}
```

Actor BeginPlay order isn't guaranteed, so never require another actor (e.g. the listener or the VAWorld) to have begun play first. Wait for it in Tick or on the subsystem's delegates instead, or let `AVAWorld` wire things up when it registers.

## VAResult Handling

When a va* function returns a VAResult, handle every documented return code, and never `check(result == VA_SUCCESS)`, e.g. in `VAListener.cpp`:

```cpp
VAResult result = vaEmitterAddTarget(Emitter, target);

switch (result)
{
	case VA_SUCCESS:
	case VA_ALREADY_EXISTS:
		break;

	case VA_NOT_ADDED_TO_WORLD:
		VA_ERROR_NAMED(TEXT("Failed to add target '%s' as it has not been added to the same world as this listener."), *name);
		break;

	default:
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to add target '%s'."), *name);
		break;
}
```
