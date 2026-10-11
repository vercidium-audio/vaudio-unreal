# Overview

This is a public repo for the Vercidium Audio Unreal Engine plugin. It wraps the Vercidium Audio 3D C SDK (`vaudionative`) and plays sound through Unreal's own audio mixer: muffling is a low-pass filter and volume on each audio component, and reverb is `USubmixEffectReverbPreset`s on submixes. It doesn't use OpenAL.

Windows (Win64), Linux (x86_64) and Mac (arm64 only) are supported, tested with Unreal Engine 5.7.

The SDK is vendored in `Source/ThirdParty/vaudio/{include,lib/Win64,lib/Linux,lib/Mac}` and is git-ignored, never commit it. `Source/ThirdParty/vaudio/include/vaudio.h` is its public header. `VAudioUnreal.Build.cs` links the library for the target platform and stages it into `Binaries/ThirdParty/vaudio/<platform>`, along with the debug window and GLFW when the dev SDK is used. `FVaudioUnrealModule::StartupModule` loads it and checks its version, and `FVaudioUnrealModule::IsSdkLoaded()` gates every entry point that doesn't go through a world.

The Godot plugins are the reference implementation. `vaudio-godot-native-openal-3d` is the closest match (C++ wrapping the same C SDK), and this plugin's classes, properties and behaviour are named after and ported from it. When changing behaviour here, check how the Godot plugin does it, and keep the two in sync unless the difference is listed under "Intentional differences" below.

# Modules and layout

- `Source/vaudio-unreal` is the `VaudioUnreal` runtime module. `Public/` has one header per class, `Private/` has the implementations plus internal helpers (`VALog.h`, `VASubmixSend.h`, `VAReverbConversion.h`, `VAMaterialConversion.h`, `VAConstants.h`)
- `Source/VaudioUnrealEditor` is the `VaudioUnrealEditor` editor module: the material asset factories, the `VAListener` details customisation that hides properties a listener doesn't use, and `UVABakeCommandlet` (`-run=VABake`)
- `AVAWorld` is split across `VAWorld.cpp` (lifecycle, bounds, emitters, listeners, reverb, materials), `VAWorldPrimitives.cpp` (building geometry) and `VAWorldPrimitiveTracking.cpp` (transforms and removal)

# Classes

Reflected names (what the editor and Blueprint show) are exactly the Godot node names. There is no backward compatibility with the pre-1.11 `AVAudio*` names: no CoreRedirects, and none should be added.

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

Nothing holds a reference to the `AVAWorld`. `AVAWorld::BeginPlay` creates the SDK world and registers with the map's `UVAWorldSubsystem`, and everything else calls `AVAWorld::Find`. BeginPlay order isn't guaranteed, so an emitter or material component that begins play first binds `OnWorldRegistered` and joins when the world arrives. A second `AVAWorld` in the same map logs an error and is ignored.

When the `AVAWorld` ends play it broadcasts `OnWorldUnregistered` last, after `vaWorldDestroy`. Emitters and material components let go and wait again, so they join the next `AVAWorld`. Both keep the subsystem they bound to in `BoundSubsystem` and rebind when it changes: seamless travel moves a kept actor into the new map with no EndPlay or BeginPlay, which `AVAEmitter::PostRename` and `UVAMaterialComponent::OnRegister` detect.

## Emitter lifecycle

1. `TryInitializeEmitter` resolves the world, runs `ValidateConfig`, then `AttachToWorld`: `CreateEmitter` (handle + callbacks), `InitializeTypeSpecific` (pushes properties) and `AVAWorld::RegisterEmitter`. Properties are pushed before the handle joins the world
2. Every non-listener emitter is automatically a target of the current listener, whichever of the two registers first. There is no target list to configure
3. `LeaveWorld` (from EndPlay or `OnWorldUnregistered`) runs `DeinitializeTypeSpecific`, detaches and calls `ReleaseEmitter`, which calls `vaWorldRemoveEmitter`
4. The SDK calls `OnRemoved` once its raytracing threads no longer read the emitter, which can be straight away, after the in-flight pass, or after the reverb tail (`VA_PENDING_REMOVAL`). The handle goes to `AVAWorld::DeferEmitterDestroy`, or to the static `OrphanedEmitters` map if the actor is already gone, and is destroyed after the next `vaWorldUpdate` returns. Never call `vaEmitterDestroy` before `OnRemoved` has fired, it returns `VA_ERROR_IN_USE`
5. `AVAWorld::EndPlay` runs `vaWorldWait`, destroys primitives, `vaWorldDestroy`, then destroys every handle it still owns

SDK callbacks run inside `vaWorldUpdate`. They only set flags or queue (`QueueRaytracingComplete`, `QueueRaytracedByListener`), and `AVAWorld::Tick` broadcasts the Blueprint delegates after `vaWorldUpdate` returns, so a handler that spawns or destroys emitters can't re-enter the SDK.

A source only plays once `AVAEmitter::IsReadyToPlay()` is true: the listener has raytraced it and, if it casts reverb rays and affects grouped EAX, its own reverb result exists.

## Listeners

Every listener in a world shares one SDK emitter, held by the current listener (`bCurrent`, `MakeCurrent()`). `AVAWorld::SetCurrentListener` hands the handle over, so targets stay connected. A listener that isn't current has no handle and doesn't tick. When the current listener leaves, the first remaining one is promoted.

## Geometry

`UVAMaterialComponent` is the registration point. On BeginPlay it calls `AVAWorld::AddMaterialPrimitives`, which adds its actor and every attached child actor that has no material component of its own, and on EndPlay it removes them. That covers spawned actors, streamed levels, World Partition cells and level instances with no extra code.

- Shape components and static mesh simple collision become SDK prisms, spheres and capsules. Convex hulls and render triangles become mesh primitives
- `PropagateMode`, `MeshLOD`, `MaterialOverrides` and the world's `CollisionObjectTypes` decide what is added
- Mesh primitives share triangle data through `AVAWorld::SharedMeshes`, keyed by static mesh, LOD and convex hull index
- The SDK's mesh triangle test is one-sided. Unreal render triangles already have the winding it expects, but Chaos convex hull `IndexData` is wound the other way and is reversed
- Unreal has no global attach event, so a child attached after its parent began play, instances added to an ISM at runtime and a swapped mesh are only picked up by `AVAWorld::SyncPrimitive`
- Landscapes, skeletal meshes, spline meshes and BSP brushes aren't supported

## Audio seams

An FMOD backend is planned, so audio-mixer calls stay behind three seams, and raytracing, lifecycle and geometry code must not touch `UAudioComponent` or submixes directly:
- `FVASourcePlayback` owns each source's audio components, filter, multipliers, dry toggle and reverb send
- `VAFilterConversion.h` converts a muffling result to a volume and a cutoff frequency
- `AVAWorld::OnReverbUpdated` and `AVAListener::ApplyListenerReverb` push reverb and pan, from the SDK's reverb-updated callback rather than every tick

Reverb sends always go through `VASetReverbSend` (`Private/VASubmixSend.h`), which sends pre distance attenuation. Never call `UAudioComponent::SetSubmixSend` directly, it defaults to post attenuation. Muffling uses the component low-pass filter, which only affects the dry path, and the send is compensated by gainLF so the reverb isn't muffled.

# Intentional differences from the Godot plugins

Don't "fix" these:
- Unreal's audio mixer instead of OpenAL Soft, so there is no `ALManager`, distance model, master volume or EFX. Distance falloff comes from `USoundAttenuation`, and Sound Cues cover Godot's `streams`, `pitch_randomness`, `volume_randomness_db` and `playback_no_repeat`
- Grouped EAX reverbs map to `GroupedEAXSubmixes`. `FSubmixEffectReverbSettings` has no echo, modulation, LF decay ratio or reference frequency fields, so those are dropped, and reverb can only be panned left/right
- Reverb sends are pre distance attenuation, and clamped to [0, 1] by the engine
- Sounds must use the "Play When Silent" virtualization mode, or a fully muffled sound stops
- `BakeGeometry` and the bake commandlet exist because cooked builds can drop CPU mesh data
- Units are cm (`MetersPerUnit = 0.01`) with `VACoordinateSystemUnreal`
- Materials are DataAssets plus a component rather than scene nodes and metadata
- `bAutoCapture` on `AVAInputStreamSource`, `bAutoFollowCamera` and `ListenerReverbSubmix` on `AVAListener`, `bAirAbsorptionEnabled`, `BoundsFollow` and `BoundsUpdateDistance` on `AVAWorld` are Unreal-only
- PIE is in-process, so there is no debugger relay: `PostEditChangeProperty` on the PIE actor reaches the running world

# Building and testing

The automation tests aren't in this repo. They live in a separate host project (`vaudio-unreal-devproject`, module `VaudioUnrealTests`) with a headless runner, as tests named `VAudio.<scenario>` that match the Godot plugins' scenario names. They assert on raytracing results, not audio output, so everything must keep working with no audio device (`-nosound`).

When that project is available on this machine you may build and run the tests yourself, with the Unreal editor closed. Otherwise the user builds and reports errors. The editor target hides missing includes through its shared PCH, so a change that adds engine types should include their headers explicitly, or packaged game targets fail to compile.

# Coding guidelines

- Log with the macros in `Private/VALog.h` (`VA_LOG`, `VA_WARN`, `VA_ERROR`, plus `_NAMED` and `_RESULT` variants). Warnings and errors also appear on screen, but on-screen messages don't exist in Shipping, so never use `GEngine->AddOnScreenDebugMessage` as the only channel
- Messages are read by someone working in the editor, not in this code, so say what's wrong and what to change rather than naming internal functions
- Use camelCase for variable names
- Don't use single capitalised acronyms for variable names, e.g. use `position` instead of `P`, `vaWorld` instead of `VAW`, etc
- Don't capitalise variable names, e.g. use `listener` instead of `Listener`
- Use `TCHAR` with `TEXT()`, never `wchar_t` (`TCHAR` is `char16_t` on Linux)
- `UE_LOG` and the `VA_*` macros need braces in an unbraced `if`/`else`
- UHT rejects a function parameter named like a member, case-insensitively (`materials` vs `Materials`)
- An effect preset's `Settings` UPROPERTY is stale after `UpdateSettings`, read live values with `GetSettings()`

## Actor Initialisation

If an actor is configured incorrectly, disable it with `SetActorTickEnabled(false)`, rather than letting `if (!AudioWorld)` or `if (!Emitter)` checks pollute the rest of the code, e.g. in `VASourceRelative.cpp`:

```cpp
void AVASourceRelative::BeginPlay()
{
	Super::BeginPlay();

	// Disable the actor if validation fails
	if (SourceSounds.Num() == 0)
	{
		VA_WARN_NAMED(TEXT("Has no SourceSounds and will not play sound"));
		SetActorTickEnabled(false);
		return;
	}
}
```

Actor BeginPlay order isn't guaranteed, so never require another actor (e.g. the listener or the VAWorld) to have begun play first. Wait for it in Tick or on the subsystem's delegates instead, or let `AVAWorld` wire things up when it registers.

## VAResult Handling

When a va* function returns a VAResult, handle every documented return code, and never `check(result == VA_SUCCESS)`, e.g. in `VAListener.cpp`:

```cpp
VAResult result = vaEmitterAddTarget(Emitter, target->GetVAEmitter());

switch (result)
{
	case VA_SUCCESS:
	case VA_ALREADY_EXISTS:
		break;

	case VA_NOT_ADDED_TO_WORLD:
		VA_ERROR_NAMED(TEXT("Failed to add target '%s' as it has not been added to the same world as this listener."), *target->GetActorNameOrLabel());
		break;

	case VA_FEATURE_DISABLED:
		VA_ERROR_NAMED(TEXT("Failed to add target '%s' as this listener casts neither occlusion nor permeation rays."), *target->GetActorNameOrLabel());
		break;

	default:
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to add target '%s'."), *target->GetActorNameOrLabel());
		break;
}
```
