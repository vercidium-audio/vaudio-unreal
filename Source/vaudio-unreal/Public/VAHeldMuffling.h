// Human-reviewed

#pragma once

#include "CoreMinimal.h"

extern "C" {
#include "vaudio.h"
}

// A suspended handle is taken out of the listener's targets, so its muffling is held here instead.
// It's still held after it's un-suspended, until the listener has raytraced it again, so it stays ready to play
struct FVAHeldMuffling
{
	VALowPassFilter Filter = { 1.0f, 1.0f };
	bool bHasFilter = false;

	// Not a listener target while this is set
	bool bSuspended = false;

	void Reset()
	{ 
		*this = FVAHeldMuffling()
	}
};
