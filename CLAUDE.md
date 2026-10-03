# Overview

This is a public repo for the Vercidium Audio Unreal Engine plugin, which is a wrapper around the Vercidium Audio 3D C SDK (vaudionative.dll).

~\vaudiofps2\ThirdParty\vaudio\include\vaudio.h is the public header for Vercidium Audio.

Don't attempt to build the plugin yourself. The user will build it and inform you of any errors.

Coding guidelines:
- Log with the macros in `Private/VALog.h` (`VA_LOG`, `VA_WARN`, `VA_ERROR`, plus `_NAMED` and `_RESULT` variants). Warnings and errors also appear on screen, but on-screen messages don't exist in Shipping, so never use `GEngine->AddOnScreenDebugMessage` as the only channel
- Use camelCase for variable names
- Don't use single capitalised acronyms for variable names, e.g. use `position` instead of `P`, `vaWorld` instead of `VAW`, etc
- Don't capitalise variable names, e.g. use `listener` instead of `Listener`

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

Actor BeginPlay order isn't guaranteed, so never require another actor (e.g. the listener) to have begun play first. Wait for it in Tick instead, or let `AVAWorld` wire things up when it registers.

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
