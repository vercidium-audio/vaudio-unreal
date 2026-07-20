# vaudio-unreal

Unreal Engine plugin for Vercidium Audio — raytraced audio simulation with realistic muffling, reverb, ambience and occlusion.

> [!WARNING]
> This plugin is experimental and requires much testing and feedback

This repository requires Vercidium Audio v1.3.0 and OpenAL Soft to run:
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

Clone (or copy) this repository into your project's `Plugins` folder, so that the `.uplugin` file exists at:

```
YourProject/Plugins/vaudio-unreal/vaudio-unreal.uplugin
```

If you're using git, adding it as a submodule from your project root keeps it easy to update:

```
git submodule add <this-repo-url> Plugins/vaudio-unreal
```

Otherwise, just clone or extract this repository directly into `Plugins/vaudio-unreal`.

### 2. Add the Vercidium Audio SDK

This plugin links against the native Vercidium Audio SDK, which is not included in this repository. The SDK files must live in the `ThirdParty/vaudio` folder at the root of your project (next to your `.uproject` file, not inside the plugin):

```
YourProject/ThirdParty/vaudio/include/vaudio.h
YourProject/ThirdParty/vaudio/lib/Win64/vaudionative.lib
YourProject/ThirdParty/vaudio/lib/Win64/vaudionative.dll
```

Download the SDK from [vercidium.com](https://vercidium.com) and copy the files into the above locations.

### 3. Enable the plugin

- Regenerate your project files (right-click your `.uproject` → **Generate Visual Studio project files**), then build the project in Visual Studio (or open the `.uproject` and let Unreal prompt you to rebuild missing modules).
- Open the project in the Unreal Editor, go to **Edit → Plugins**, search for **Vercidium Audio**, and make sure it's enabled.
- Restart the editor if prompted.

> [!NOTE]
> `vaudionative.dll` is delay-loaded, so it's only loaded the first time a plugin function is called (e.g. `AVAudioWorld::BeginPlay`), not at editor startup. If that first call fails with a "module not found" error, UBT likely hasn't copied `vaudionative.dll` into `YourProject/Binaries/Win64` yet (this copy only happens on a full build — incremental/hot-reload builds can skip it). Manually copy `vaudionative.dll` from `YourProject/Plugins/vaudio-unreal/Binaries/Win64/` into `YourProject/Binaries/Win64/` to fix this.

## Usage

Once the plugin is enabled, several new actor/component types are available in the editor:

- **VA Audio World** (`AVAudioWorld`) — place one of these in your level. It owns the raytracing world and scans the level on `BeginPlay` for any actor with a **VA Audio Material** component to build the acoustic scene geometry. Configure world bounds, air absorption, threading and reverb submixes on this actor.
- **VA Audio Material** component (`UVAudioMaterialComponent`) — add this component to any actor whose mesh should participate in raytracing (walls, floors, props, etc). Choose a built-in material (concrete, wood, glass, ...) from the `Material` dropdown, or assign a material asset to `MaterialAsset` (see below) to override it. Assign the `AudioWorld` reference either way.
- **VA Audio Listener** (`AVAudioListener`) — place exactly one per world (e.g. on the player camera). It's the world's reference point for reverb and ambient filtering; every other emitter is raytraced against it. Assign the `AudioWorld` reference and its `TargetEmitters`.
- **VA Audio Source** (`AVAudioSource`) — place one per 3D sound source that should be raytraced (occlusion/permeation) against the listener. Assign the `AudioWorld` reference and the sound to play; it drives occlusion, permeation and reverb automatically as the scene changes.
- **VA Audio Continuous** (`AVAudioContinuous`) — a raytracing target that plays no sound of its own, for actors (e.g. an enemy) that need an up-to-date muffle/occlusion value every frame so multiple attached `AVAudioRelativeSource`s can reuse it instead of each raytracing independently.
- **VA Audio Relative Source** (`AVAudioRelativeSource`) — for sounds relative to the listener or an `AVAudioContinuous` (footsteps, gunshots) that reuse that emitter's reverb/muffling without raytracing themselves.
- **VA Audio Ambient Source** (`AVAudioAmbientSource`) — for ambient rain/wind/room-tone sounds that reuse the listener's ambient filter without raytracing or reverb.

You can also create material assets in the Content Browser and add them to the `AVAudioWorld`'s `Materials` array:
- **VA Audio Material** (`UVAudioMaterialAsset`) — overrides the default properties (absorption, scattering, transmission) of one of the 23 built-in materials. Pick its name from the `MaterialType` dropdown and use **Reset To Defaults** to pull in the SDK's base values, then tweak as needed.
- **VA Audio Custom Material** (`UVAudioCustomMaterialAsset`) — defines a brand new material with its own free-form (unique) `MaterialName` and an SDK-assigned ID. Assign it to a `UVAudioMaterialComponent`'s `MaterialAsset` field to use it on geometry.

## References
- [Vercidium Audio documentation](https://vercidium.com/docs)
- [vaudio-godot-openal](https://github.com/vercidium-audio/godot-openal) — equivalent plugin for Godot

## Licencing

The Vercidium Audio SDK is free for non-commercial products only. To purchase a licence for commercial use, see [vercidium.com](https://vercidium.com).
