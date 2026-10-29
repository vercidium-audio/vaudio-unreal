#pragma once

#include "CoreMinimal.h"
#include "VAStreamSource.h"
#include "VANetworkedStreamSource.generated.h"

// A stream source for audio received over the network, e.g. another player's voice chat. Behaves exactly like VAStreamSource, and exists as a distinct type so netcode can find and feed these sources: call OpenStream once, then PushAudioData with each chunk as it arrives
UCLASS(DisplayName = "VANetworkedStreamSource")
class VAUDIOUNREAL_API AVANetworkedStreamSource : public AVAStreamSource
{
	GENERATED_BODY()
};
