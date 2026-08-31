#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "VAudioBakeCommandlet.generated.h"

UCLASS()
class UVAudioBakeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
