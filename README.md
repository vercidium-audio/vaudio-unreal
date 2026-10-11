# vaudio-unreal

Unreal Engine plugin for Vercidium Audio — raytraced audio simulation with realistic muffling, reverb, ambience and occlusion.

> [!WARNING]
> This plugin is experimental and requires much testing and feedback

This repository requires Vercidium Audio v1.11.0 to run. Windows, Linux and macOS (Apple Silicon) are supported, and sound plays through Unreal's own audio engine.
- Download the Vercidium Audio SDK from [vercidium.com](https://vercidium.com)

> Please note that the Vercidium Audio SDK is not free for commercial use. See [vercidium.com/eula](https://vercidium.com/eula)

## Features

- Muffle sounds in real time
- Accurate reverb in any environment
- Innovative event-based raytracing system
- Realistic energy-based model using materials
- Dynamic scene updates - automatically handles moving objects

## Requirements

- Unreal Engine 5, C++ project (this plugin is a C++ module, so your project needs a `Source` folder — if you have a Blueprint-only project, Unreal will offer to create one the first time you add a C++ plugin)
- Visual Studio (or another C++ toolchain) installed and set up for Unreal development
- [Vercidium Audio SDK](https://vercidium.com)

## Installation

### 1. Add the plugin

Download `vaudio-unreal.zip` from the [Releases page](https://github.com/vercidium-audio/vaudio-unreal/releases) and extract it into your project's `Plugins` folder. It contains the plugin source plus prebuilt binaries for Win64, Linux and Mac (arm64). You can also clone this repository instead, which builds the plugin from source. Either way, the `.uplugin` file must exist at:

```
YourProject/Plugins/vaudio-unreal/vaudio-unreal.uplugin
```

If you're using git, adding it as a submodule from your project root keeps it easy to update:

```
git submodule add <this-repo-url> Plugins/vaudio-unreal
```

Otherwise, just clone or extract this repository directly into `Plugins/vaudio-unreal`.

### 2. Add the Vercidium Audio SDK

This plugin links against the native Vercidium Audio SDK, which is not included in this repository. The SDK files must live in the plugin's `Source/ThirdParty/vaudio` folder:

```
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/include/vaudio.h
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/Win64/vaudionative.lib
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/Win64/vaudionative.dll
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/Linux/libvaudionative.so
YourProject/Plugins/vaudio-unreal/Source/ThirdParty/vaudio/lib/Mac/libvaudionative.dylib
```

Download the SDK from [vercidium.com](https://vercidium.com) and copy the 3D native SDK's files into the above locations. You only need the `lib` folders for the platforms you build for. To use the debug window, use the dev SDK and also copy `vaudio-debug-window.exe` and `glfw3.dll` into `lib/Win64`, or `vaudio-debug-window` and `libglfw.so.3` into `lib/Linux`.

When you build, the plugin copies these into `Binaries/ThirdParty/vaudio/<platform>` and loads them from there. If the SDK is missing, the build fails with a message saying where to put it.

### 3. Enable the plugin

- Regenerate your project files (right-click your `.uproject` → **Generate Visual Studio project files**), then build the project in Visual Studio (or open the `.uproject` and let Unreal prompt you to rebuild missing modules).
- Open the project in the Unreal Editor, go to **Edit → Plugins**, search for **Vercidium Audio**, and make sure it's enabled.
- Restart the editor if prompted.

## Usage

Once the plugin is enabled, these actors and components are available. Nothing needs a reference to the world: every actor and component finds the level's `VAWorld` by itself, in whatever order they begin play.

### Quick start

1. Place one **VAWorld** in the level. Its location is the minimum corner of the raytraced box and `BoundsSize` is the box's size, so move and size it to cover the playable area. Add at least one submix to `GroupedEAXSubmixes` (three is a good default): each one plays one of the reverbs the SDK produces.
2. Add a **VAMaterialComponent** to every actor that should block or reflect sound (walls, floors, props) and pick its `Material`.
3. Place one **VAListener**. By default it follows the player's camera (`bAutoFollowCamera`). Give it occlusion and permeation rays (e.g. `OcclusionRayCount` 1024, `OcclusionBounceCount` 8, `PermeationRayCount` 256, `PermeationBounceCount` 4), or nothing is muffled.
4. Place a **VASource** for each sound, set its `SourceSound` and `bAutoPlay`, and give it reverb rays (e.g. `ReverbRayCount` 64, `ReverbBounceCount` 64) so it has reverb.

Every source is muffled relative to the current listener automatically. Sounds should have a Sound Attenuation asset for distance falloff, and must use the **Play When Silent** virtualization mode, otherwise a fully muffled sound is stopped by the engine.

### World

- **VAWorld** (`AVAWorld`) owns the raytracing world: bounds, air absorption, threading, materials and reverb submixes. A level can only have one. With the dev SDK, `bRenderingEnabled` shows the scene in a separate debug window. `bShowDebugMessages` prints status lines on screen, and the same numbers are available to Blueprint (`GetRaytracingTime`, `GetGroupedEAXCount`, `GetRaytraceCount`, ...).

### Emitters and sources

- **VAListener** (`AVAListener`) is the point everything is heard from. A level can have several, and one is current at a time (`bCurrent`, `MakeCurrent()`). Sounds that use listener reverb are sent to its `ListenerReverbSubmix`.
- **VAEmitter** (`AVAEmitter`) is a raytraced point that plays no sound itself, e.g. on an enemy. Attach `VASourceLeech` actors to it to play several sounds with one set of rays.
- **VASource** (`AVASource`) is a raytraced 3D sound. It only plays once it has been raytraced (`IsReadyToPlay()`), so it never starts unmuffled. `Play()` can be called repeatedly and the sounds overlap. Looping comes from the sound asset.
- **VASourceLeech** (`AVASourceLeech`) is a 3D sound that reuses the muffling and reverb of the `VAEmitter` it's attached to (or its `Emitter` property) instead of casting rays, e.g. gunshots and footsteps on one character.
- **VASourceRelative** (`AVASourceRelative`) is a 2D sound for the player's own sounds. It isn't muffled, and uses the listener's reverb.
- **VASourceAmbient** (`AVASourceAmbient`) is a 2D sound for rain, wind or room tone, filtered by how enclosed the listener is. The listener needs ambient rays (`AmbientOcclusionRayCount` or `AmbientPermeationRayCount`).
- **VAStreamSource** (`AVAStreamSource`) is a raytraced sound that plays raw PCM pushed at runtime: `OpenStream`, `PushAudioData`, `CloseStream`.
- **VAInputStreamSource** (`AVAInputStreamSource`) is a stream fed by a microphone, with an `OnAudioCaptured` event for sending it elsewhere. Linux has no capture backend in Unreal.
- **VANetworkedStreamSource** (`AVANetworkedStreamSource`) is a stream for audio received over the network, see below.
- **VAVisualisation** (`UVAVisualisation`) is a component for an emitter that draws where its rays land in the level.

Useful emitter properties, shared by the listener and the raytraced sources:
- `bAffectsGroupedEAX`: this emitter's reverb is blended into the world's grouped reverbs (on by default for sources). When off, `bUseListenerReverb` chooses between the listener's reverb and none.
- `bRaytraceOnce`: stop casting rays after the first result, for one-shot sounds that don't move.
- `bKeepReverbTailAlive`: keep contributing to the reverb after the actor is destroyed, until its tail has faded.
- `GetMufflingFilterResult`, `GetReverbResult` and `GetAmbientFilterResult` expose the raytraced results to Blueprint.

### Geometry and materials

A **VAMaterialComponent** (`UVAMaterialComponent`) makes its actor part of the raytraced scene, along with any attached child actors that don't have their own. Shape components and simple collision are used where they exist, and render triangles otherwise.

- `Material` picks a built-in material (concrete, wood, glass, ...). `MaterialAsset` uses a material asset instead.
- `PropagateMode` chooses between colliders, render triangles or both, and `MeshLOD` picks which LOD is raytraced.
- `MaterialOverrides` gives individual components another material, e.g. a glass window in a brick wall.
- `CollisionObjectTypes` on the `VAWorld` limits colliders to certain object types.

Geometry is added and removed as actors spawn, are destroyed or stream in and out, and follows them when they move. Hidden actors and actors with collision disabled still block sound. Call `SyncPrimitive(Actor)` on the `VAWorld` after attaching an actor to one that has a material component, changing an actor's meshes, or adding instances to an instanced static mesh. Landscapes aren't supported yet.

Material assets are created in the Content Browser and added to the `VAWorld`'s `Materials` array:
- **VADefaultMaterial** (`UVADefaultMaterial`) changes the properties of one of the built-in materials everywhere it's used. Pick it with `MaterialType`, and use **Reset To Defaults** to load the SDK's values.
- **VACustomMaterial** (`UVACustomMaterial`) is a new material. Assign it to a component's `MaterialAsset`.

Use `SetMaterials` on the `VAWorld` to change the list during play.

### Shipping builds

Cooked builds can drop the CPU copy of a mesh's triangles. The material component marks its actor's meshes as CPU-accessible, and as a fallback **Bake Geometry For Shipping** on the `VAWorld` (or the `-run=VABake` commandlet) stores the triangles in the level.

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

The stream must use the input source's `Format` (`Mono16` here) and the rate the device actually captures at, which is why the rate is sent along with the data. Raw 16-bit PCM is about 88 KB/s at 44.1 kHz, so compress it (e.g. Opus) before sending in production. Data pushed before the source is first raytraced is dropped, so the first moments of speech after it spawns may be lost.

### Open worlds

A level can only have one `VAWorld`, and everything else finds it automatically. Geometry (`VAMaterialComponent`), emitters, listeners and sources join it when they begin play and leave it when they end play, so World Partition cells, streamed sublevels and level instances are raytraced while they're loaded, with no extra setup. Placements of the same static mesh share one copy of its triangles, so placing a level instance many times is cheap.

**Where to put the VAWorld**
- Put it in the persistent level. With World Partition, also untick **Is Spatially Loaded** on it, so it never streams out.
- Never put a `VAWorld` inside a level instance or a streamed sublevel. Placing that instance twice gives a second `VAWorld`, which logs an error and is ignored.
- Each map needs its own `VAWorld`. Opening another map (`OpenLevel`, server travel) destroys the old map's `VAWorld` along with its emitters and geometry, and the new map's actors use the new one.
- Actors that seamless travel carries into the new map (e.g. a listener or source on the player's pawn) join the new map's `VAWorld` when it begins play. Don't add the `VAWorld` itself to the list of actors to keep.

**World bounds**

Only the box at the `VAWorld`'s location (its minimum corner) with size `BoundsSize` is raytraced. `BoundsFollow` decides what that box does during play:
- `None` (default): a fixed box. Good for levels that fit inside one box. It still follows the `VAWorld` actor if you move it.
- `Listener`: the box is centred on the current `VAListener`. Use this with World Partition, and set `BoundsSize` to cover the area that's loaded around the player, e.g. twice the runtime grid's loading range. The plugin can't read the loading range itself. The actor's location only places the box in the editor, and until a listener begins play.

Moving the bounds makes the SDK re-check every raytraced trail, so they move in steps instead of every frame: the box only follows once the listener (or the `VAWorld` actor) is `BoundsUpdateDistance` away, 10 m by default. `SetBoundsFollow` and `SetBoundsSize` change these during play.

Sources that are loaded but outside the box aren't raytraced. `bEmittersOutsideTheWorldAreMuffled` on the `VAWorld` decides whether they're fully muffled (default) or not muffled at all.

**When the VAWorld ends play**

If the `VAWorld` is destroyed or streams out while other actors stay loaded, they stop raytracing and wait. When another `VAWorld` begins play they join it, and their geometry is added again. Their sounds stop when the `VAWorld` ends: a `VASource` with `bAutoPlay` plays again once it rejoins, but a stream source's stream is closed, so call `OpenStream` again (and `OpenCapture` + `StartCapture` on a `VAInputStreamSource`).

## References
- [Vercidium Audio documentation](https://vercidium.com/docs)
- [vaudio-godot-native-openal-3d](https://github.com/vercidium-audio/vaudio-godot-native-openal-3d) — equivalent plugin for Godot (native C++)
- [vaudio-godot-mono-openal-3d](https://github.com/vercidium-audio/vaudio-godot-mono-openal-3d) — equivalent plugin for Godot (C#)

## Licencing

The Vercidium Audio SDK is free for non-commercial products only. To purchase a licence for commercial use, see [vercidium.com](https://vercidium.com).
