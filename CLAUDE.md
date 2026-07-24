# Overview

~\vaudiofps2\ThirdParty\vaudio\include\vaudio.h is the public header for Vercidium Audio.

Coding guidelines:
- Don't write "if (!something) return;" with no reasoning why 'something' could be null. Rather than letting null checks permeate the codebase, fix them early. If it genuinely can be null, leave a comment explaining why
- Use DisplayWarning() to surface errors to the user on the screen. I used to use VALog() instead
- Use camelCase for variable names
- Don't use single capitalised acronyms for variable names - use position instead of P, vaWorld instead of VAW, etc.

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