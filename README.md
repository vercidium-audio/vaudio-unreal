# vaudio-unreal

Unreal Engine plugin for Vercidium Audio — raytraced audio simulation with realistic muffling, reverb, ambience and occlusion.

> [!WARNING]
> This plugin is experimental. Please raise issues on the [Support repository](https://github.com/vercidium-audio/support)

This repository requires Vercidium Audio v1.12.0 to run.
- Download the Vercidium Audio SDK from [vercidium.com](https://vercidium.com)

> Please note that the Vercidium Audio SDK is not free for commercial use. See [vercidium.com/eula](https://vercidium.com/eula)

## Features

- Muffle sounds in real time
- Accurate reverb in any environment
- Innovative event-based raytracing system
- Realistic energy-based model using materials
- Dynamic scene updates - automatically handles moving objects

## Requirements

- Unreal Engine 4.27, 5.7 or 5.8
- A C++ project (this plugin is a C++ module, so your project needs a `Source` folder. If you have a Blueprint-only project, Unreal will offer to create one the first time you add a C++ plugin)
- Visual Studio (or another C++ toolchain) installed and set up for Unreal development
- [Vercidium Audio SDK](https://vercidium.com)

## Installation

### 1. Add the plugin

Download the zip for your engine version from the [Releases page](https://github.com/vercidium-audio/vaudio-unreal/releases) (`vaudio-unreal-4.27.zip`, `vaudio-unreal-5.7.zip` or `vaudio-unreal-5.8.zip`) and extract it into your project's `Plugins` folder. It contains the plugin source plus prebuilt binaries for Win64, Linux and Mac. On 5.x you can also clone this repository instead, which builds the plugin from source. On 4.27 you must use the zip (see [Unreal Engine 4.27](#unreal-engine-427)).

The `.uplugin` file must exist at:
```
YourProject/Plugins/vaudio-unreal/vaudio-unreal.uplugin
```

If you're using git, add it as a submodule from your project root:

```
git submodule add <this-repo-url> Plugins/vaudio-unreal
```

### 2. Add the Vercidium Audio SDK

This plugin links against the native Vercidium Audio SDK, which is not included in this repository. The SDK files must live in the plugin's `Source/ThirdParty/vaudio` folder:

```
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/include/vaudio.h
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/Win64/vaudionative.lib
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/Win64/vaudionative.dll
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/Linux/libvaudionative.so
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/Mac/libvaudionative.dylib
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/LinuxArm64/libvaudionative.so
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/Win64-arm64/vaudionative.lib
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/Win64-arm64/vaudionative.dll
```

The SDK's platform folders map to these `lib` folders:

| SDK folder | Plugin folder |
|---|---|
| `windows` | `lib/Win64` |
| `linux` | `lib/Linux` |
| `mac` | `lib/Mac` |
| `linux-arm64` | `lib/LinuxArm64` (5.x only, see [ARM64](#arm64)) |
| `windows-arm64` | `lib/Win64-arm64` (5.8 only, see [ARM64](#arm64)) |

[Download the SDK](https://vercidium.com) and:
- if using Unreal 5.7 onwards, copy the files of the double-precision native SDK (`3d/native-double`) into the above locations.
- if using Unreal 4.27, copy the files of the single-precision native SDK (`3d/native`) into the above locations.

You only need the `lib` folders for the platforms you'll build for.

To use the debug window, use the dev SDK and also copy the files from the SDK's `3d/debug-window/{SDK folder}` folder:
- For Windows: `vaudio-debug-window.exe` and `glfw3.dll` into `lib/Win64`
- For Linux: `vaudio-debug-window` and `libglfw.so.3` into `lib/Linux` (and `lib/LinuxArm64`)
- For Mac: `vaudio-debug-window` and `libglfw.3.dylib` into `lib/Mac`
- For Windows ARM64: `vaudio-debug-window.exe` and `glfw3.dll` into `lib/Win64-arm64`

The Mac SDK is universal (arm64 and x86_64), so Mac builds can target Apple Silicon, Intel or Universal. The debug window is universal too, so it opens on Apple Silicon and Intel.

> [!WARNING]
> Mac intel builds have only been tested under Rosetta, not on Intel hardware.

When you build, the plugin copies the above dll/so/dylib files into `Binaries/ThirdParty/vaudio/<platform>` and loads them from there. A Blueprint-only project never builds, so the prebuilt plugin from the release zip loads the SDK straight from `Source/ThirdParty/vaudio/lib/<platform>` instead.

### 3. Enable the plugin

- Regenerate your project files (right-click your `.uproject` and click `Generate Visual Studio project files`), then build the project in Visual Studio (or open the `.uproject` and let Unreal prompt you to rebuild missing modules).
- Open the project in the Unreal Editor, go to `Edit > Plugins`, search for `Vercidium Audio`, and enable it.
- Restart the editor if prompted.

## ARM64

Unreal has no ARM64 editor for Windows or Linux, so ARM64 is supported for packaged games only, e.g. for raytracing AI hearing on ARM cloud servers. The plugin is built from source when you package, against the SDK in `lib/LinuxArm64` or `lib/Win64-arm64`.

- **Linux ARM64** (5.7 and 5.8): package from a Linux x64 editor with Unreal's Linux ARM64 target support, e.g. `-platform=LinuxArm64`. The packaged game's launcher is `YourProject-Arm64.sh`.
- **Windows ARM64** (5.8 only, experimental in Unreal): install the engine's Windows ARM64 target platform component (Epic Games Launcher > Library > 5.8 > Options), then package from the x64 editor with `-platform=Win64 -clientarchitecture=arm64`, or `arm64+x64` for a game with both executables. The ARM64 and x64 SDKs are staged into separate `Binaries/ThirdParty/vaudio/Win64-arm64` and `Win64` folders. `arm64ec` uses the x64 SDK and is untested.

## Dedicated servers

The plugin works on Linux dedicated servers (5.x, x64 and ARM64). Servers have no audio device, but raytracing still runs as usual and muffling, reverb, ambience and visualisation data is available.

- With no player controller, the level's current `VAListener` is used as the listener, so place one in the level (or spawn one) for muffling and relative reverb to work
- A dedicated server opens `Server Default Map` (Project Settings > Maps & Modes), not `Game Default Map`, so set it or pass the map on the command line

## Unreal Engine 4.27

4.27 is fully supported, but you must use `vaudio-unreal-4.27.zip` from the [Releases page](https://github.com/vercidium-audio/vaudio-unreal/releases). The source in this repository is written for 5.x, and a few headers contain blocks that are converted to their 4.27 versions when the zip is built, so cloning the repository won't compile on 4.27.

These Unreal features don't exist in 4.27, so the sections of this README about them don't apply:
- World Partition
- Level Instances

Other differences on 4.27:
- Unreal's positions are floats, so geometry and sources far from the origin are less precise than on 5.x.
- Landscapes and convex hulls are read from PhysX collision rather than Chaos. Landscape holes follow that collision, which may be one quad away from where they were painted.

Platform requirements:
- **Windows:** build with Visual Studio 2019.
- **Linux:** Epic doesn't release a 4.27 editor for Linux, so you need to build the engine from source. The zip's prebuilt Linux binaries only load in the engine build that compiled them, so they won't load in yours. Use a C++ project, which rebuilds the plugin for your engine. A Blueprint-only project fails at startup with `Plugin 'vaudio-unreal' failed to load because module 'VaudioUnreal' could not be found`.
- **Mac:** the 4.27 editor is x86_64 only, and runs under Rosetta on Apple Silicon. To build a C++ project with Xcode 27, set the below environment variable:

```
export CCC_OVERRIDE_OPTIONS="# +-Wno-error +-Wno-deprecated-builtins +-fno-access-control +-Wl,-rpath,/usr/lib/swift"
```

## Usage

### Quick start

1. Place one **VAWorld** in the level. Its location is the minimum corner of the raytraced box and `BoundsSize` is the box's size, so move and size it to cover the playable area.
2. For reverb to work, add at least one submix to `VAWorld.GroupedEAXSubmixes` (I recommend at least three)
3. Give the geometry that should affect raytracing (walls, floors, props) a material. Add their render materials or physical materials to the `VAWorld`'s `RenderMaterialMap` or `PhysicalMaterialMap` (e.g. `M_Brick` to Brick, `PM_Dirt` to Dirt), or give the mesh a [VAMeshMaterial](#static-mesh-materials). Geometry without a material is ignored, see [Geometry and materials](#geometry-and-materials).
4. Place one **VAListener**. By default it follows the player's camera (`bAutoFollowCamera`). For muffling to work, enable its occlusion and permeation rays (e.g. `OcclusionRayCount=1024`, `OcclusionBounceCount=8`, `PermeationRayCount=128`, `PermeationBounceCount=2`)
5. Place a **VASource** for each sound, set its `SourceSound` and `bAutoPlay`, and give it reverb rays (e.g. `ReverbRayCount=64`, `ReverbBounceCount=64`) so it has reverb.

Every source is muffled relative to the current listener automatically. Sounds should have a Sound Attenuation asset for distance falloff, and must use the **Play When Silent** virtualization mode, otherwise a fully muffled sound will be stopped by the engine.

### World

- **VAWorld** (`AVAWorld`) owns the raytracing world: bounds, air absorption, threading, materials and reverb submixes. A level can only have one. With the dev SDK, `bRenderingEnabled` shows the scene in a separate debug window.

### Emitters and sources

- **VAListener** (`AVAListener`) is the point everything is heard from. A level can have several, and one is current at a time (`bCurrent`, `MakeCurrent()`). Sounds that use listener reverb are sent to its `ListenerReverbSubmix`. Listeners can share one submix asset, and the current listener controls its reverb. The submix is silent until the listener has its first raytraced reverb (EAX), rather than playing the engine's default reverb, and the grouped EAX submixes likewise. Give the listener reverb rays (`ReverbRayCount`, `ReverbBounceCount`) if it has a submix, or it stays silent.
- **VASource** (`AVASource`) is a raytraced 3D sound. It only plays once it has been raytraced (`IsReadyToPlay()`), so it never starts unmuffled. `Play()` before then returns true and plays it once ready (`Stop()` cancels it), and the same goes for a `VASourceLeech` waiting for its emitter. `Play()` can be called repeatedly and the sounds overlap. Looping comes from the sound asset.
- **VAEmitter** (`AVAEmitter`) is a raytraced point that plays no sound itself, e.g. on an enemy. Use `VASourceLeech` actors to play sounds
- **VASourceLeech** (`AVASourceLeech`) is a 3D sound that reuses the muffling and reverb of the `VAEmitter` it's attached to, instead of casting rays, e.g. many gunshots and footsteps sounds will use the one `VAEmitter`'s raytracing properties
- **VASourceRelative** (`AVASourceRelative`) is a non-3D sound for the player's own sounds like footsteps. It isn't muffled or raytraced, and uses the listener's reverb. It waits for the current listener's first EAX before playing (`IsReadyToPlay()`), so it never plays through a default reverb. `bAutoPlay` plays it once it's ready, and `Play()` before then returns true and plays it once ready (`Stop()` cancels it), like Godot's `VASourceRelative`. It's ready straight away if the listener has no `ListenerReverbSubmix`, casts no reverb rays or is outside the bounds.
- **VASourceAmbient** (`AVASourceAmbient`) is a non-3D sound for rain or wind, filtered by how enclosed the listener is. The listener needs ambient rays (`AmbientOcclusionRayCount` or `AmbientPermeationRayCount`) for this to work
- **VAStreamSource** (`AVAStreamSource`) is a raytraced sound that plays raw PCM data pushed at runtime: `OpenStream`, `PushAudioData`, `CloseStream`.
- **VAInputStreamSource** (`AVAInputStreamSource`) is a stream fed by a microphone, with an `OnAudioCaptured` event for sending it elsewhere. Not supported on Linux.
- **VANetworkedStreamSource** (`AVANetworkedStreamSource`) is a stream for audio received over the network
- **VAVisualisation** (`UVAVisualisation`) is a component that renders dots where rays bounce, with an `OnVisualisationDataReceived` event that has the position and normal of each bounce. Must be attached to a `VAListener`, `VASource` or `VAEmitter`

Useful emitter properties, shared by the listener and the raytraced sources:
- `bAffectsGroupedEAX`: this emitter's reverb is blended into the world's grouped reverbs (on by default for sources). When off, `bUseListenerReverb` chooses between the listener's reverb and none.
- `bRaytraceOnce`: stop raytracing once the first results are in, for short sounds. The emitter keeps its reverb, ambience and muffling for the rest of its lifetime, so its muffling doesn't change as the listener moves.
- `SetSuspended`: stop raytracing on demand, and start again later, e.g. for a sound that's paused or too far away. Its reverb, ambience and muffling don't update while suspended, even if it or the listener moves.
- `bKeepReverbTailAlive`: keep contributing to the reverb after the actor is destroyed, until its reverb tail has faded away.
- `GetMufflingFilterResult`, `GetReverbResult` and `GetAmbientFilterResult` expose the raytraced results to Blueprints.

Reverb is sent before distance attenuation, and the send level is divided by the muffled volume so the reverb hears the unmuffled sound. Unreal caps submix send levels at 1, so a heavily muffled source (its volume below its reverb send level) has quieter reverb than the raytraced result says. `GetReverbSendLevel` on a source returns the level actually sent.

### Conversion

To convert existing audio actors, right-click an **Ambient Sound** and choose **Vercidium Audio → Convert to VASource**. Each one is replaced by a `VASource` with the same sound, transform, label, folder, tags and attachments, and its Auto Activate, Volume Multiplier and Pitch Multiplier become `bAutoPlay`, `VolumeMultiplier` and `PitchMultiplier`.

`bAutoDestroy` is disabled, as an Ambient Sound outlives its sound.

A `VASource` uses the Sound Attenuation on its sound, so attenuation set on the ambient sound's audio component isn't kept, and neither are its low and high pass filters. The Output Log lists anything that wasn't kept. The conversion can be undone, and the new sources still need reverb rays.

### One-shot sounds

`AVAWorld` has static functions that replace the `UGameplayStatics` sound functions, and they're also available in Blueprints under **Vercidium Audio**. As in Unreal, the `Play*` functions are fire-and-forget and return nothing, and the `Spawn*` functions return something that can be controlled:

| Function | Replaces | Returns | Raytracing |
|---|---|---|---|
| `PlaySoundAtLocation` | `UGameplayStatics::PlaySoundAtLocation` | nothing | Its own rays, cast once by default |
| `PlaySoundLeech` | | nothing | Reuses a `VAEmitter`'s rays |
| `PlaySoundRelative` | `UGameplayStatics::PlaySound2D` | nothing | None |
| `SpawnSoundAtLocation` | `UGameplayStatics::SpawnSoundAtLocation` | `AVASource*` | Its own rays, cast once by default |
| `SpawnSoundAttached` | `UGameplayStatics::SpawnSoundAttached` | `AVASource*` | Its own rays, cast continuously |

The `Play*` functions don't spawn an actor: the `VAWorld` keeps each sound's filter and reverb send up to date until it finishes, so they cost about the same as the `UGameplayStatics` functions plus their rays.

The `Spawn*` functions spawn a `VASource`, which plays the sound once it has been raytraced and then destroys itself when the sound finishes. Use them to read the source's results (e.g. `GetReverbResult`) or to stop it with `Stop()`, which also destroys it.

`PlaySoundAtLocation`, `SpawnSoundAtLocation` and `SpawnSoundAttached` take an `FVAOneShotSettings`, which sets the sound's volume and pitch multipliers, an optional `AttenuationSettings` override, and its reverb, muffling and clustering properties. It defaults to 64 reverb rays with 64 bounces.

```cpp
FVAOneShotSettings settings;
settings.VolumeMultiplier = Volume;
settings.PitchMultiplier = Pitch;
settings.AttenuationSettings = AttenuationSettings;
settings.ReverbRayCount = 64;
settings.ReverbBounceCount = 64;

// Fire and forget, e.g. an explosion or an impact
AVAWorld::PlaySoundAtLocation(this, Sound, SoundLocation, settings);

// Returns a VASource that can be modified or stopped
AVASource* source = AVAWorld::SpawnSoundAtLocation(this, Sound, SoundLocation, settings);
AVASource* attached = AVAWorld::SpawnSoundAttached(Sound, AttachComp, AttachBone, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, bStopWhenAttachedToDestroyed, settings);
```

- `PlaySoundAtLocation` and `SpawnSoundAtLocation` raytrace once by default, as the sound doesn't move. Its muffling is held from then on, even as the listener moves, and its reverb tail starts when the sound finishes. Set `settings.bRaytraceOnce = false` to keep raytracing for the sound's whole lifetime, e.g. for a long sound while doors open and close around it.
- `SpawnSoundAttached` ignores `bRaytraceOnce` and keeps raytracing as it moves, so it's the expensive choice for frequent sounds.
- The sound starts after the first raytracing result, not straight away, so it's muffled correctly from the beginning.
- A one-shot needs a `VAListener` that has begun play. Without one, `PlaySoundAtLocation`, `PlaySoundLeech`, `PlaySoundRelative` and the `Spawn*` functions log a warning and play nothing, rather than waiting for a listener. This includes sounds played from a `BeginPlay` that runs before the listener's.
- `PlaySoundAtLocation` needs a `VAWorld` that has begun play in the world context object's level, and does nothing otherwise. The `Spawn*` functions return null if the sound, world or component is missing.

#### Leech sounds

For frequent sounds on a character, like an enemy's footsteps or gunshots, put one `VAEmitter` on each enemy and play all of its sounds through it with `AVAWorld::PlaySoundLeech`. The sound is attached to the `VAEmitter` and uses its muffling and reverb instead of casting its own rays, so hundreds of sounds a second cost no more than the one emitter:

```cpp
AVAWorld::PlaySoundLeech(GunshotSound, EnemyEmitter, Volume, Pitch, AttenuationSettings);
```

- It plays immediately if the emitter has already been raytraced. Otherwise it waits, and is dropped if the emitter is destroyed first.
- It follows the emitter, and if the emitter is destroyed mid-sound, it finishes playing where it was.
- A listener can't be leeched, so use `PlaySoundRelative` for the player's own sounds.

#### Relative sounds

For the player's own sounds, `AVAWorld::PlaySoundRelative` replaces `UGameplayStatics::PlaySound2D`. It isn't raytraced, and plays unmuffled with the current listener's reverb. Like a `VASourceRelative`, it waits for the listener's first EAX, and plays as soon as the listener has it:

```cpp
AVAWorld::PlaySoundRelative(this, FootstepSound, Volume, Pitch);
```

#### Notes

- `PlaySoundLeech` and `PlaySoundRelative` do nothing if there's no audio device. `PlaySoundAtLocation` is still raytraced without one, but isn't heard.
- The `Play*` functions can't be stopped individually, so a looping sound would play forever. The Output Log warns once per sound and still plays it. Use `SpawnSoundAtLocation`, a `VASourceLeech` or a `VASourceRelative` for looping sounds instead, as they can be stopped.
- `StopOneShots()` on the `VAWorld` stops every sound started by the `Play*` functions.
- The `Play*` functions reuse their audio components. `MaxPooledAudioComponents` on the `VAWorld` (default 64) is how many idle components are kept for reuse. Raise it if more sounds than that regularly overlap.
- `AttenuationSettings` can also be set on a placed `VASource`.

### Geometry and materials

> [!NOTE]
> 1.12.0 removed `VAMaterialComponent`. See the [migration guide](https://vercidium.com/docs/unreal/migration-guide) for upgrading from 1.11.

The `VAWorld` finds raytracing geometry by itself, so there's nothing to add to your actors. It looks at every static mesh, shape component and landscape in its level when it begins play, in streamed sublevels, World Partition cells and level instances as they load, and in actors spawned during play (on its next tick, so a mesh set right after spawning is still found).

Geometry is only raytraced if it has a material. Geometry with no material is ignored.

| Geometry | Where its material comes from |
|---|---|
| Static mesh render triangles | Each material slot, see [Material slots](#material-slots) |
| Static mesh simple collision | The mesh's `VAMeshMaterial` `Material`. Without a `VAMeshMaterial`: the collision's physical material, else the material most slots use |
| Shape components (box, sphere, capsule) | The component's `Phys Material Override` |
| Landscapes | The landscape's `Default Phys Material` |

Geometry follows its actor when it moves, and is removed when the actor is destroyed or streams out. Hidden actors and actors with collision disabled still affect raytracing. `CollisionObjectTypes` on the `VAWorld` limits colliders to certain object types. Skeletal meshes, BSP brushes and dynamic meshes aren't supported, and neither is spline mesh deformation.

Geometry and sources in an unloaded streaming level don't affect sound, as their actors don't exist yet. Stream levels in before they're within earshot, e.g. with a larger streaming volume, or keep walls that matter for sound in the persistent level. Otherwise the muffling changes the moment the level loads.

#### Material slots

Each material slot of a static mesh's render triangles uses the first of these that gives it a material:
1. The slot's `SlotMaterials` entry on the mesh's [VAMeshMaterial](#static-mesh-materials). `bIgnore` leaves the slot out
2. The slot's render material's entry in the `VAWorld`'s `RenderMaterialMap`. Material instances also check their parent materials
3. That render material's physical material's entry in the `VAWorld`'s `PhysicalMaterialMap`
4. The `VAMeshMaterial`'s `Material`
5. Otherwise the slot is ignored. A mesh with every slot ignored isn't raytraced

So once `M_Brick` and `M_Glass` are in `RenderMaterialMap`, a building mesh with those two slots is raytraced as brick and glass, as one primitive with a material per triangle. Render materials are read from the Static Mesh asset's slots, and a placed component's `Override Materials` aren't used.

A mesh without a `VAMeshMaterial` is raytraced like one with `PropagateMode = All` and `MeshLOD = 0`, without flat transmission. If it has simple collision, that's what's raytraced, with the collision's physical material (the component's `Phys Material Override`, else the mesh's `Simple Collision Physical Material`) if it's mapped, else the material most of its slots use. So a mesh matched only by its collision's physical material is still raytraced.

#### Material maps

`RenderMaterialMap` and `PhysicalMaterialMap` on the `VAWorld` map render material and physical material assets to materials, e.g. `PM_Dirt` to Dirt. Materials are never matched by their asset name, so render materials and physical materials that aren't in a map are ignored.
- An entry for a parent material also applies to all of its material instances. An instance's own entry beats its parent's
- An entry can pick a built-in material, or a `MaterialAsset` from the `VAWorld`'s `Materials`. An entry whose `MaterialAsset` isn't in `Materials` logs a warning and isn't used
- The engine's default physical material is never matched, even if it's in the map

#### Static Mesh materials

A Static Mesh asset can carry its own material, so every placement of it is raytraced, e.g. a wall mesh that's always brick. Open the mesh in the Static Mesh editor, add an entry to **Asset User Data** in its Details panel, and choose **VAMeshMaterial** (`UVAMeshMaterialUserData`). Save the mesh, and it applies in every level.

- `Material` picks a built-in material (concrete, wood, glass, ...), or `MaterialAsset` a material asset, which must be in the `VAWorld`'s `Materials` array. It's used by simple collision, and by slots that have no `SlotMaterials` entry and whose render material and physical material aren't mapped.
- `SlotMaterials` gives individual material slots (by Material Slot Name) their own material, e.g. Glass for a building's window slot, ahead of the `VAWorld`'s maps. `bIgnore` leaves a slot's triangles out, e.g. decals or foliage cards. Simple collision has no slots, so a mesh that needs per-slot materials should use `PropagateMode = Visuals`.
- `bUseFlatTransmission` makes permeation rays lose a flat percentage of energy when they touch the mesh, using the material of the triangle they touch.
- `PermeationMaterial` (with its checkbox ticked) is the material inside the mesh, which permeation rays lose energy to based on the distance they travel inside it, e.g. Concrete inside a building whose surfaces are brick and glass. It defaults to `Material`, and isn't used with flat transmission.
- `PropagateMode` chooses between simple collision, render triangles or both, and `MeshLOD` picks which LOD is raytraced.
- It applies to every static mesh component using the mesh, including instanced and foliage meshes.
- Editing, adding or removing a `VAMeshMaterial` during Play In Editor rebuilds the geometry straight away.

#### Shape components

Box, sphere and capsule components are raytraced if their `Phys Material Override` (under Collision) is in the `VAWorld`'s `PhysicalMaterialMap`. They're always colliders, filtered by `CollisionObjectTypes`.

#### Planes

A mesh whose box collision has no thickness, like `/Engine/BasicShapes/Plane`, is an open sheet that always uses flat transmission (`bUseFlatTransmission` is ignored). It reflects rays from both sides, but flat transmission on the built-in materials is too low to muffle sound passing through it. Give it a `VACustomMaterial` with `FlatTransmissionLF` and `FlatTransmissionHF` near 1 to muffle that sound.

#### Landscapes

Set the landscape's `Default Phys Material` to a physical material in the `VAWorld`'s `PhysicalMaterialMap`, e.g. `PM_Dirt` mapped to Dirt. Each streaming proxy reads its own `Default Phys Material`. The layers' physical materials aren't used, as each landscape component is raytraced with one material.
- Each landscape component becomes one heightmap built from its collision, so collision must be enabled on the landscape. It works in cooked builds without baking.
- Holes painted with the visibility layer are left open.
- A landscape with a negative or zero scale on any axis is skipped with a warning.
- Landscapes always use flat transmission (`bUseFlatTransmission` is ignored), so sound crossing it is muffled from either side, however deep the source is (e.g. a cave far below). The built-in materials' flat transmission is low, so give landscapes a `VACustomMaterial` with `FlatTransmissionLF` and `FlatTransmissionHF` near 1.
- `LandscapeLOD` on the `VAWorld` simplifies every landscape for raytracing, without changing its collision. Each LOD halves the number of points, so 1 m quads become 2 m at LOD 1, 4 m at LOD 2, and so on. It defaults to 3 (roughly 8 m quads) to keep memory usage down on large landscapes. Lower it for terrain with sharp features smaller than that, such as narrow ridges or trenches.

#### Changes during play

What each render material and physical material resolves to is cached. Editing the `VAWorld`'s `RenderMaterialMap`, `PhysicalMaterialMap` or `Materials` in the Details panel (including during Play In Editor) rebuilds the geometry straight away, and so do `SetMaterials`, `SetCollisionObjectTypes` and `SetLandscapeLOD`. Editing a `VAMeshMaterial`, a render material's parent or physical material, or a mesh's or component's collision physical material during Play In Editor also rebuilds it.

Changing the maps from Blueprint or C++ doesn't, so call `RebuildPrimitives` (or `SyncPrimitive(Actor)` for one actor) afterwards. Until then, the old results are still used for geometry that spawns or streams in.

Call `SyncPrimitive(Actor)` on the `VAWorld` after:
- changing an actor's meshes
- adding instances to an instanced static mesh
- sculpting a landscape during play

In C++, `AVAWorld::GetMeshTriangles(MeshComponent, OutVertices, OutTriangleMaterials)` returns the mesh-space triangles a component's render triangles are raytraced with, and the SDK material of each triangle (ignored slots are left out), e.g. to check which material each slot got.

#### Materials

Material assets are created in the Content Browser and added to the `VAWorld`'s `Materials` array:
- **VADefaultMaterial** (`UVADefaultMaterial`) changes the properties of one of the built-in materials everywhere it's used. Pick it with `MaterialType`, and use `Reset To Defaults` to load the SDK's values.
- **VACustomMaterial** (`UVACustomMaterial`) is a new material. Use it in a `VAMeshMaterial`'s `MaterialAsset` or a material map entry.

Use `SetMaterials` on the `VAWorld` to change the list during play.

### Shipping builds

Cooked builds drop the CPU copy of a mesh's triangles unless the mesh has `Allow CPU Access` set, so a mesh raytraced by its render triangles (one without simple collision, or with a `VAMeshMaterial` set to `Visuals`) affects nothing in a packaged game until it's baked. Box, sphere, capsule and convex collision, and landscapes, don't need baking.

Click `Bake Geometry For Shipping` on the `VAWorld`, or run the `-run=VABake` commandlet, which bakes and saves every level that has a `VAWorld` (add `-MapPath=/Game/Maps` to only bake the levels under that folder). This stores one copy of the triangles per static mesh and LOD in the level, shared by every placement. The bake includes:
- the persistent level and its streaming sublevels, whatever their streaming method, and whether or not they're hidden in the editor, as the editor and the commandlet both load every sublevel
- every Level Instance, loading unloaded ones while it runs
- in World Partition maps, every actor, loading unloaded cells while it runs

The baked triangles are only used in cooked builds. The bake doesn't update itself, so saving or cooking a level whose meshes no longer match it (a mesh or LOD is added or no longer used, or a baked mesh was edited) logs a warning naming the mesh. Bake again and save to clear it. This check only looks at loaded actors, so in World Partition maps it can't tell that a mesh is no longer used.

On-screen warnings don't exist in Shipping builds, so check the log (`LogVAudio`) there.

### Networked voice chat

`AVANetworkedStreamSource` plays audio received over the network, e.g. another player's voice, raytraced like any other source. It works exactly like `AVAStreamSource`, and is a separate type so your netcode can find it. Sending and receiving the audio is up to you.

On the speaking player, an `AVAInputStreamSource` captures the microphone and broadcasts each chunk through `OnAudioCaptured`. Send those bytes to the other players, e.g. with an unreliable RPC on the player's pawn:

```cpp
// Speaker: forward captured audio to the server, which multicasts it to everyone else
void AMyCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (IsLocallyControlled() && Microphone)
        Microphone->OnAudioCaptured.AddDynamic(this, &AMyCharacter::OnMicrophoneCaptured);
}

void AMyCharacter::OnMicrophoneCaptured(const TArray<uint8>& Data)
{
    ServerSendVoice(Data, Microphone->GetCaptureSampleRate());
}

void AMyCharacter::ServerSendVoice_Implementation(const TArray<uint8>& Data, int32 SampleRate)
{
    MulticastReceiveVoice(Data, SampleRate);
}

// Listeners: play it from the speaker's VANetworkedStreamSource, attached to their pawn
void AMyCharacter::MulticastReceiveVoice_Implementation(const TArray<uint8>& Data, int32 SampleRate)
{
    if (IsLocallyControlled() || !Voice)
        return;

    if (!Voice->IsStreamOpen())
        Voice->OpenStream(EVAStreamFormat::Mono16, SampleRate);

    Voice->PushAudioData(Data);
}
```

- The stream must use the input source's `Format` (`Mono16` here) and the rate the device actually captures at, which is why the rate is sent along with the data.
- Raw 16-bit PCM is about 96 KB/s at 48 kHz, so compress it (e.g. Opus) before sending in production.
- Data pushed before the source is first raytraced is dropped, so the first moments of speech after it spawns may be lost.

### Open worlds

A level can only have one `VAWorld`, and everything else finds it automatically. Geometry is found by the `VAWorld`, and emitters, listeners and sources join it when they begin play and leave it when they end play, so World Partition cells, streamed sublevels and level instances are raytraced while they're loaded, with no extra setup. Placements of the same static mesh share one copy of its triangles, so placing a level instance many times is cheap.

#### Where to put the VAWorld

- Put it in the persistent level. With World Partition, also untick `Is Spatially Loaded` on it, so it never streams out.
- Never put a `VAWorld` inside a level instance or a streamed sublevel. Placing that instance twice gives a second `VAWorld`, which logs an error and is ignored.
- Each map needs its own `VAWorld`. Opening another map (`OpenLevel`, server travel) destroys the old map's `VAWorld` along with its emitters and geometry, and the new map's actors use the new one.
- Actors that seamless travel carries into the new map (e.g. a listener or source on the player's pawn) join the new map's `VAWorld` when it begins play. Don't add the `VAWorld` itself to the list of actors to keep.

#### World bounds

Only the box at the `VAWorld`'s location (its minimum corner) with size `BoundsSize` is raytraced. The box is always axis-aligned, so the Details panel only shows the VAWorld's location, and the rotate and scale gizmos do nothing.

In the editor, select the VAWorld to show a handle in the middle of each face of the box. Click one and drag the translate widget to move that face, while the opposite face stays put.

`BoundsFollow` decides what the box does during play:
- `None` (default): a fixed box, for levels that fit inside one box. It still follows the `VAWorld` actor if you move it.
- `Listener`: the box is centred on the current `VAListener`. Use this with World Partition, and set `BoundsSize` to cover the area that's loaded around the player, e.g. twice the runtime grid's loading range. The actor's location only places the box in the editor, and until a listener begins play.

Moving the bounds is expensive, so the box only moves once the listener (or the `VAWorld` actor) is `BoundsUpdateDistance` away (100 m by default). With `BoundsFollow = Listener`, `BoundsSize` is clamped to at least twice `BoundsUpdateDistance` on every axis, so the listener can't walk out of the box before it recentres. `SetBoundsFollow`, `SetBoundsSize` and `SetBoundsUpdateDistance` change these during play.

#### Sources outside the world bounds

Sources that are loaded but outside the box aren't raytraced. `bEmittersOutsideTheWorldAreMuffled` on the `VAWorld` decides what happens to them.

When it's off, they play clearly with no muffling or reverb, and get their reverb back once they're inside again.

When it's on (default), they're fully muffled, and:
- A sound that doesn't loop isn't started outside the box. `PlaySoundAtLocation`, `PlaySoundLeech` and the `Spawn*` functions log a warning and create nothing (`Spawn*` return null), and a looping `Play*` is dropped the same way. A `VASource` or `VASourceLeech` that doesn't loop returns false from `Play()` with a warning, and isn't retried once it's inside (with `bAutoDestroy`, it's destroyed).
- A sound that leaves the box, or that the box moves away from, is stopped that tick, cut off mid-sound. One-shots and sources that don't loop are then finished, and destroyed with `bAutoDestroy`.
- A looping `VASource` (including a looping `Spawn*` one-shot) or `VASourceLeech`, and an open stream source, wait outside the box and play again from the start once they're back inside. `Stop()` cancels that wait, and a stream whose sound stopped for any other reason isn't restarted.
- `IsSilencedByBounds` on an emitter or the `VAWorld` returns whether this applies. An emitter with `bClampPosition` on (the default for `VAEmitter`, but not for sources) is raytraced from the nearest point inside the box, so it's never silenced, and neither are leeches of it.

#### When the VAWorld ends play

If the `VAWorld` is destroyed or streams out while other actors stay loaded, they stop raytracing and wait. When another `VAWorld` begins play they join it, and their geometry is added again.

Their sounds stop when the `VAWorld` ends play:
- A `VASource` with `bAutoPlay` plays again once it rejoins. With `bAutoDestroy`, it only plays again if its sound loops, otherwise it's destroyed.
- A stream source's stream is closed, so call `OpenStream` again (and `OpenCapture` + `StartCapture` on a `VAInputStreamSource`).

## References
- [Vercidium Audio documentation](https://vercidium.com/docs)
- [vaudio-godot-native-openal-3d](https://github.com/vercidium-audio/vaudio-godot-native-openal-3d) — equivalent plugin for Godot (native C++)
- [vaudio-godot-mono-openal-3d](https://github.com/vercidium-audio/vaudio-godot-mono-openal-3d) — equivalent plugin for Godot (C#)

## Licencing

The Vercidium Audio SDK is free for non-commercial products only. To purchase a licence for commercial use, see [vercidium.com](https://vercidium.com).
