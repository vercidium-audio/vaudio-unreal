#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
class USoundSubmix;

// Sets the component's send to a reverb submix. Unlike Godot, this must be pre distance attenuation, else distant sounds contribute less to the reverb than vaudio's relative gain says. Does nothing until the component is playing
void VASetReverbSend(UAudioComponent* Component, USoundSubmix* Submix, float SendLevel);
