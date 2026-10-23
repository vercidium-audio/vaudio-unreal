#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "VABakeCommandlet.generated.h"

UCLASS()
class UVABakeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
