# vaudio-unreal

Unreal Engine plugin for Vercidium Audio — raytraced audio simulation with realistic muffling, reverb, ambience and occlusion.

> [!WARNING]
> This plugin is experimental. Please raise issues on the [Support repository](https://github.com/vercidium-audio/support)

This repository requires Vercidium Audio v1.11.1 to run.
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

Download the zip for your engine version from the [Releases page](https://github.com/vercidium-audio/vaudio-unreal/releases) (`vaudio-unreal-4.27.zip`, `vaudio-unreal-5.7.zip` or `vaudio-unreal-5.8.zip`) and extract it into your project's `Plugins` folder. It contains the plugin source plus prebuilt binaries for Win64, Linux and Mac. On 5.x you can also clone this repository instead, which builds the plugin from source. On 4.27 you must use the zip, as the repository's source only builds on 5.x.

The `.uplugin` file must exist at:
```
YourProject/Plugins/vaudio-unreal/vaudio-unreal.uplugin
```

If you're using git, addi it as a submodule from your project root:

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
```

[Download the SDK](https://vercidium.com) and:
- if using Unreal 5.7 onwards, copy the files of the double-precision native SDK (`3d/native-double`) into the above locations.
- if using Unreal 4.27, copy the files of the single-precision native SDK (`3d/native`) into the above locations.

You only need the `lib` folders for the platforms you build for. To use the debug window, use the dev SDK and also copy:
- For Window: `vaudio-debug-window.exe` and `glfw3.dll` into `lib/Win64`
- For Linux: `vaudio-debug-window` and `libglfw.so.3` into `lib/Linux`
- For Mac: `vaudio-debug-window` and `libglfw.3.dylib` into `lib/Mac`

The Mac SDK is universal (arm64 and x86_64), so Mac builds can target Apple Silicon, Intel or Universal. The debug window is universal too, so it opens on Apple Silicon and Intel.

> [!WARNING]
> Mac intel builds have only been tested under Rosetta, not on Intel hardware.

When you build, the plugin copies the above dll/so/dylib files into `Binaries/ThirdParty/vaudio/<platform>` and loads them from there. A Blueprint-only project never builds, so the prebuilt plugin from the release zip loads the SDK straight from `Source/ThirdParty/vaudio/lib/<platform>` instead.

### 3. Enable the plugin

- Regenerate your project files (right-click your `.uproject` and click `Generate Visual Studio project files`), then build the project in Visual Studio (or open the `.uproject` and let Unreal prompt you to rebuild missing modules).
- Open the project in the Unreal Editor, go to `Edit > Plugins`, search for `Vercidium Audio`, and enable it.
- Restart the editor if prompted.

## Unreal Engine 4.27

Use `vaudio-unreal-4.27.zip` from the [Releases page](https://github.com/vercidium-audio/vaudio-unreal/releases). Cloning this repository doesn't work on 4.27. The SDK setup is the same as on 5.x, except it uses the single precision SDK (`3d/native`) rather than `3d/native-double`, as 4.27 positions are floats.

Some features don't exist in 4.27, so the plugin doesn't have them there:
- World Partition isn't available, so the World Partition notes in this README don't apply, and `Bake Geometry For Shipping` covers the level's loaded actors.
- Level Instances aren't available, so a `VAMaterialComponent` can't reach into one.

Other differences on 4.27:
- Unreal's positions are floats, so geometry and sources far from the origin are less precise than on 5.x.
- Landscapes and convex hulls are read from PhysX collision rather than Chaos. Landscape holes follow that collision, which may be one quad away from where they were painted.

Platform requirements:
- **Windows:** build with Visual Studio 2019.
- **Linux:** 4.27 must be built from source, and prebuilt modules only load in the exact engine build that made them. A Blueprint-only project fails at startup with `Plugin 'vaudio-unreal' failed to load because module 'VaudioUnreal' could not be found`. Use a C++ project instead, which rebuilds the plugin for your engine.
- **Mac:** the 4.27 editor is x86_64 only, and runs under Rosetta on Apple Silicon. To build a C++ project with Xcode 27, set the below environment variable:

```
export CCC_OVERRIDE_OPTIONS="# +-Wno-error +-Wno-deprecated-builtins +-fno-access-control +-Wl,-rpath,/usr/lib/swift"
```

## Usage

### Quick start

1. Place one **VAWorld** in the level. Its location is the minimum corner of the raytraced box and `BoundsSize` is the box's size, so move and size it to cover the playable area.
2. For reverb to work, add at least one submix to `VAWorld.GroupedEAXSubmixes` (I recommend at least three)
3. Add a **VAMaterialComponent** to every actor that should affect raytracing (walls, floors, props) and select its `Material` (brick, concrete, dirt, etc).
4. Place one **VAListener**. By default it follows the player's camera (`bAutoFollowCamera`). For muffling to work, enable its occlusion and permeation rays (e.g. `OcclusionRayCount=1024`, `OcclusionBounceCount=8`, `PermeationRayCount=128`, `PermeationBounceCount=2`)
5. Place a **VASource** for each sound, set its `SourceSound` and `bAutoPlay`, and give it reverb rays (e.g. `ReverbRayCount=64`, `ReverbBounceCount=64`) so it has reverb.

Every source is muffled relative to the current listener automatically. Sounds should have a Sound Attenuation asset for distance falloff, and must use the **Play When Silent** virtualization mode, otherwise a fully muffled sound will be stopped by the engine.

### World

- **VAWorld** (`AVAWorld`) owns the raytracing world: bounds, air absorption, threading, materials and reverb submixes. A level can only have one. With the dev SDK, `bRenderingEnabled` shows the scene in a separate debug window.

### Emitters and sources

- **VAListener** (`AVAListener`) is the point everything is heard from. A level can have several, and one is current at a time (`bCurrent`, `MakeCurrent()`). Sounds that use listener reverb are sent to its `ListenerReverbSubmix`. Listeners can share one submix asset, and the current listener controls its reverb.
- **VASource** (`AVASource`) is a raytraced 3D sound. It only plays once it has been raytraced (`IsReadyToPlay()`), so it never starts unmuffled. `Play()` can be called repeatedly and the sounds overlap. Looping comes from the sound asset.
- **VAEmitter** (`AVAEmitter`) is a raytraced point that plays no sound itself, e.g. on an enemy. Use `VASourceLeech` actors to play sounds
- **VASourceLeech** (`AVASourceLeech`) is a 3D sound that reuses the muffling and reverb of the `VAEmitter` it's attached to, instead of casting rays, e.g. many gunshots and footsteps sounds will use the one `VAEmitter`'s raytracing properties
- **VASourceRelative** (`AVASourceRelative`) is a non-3D sound for the player's own sounds like footsteps. It isn't muffled or raytraced, and uses the listener's reverb.
- **VASourceAmbient** (`AVASourceAmbient`) is a non-3D sound for rain or wind, filtered by how enclosed the listener is. The listener needs ambient rays (`AmbientOcclusionRayCount` or `AmbientPermeationRayCount`) for this to work
- **VAStreamSource** (`AVAStreamSource`) is a raytraced sound that plays raw PCM data pushed at runtime: `OpenStream`, `PushAudioData`, `CloseStream`.
- **VAInputStreamSource** (`AVAInputStreamSource`) is a stream fed by a microphone, with an `OnAudioCaptured` event for sending it elsewhere. Not supported on Linux.
- **VANetworkedStreamSource** (`AVANetworkedStreamSource`) is a stream for audio received over the network
- **VAVisualisation** (`UVAVisualisation`) is a component that renders dots where rays bounce. Must be attached to a `VAListener`, `VASource` or `VAEmitter`

Useful emitter properties, shared by the listener and the raytraced sources:
- `bAffectsGroupedEAX`: this emitter's reverb is blended into the world's grouped reverbs (on by default for sources). When off, `bUseListenerReverb` chooses between the listener's reverb and none.
- `bRaytraceOnce`: stop casting rays once the first results are in, for short sounds. The emitter keeps its reverb and ambience, and its muffling keeps updating as the listener moves.
- `SetSuspended`: stop casting rays on demand, and start again later, e.g. for a sound that's paused or too far away. Its reverb and ambience don't update while suspended, even if it moves.
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
| `PlaySoundAtLocation` | `UGameplayStatics::PlaySoundAtLocation` | nothing | Its own rays, cast once |
| `PlaySoundLeech` | | nothing | Reuses a `VAEmitter`'s rays |
| `PlaySoundRelative` | `UGameplayStatics::PlaySound2D` | nothing | None |
| `SpawnSoundAtLocation` | `UGameplayStatics::SpawnSoundAtLocation` | `AVASource*` | Its own rays, cast once |
| `SpawnSoundAttached` | `UGameplayStatics::SpawnSoundAttached` | `AVASource*` | Its own rays, every frame |

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

- A sound at a location raytraces once (`bRaytraceOnce`), and its reverb tail starts when the sound finishes. An attached sound keeps raytracing every frame as it moves, so `SpawnSoundAttached` is the expensive choice for frequent sounds.
- The sound starts after the first raytracing result, not straight away, so it's muffled correctly from the beginning.
- A one-shot needs a `VAListener` that has begun play. Without one, `PlaySoundAtLocation`, `PlaySoundLeech` and the `Spawn*` functions log a warning and play nothing, rather than waiting for a listener. This includes sounds played from a `BeginPlay` that runs before the listener's.
- `PlaySoundAtLocation` needs a `VAWorld` that has begun play in the world context object's level, and does nothing otherwise. The `Spawn*` functions return null if the sound, world or component is missing.

For frequent sounds on a character, like an enemy's footsteps or gunshots, put one `VAEmitter` on each enemy and play all of its sounds through it with `AVAWorld::PlaySoundLeech`. Like a `VASourceLeech`, the sound is attached to the `VAEmitter` and uses its muffling and reverb instead of rays of its own, so hundreds of sounds a second cost no more rays than the one emitter:

```cpp
AVAWorld::PlaySoundLeech(GunshotSound, EnemyEmitter, Volume, Pitch, AttenuationSettings);
```

It plays immediately, as the emitter should already be raytraced. A sound started before then waits, and is dropped without playing if the emitter is destroyed first. It follows the emitter, and if the emitter is destroyed mid-sound it finishes playing where it was. A listener can't be leeched, so use `PlaySoundRelative` for the player's own sounds.

For the player's own sounds, `AVAWorld::PlaySoundRelative` replaces `UGameplayStatics::PlaySound2D`. Like a `VASourceRelative` it isn't raytraced, so it plays straight away, unmuffled and with the current listener's reverb:

```cpp
AVAWorld::PlaySoundRelative(this, FootstepSound, Volume, Pitch);
```

`PlaySoundLeech` and `PlaySoundRelative` do nothing if there's no audio device. `PlaySoundAtLocation` is still raytraced without one, but isn't heard.

The `Play*` functions release a sound when it finishes, so a looping sound would play forever. The Output Log warns once per sound and still plays it. Use `SpawnSoundAtLocation`, a `VASourceLeech` or a `VASourceRelative` for looping sounds instead, as they can be stopped. `StopOneShots()` on the `VAWorld` stops every sound started by the `Play*` functions at once.

The `Play*` functions reuse their audio components: once a sound finishes, its component is kept by the `VAWorld` and reset for the next sound, so playing many sounds doesn't create a new component each time. `MaxPooledAudioComponents` on the `VAWorld` (default 64) is how many idle components are kept, and any finished beyond it are destroyed. Raise it if more sounds than that overlap regularly. The pooled components are owned by the `VAWorld`.

`AttenuationSettings` can also be set on a placed `VASource`.

### Geometry and materials

A **VAMaterialComponent** (`UVAMaterialComponent`) makes its actor part of the raytraced scene, along with any attached child actors that don't have their own. Shape components and simple collision are used where they exist, and render triangles otherwise.

**Planes**: a mesh whose box collision has no thickness, like `/Engine/BasicShapes/Plane`, is an open sheet that always uses flat transmission (`bUseFlatTransmission` is ignored). It reflects rays from both sides, but if the listener casts permeation rays, flat transmission on the built-in materials is too low to muffle sound crossing it. Give it a `VACustomMaterial` with `FlatTransmissionLF`/`HF` near 1 to muffle that sound.

On a **Level Instance** actor, it also covers the actors in the instance's level that don't have their own, added once that level has loaded. In World Partition maps, level instances with the default "Partitioned" Level Behavior are embedded into the map's cells when played, so for those put the `VAMaterialComponent` on actors inside the instance's level instead.

- `Material` picks a built-in material (concrete, wood, glass, ...). `MaterialAsset` uses a material asset instead.
- `PropagateMode` chooses between colliders, render triangles or both, and `MeshLOD` picks which LOD is raytraced.
- `MaterialOverrides` gives individual components another material, e.g. a glass window in a brick wall.
- `CollisionObjectTypes` on the `VAWorld` limits colliders to certain object types.

Geometry is added and removed as actors spawn, are destroyed or stream in and out, and follows them when they move. Hidden actors and actors with collision disabled still block sound. Call `SyncPrimitive(Actor)` on the `VAWorld` after attaching an actor to one that has a material component, changing an actor's meshes, or adding instances to an instanced static mesh.

Geometry and sources in an unloaded streaming level don't affect sound, as their actors don't exist yet. Stream levels in before they're within earshot, e.g. with a larger streaming volume, or keep walls that matter for sound in the persistent level. Otherwise the muffling changes the moment the level loads.

**Landscapes** work the same way: add a `VAMaterialComponent` to the landscape actor (and to each streaming proxy, which is its own actor). Every landscape component becomes one heightmap built from its collision, so it works in cooked builds without baking, and it needs collision to be enabled on the landscape. A landscape with a negative or zero scale on any axis is skipped with a warning. Each component is an open surface that always uses flat transmission (`bUseFlatTransmission` is ignored), so it muffles sound crossing it from either side, however deep the source is (e.g. a cave far below). Flat transmission uses the material's `FlatTransmissionLF`/`HF`, which are low on the built-in materials, so give landscapes a `VACustomMaterial` that has them near 1. Holes painted with the visibility layer are left open. `LandscapeLOD` on the `VAWorld` simplifies every landscape for raytracing without changing its collision: LOD N gives each component 1/2^N as many rows and columns of evenly spaced points (rounded up), so 1 m quads become roughly 2 m at LOD 1, 4 m at LOD 2 and so on. Heights are taken from the collision's surface and components still meet without gaps. It defaults to 3 (roughly 8 m quads), which keeps the memory used by large landscapes down. Lower it for terrain with sharp features smaller than that, such as narrow ridges or trenches. Call `SyncPrimitive` on the landscape actor after sculpting it during play.

Material assets are created in the Content Browser and added to the `VAWorld`'s `Materials` array:
- **VADefaultMaterial** (`UVADefaultMaterial`) changes the properties of one of the built-in materials everywhere it's used. Pick it with `MaterialType`, and use **Reset To Defaults** to load the SDK's values.
- **VACustomMaterial** (`UVACustomMaterial`) is a new material. Assign it to a component's `MaterialAsset`.

Use `SetMaterials` on the `VAWorld` to change the list during play.

### Shipping builds

Cooked builds drop the CPU copy of a mesh's triangles unless the mesh has "Allow CPU Access" set, so a mesh raytraced by its render triangles (one without simple collision, or under a material component set to `Visuals`) affects nothing in a packaged game until it's baked. Box, sphere, capsule and convex collision, and landscapes, don't need baking. **Bake Geometry For Shipping** on the `VAWorld` (or the `-run=VABake` commandlet, which bakes and saves every level with a `VAWorld`) stores one copy of the triangles per static mesh and LOD in the level, shared by every placement. It covers loaded actors, level instances, and in World Partition maps every actor, loading unloaded cells while it runs. The live mesh is used whenever it still has its triangles, so the bake is only read in cooked builds. The bake doesn't update itself, so saving or cooking a level whose meshes no longer match it (a mesh or LOD is added or no longer used, or a baked mesh was edited) logs a warning naming the mesh. That check only looks at loaded actors, so in World Partition maps it can't tell that a mesh is no longer used. Bake again and save to clear it.

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

The stream must use the input source's `Format` (`Mono16` here) and the rate the device actually captures at, which is why the rate is sent along with the data. Raw 16-bit PCM is about 96 KB/s at 48 kHz, so compress it (e.g. Opus) before sending in production. Data pushed before the source is first raytraced is dropped, so the first moments of speech after it spawns may be lost.

### Open worlds

A level can only have one `VAWorld`, and everything else finds it automatically. Geometry (`VAMaterialComponent`), emitters, listeners and sources join it when they begin play and leave it when they end play, so World Partition cells, streamed sublevels and level instances are raytraced while they're loaded, with no extra setup. Placements of the same static mesh share one copy of its triangles, so placing a level instance many times is cheap.

**Where to put the VAWorld**
- Put it in the persistent level. With World Partition, also untick **Is Spatially Loaded** on it, so it never streams out.
- Never put a `VAWorld` inside a level instance or a streamed sublevel. Placing that instance twice gives a second `VAWorld`, which logs an error and is ignored.
- Each map needs its own `VAWorld`. Opening another map (`OpenLevel`, server travel) destroys the old map's `VAWorld` along with its emitters and geometry, and the new map's actors use the new one.
- Actors that seamless travel carries into the new map (e.g. a listener or source on the player's pawn) join the new map's `VAWorld` when it begins play. Don't add the `VAWorld` itself to the list of actors to keep.

**World bounds**

Only the box at the `VAWorld`'s location (its minimum corner) with size `BoundsSize` is raytraced. The box is always axis-aligned, so the Details panel only shows the VAWorld's location and the rotate and scale gizmos do nothing. Select the VAWorld to show a handle in the middle of each face of the box: click one and drag the translate widget to move that face along its axis while the opposite face stays put (dragging a min face moves the VAWorld too). The handles only show in the editor, not during play. `BoundsFollow` decides what that box does during play:
- `None` (default): a fixed box. Good for levels that fit inside one box. It still follows the `VAWorld` actor if you move it.
- `Listener`: the box is centred on the current `VAListener`. Use this with World Partition, and set `BoundsSize` to cover the area that's loaded around the player, e.g. twice the runtime grid's loading range. The plugin can't read the loading range itself. The actor's location only places the box in the editor, and until a listener begins play.

Moving the bounds makes the SDK re-check every raytraced trail, so they move in steps instead of every frame: the box only follows once the listener (or the `VAWorld` actor) is `BoundsUpdateDistance` away, 100 m by default. With `BoundsFollow = Listener`, `BoundsSize` is clamped to at least twice `BoundsUpdateDistance` on every axis, so the listener can't walk out of the box before it recentres. `SetBoundsFollow`, `SetBoundsSize` and `SetBoundsUpdateDistance` change these during play.

Sources that are loaded but outside the box aren't raytraced. `bEmittersOutsideTheWorldAreMuffled` on the `VAWorld` decides what happens to them. When it's off, they aren't muffled at all and play clearly with no reverb (they don't wait for their own reverb, which they can't get outside), and get their reverb back once they're inside again. When it's on (default), they're fully muffled, and the bounds are final for raytraced sounds:
- A sound that doesn't loop isn't started outside the box. `PlaySoundAtLocation`, `PlaySoundLeech` and the `Spawn*` functions log a warning and create nothing (`Spawn*` return null), and a looping `Play*` is dropped the same way. A `VASource` or `VASourceLeech` that doesn't loop returns false from `Play()` with a warning, and isn't retried once it's inside (with `bAutoDestroy`, it's destroyed).
- A sound that leaves the box, or that the box moves away from, is stopped that tick, cut off mid-sound. One-shots and sources that don't loop are then finished, and destroyed with `bAutoDestroy`.
- A looping `VASource` (including a looping `Spawn*` one-shot) or `VASourceLeech`, and an open stream source, wait outside the box and play again from the start once they're back inside. `Stop()` cancels that wait, and a stream whose sound stopped for any other reason isn't restarted.
- `IsSilencedByBounds` on an emitter or the `VAWorld` returns whether this applies. An emitter with `bClampPosition` on (the default for `VAEmitter`, but not for sources) is raytraced from the nearest point inside the box, so it's never silenced, and neither are leeches of it.

**When the VAWorld ends play**

If the `VAWorld` is destroyed or streams out while other actors stay loaded, they stop raytracing and wait. When another `VAWorld` begins play they join it, and their geometry is added again. Their sounds stop when the `VAWorld` ends: a `VASource` with `bAutoPlay` plays again once it rejoins (with `bAutoDestroy`, only if its sound loops, otherwise it's destroyed), but a stream source's stream is closed, so call `OpenStream` again (and `OpenCapture` + `StartCapture` on a `VAInputStreamSource`).

## References
- [Vercidium Audio documentation](https://vercidium.com/docs)
- [vaudio-godot-native-openal-3d](https://github.com/vercidium-audio/vaudio-godot-native-openal-3d) — equivalent plugin for Godot (native C++)
- [vaudio-godot-mono-openal-3d](https://github.com/vercidium-audio/vaudio-godot-mono-openal-3d) — equivalent plugin for Godot (C#)

## Licencing

The Vercidium Audio SDK is free for non-commercial products only. To purchase a licence for commercial use, see [vercidium.com](https://vercidium.com).
