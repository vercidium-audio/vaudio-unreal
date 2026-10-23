#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
class USoundSubmix;
class USoundEffectSubmixPreset;

// Sets the component's send to a reverb submix. Unlike Godot, this must be pre distance attenuation, else distant sounds contribute less to the reverb than vaudio's relative gain says. Does nothing until the component is playing
void VASetReverbSend(UAudioComponent* Component, USoundSubmix* Submix, float SendLevel);

// Mutes or restores the component's main submix output without touching its submix sends, so reverb keeps playing while the dry signal is silenced. Does nothing until the component is playing
void VASetDryOutputEnabled(UAudioComponent* Component, bool bEnabled);

// Adds an effect to the end of a submix's chain. In a cooked game a submix's registration with the mixer is still queued on the audio thread at BeginPlay, and adding an effect before it runs only logs "Submix instance not found", so this waits for it (and registers the submix if nothing has)
void VAAddSubmixEffect(const UObject* WorldContext, USoundSubmix* Submix, USoundEffectSubmixPreset* Preset);
