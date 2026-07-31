# Overview

This is a public repo for the Vercidium Audio Unreal Engine plugin, which is a wrapper around the vaudionative.dll file, which is the Vercidium Audio (vaudio) C SDK.

~\vaudiofps2\ThirdParty\vaudio\include\vaudio.h is the public header for Vercidium Audio.

Don't attempt to build the plugin yourself. The user will build it and inform you of any errors.

Coding guidelines:
- Use DisplayWarning() to surface errors to the user on the screen
- Use camelCase for variable names
- Use camelCase for field and parameter names, e.g. 'result' instead of 'Result'
- Don't use single capitalised acronyms for variable names - use position instead of P, vaWorld instead of VAW, etc
- Don't capitalies variable names, e.g. 'listener' instead of 'Listener'

## Actor Initialisation

If an actor has invalid configuration, disable it with `SetActorTickEnabled(false)`, rather than letting `if (!AudioWorld)` or `if (!Emitter)` checks pollute the rest of the code, e.g. in `VAudioRelativeSource.cpp`:

```cpp
void AVAudioRelativeSource::BeginPlay()
{
	Super::BeginPlay();

	// Disable the actor if validation fails
	if (SourceSounds.Num() == 0)
	{
		DisplayWarning(TEXT("[VA] RelativeSource '%s' has no SourceSounds and will not play sound"), *GetActorNameOrLabel());
		SetActorTickEnabled(false);
		return;
	}
}
```

## VAResult Handling

When a va* function returns a VAResult, ensure all options are handled, e.g. in `VAudioListener.cpp`:

```cpp
VAResult result = vaEmitterAddTarget(Emitter, Target->GetVAEmitter());

if (result == VA_FEATURE_DISABLED)
{
    DisplayWarning(TEXT("[VA] Listener '%s' cannot have targets as it does not cast occlusion or permeation rays"), *GetActorNameOrLabel());
}
else if (result == VA_NOT_ADDED_TO_WORLD)
{
    DisplayWarning(TEXT("[VA] Listener '%s' has a target '%s' that has not been assigned to the same World as this listener. This target will not be raytraced"), *GetActorNameOrLabel(), *Target->GetActorNameOrLabel());
}
else
{
    check(result == VA_SUCCESS);
}
```