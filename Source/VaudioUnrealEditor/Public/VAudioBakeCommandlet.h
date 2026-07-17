#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "VAudioBakeCommandlet.generated.h"

// Run before -cook as part of packaging (see build_command.txt) so every AVAudioWorld actor's
// mesh geometry is baked and saved without relying on someone remembering to press the
// "Bake Geometry For Shipping" button in the editor.
//
//   UnrealEditor-Cmd.exe MyProject.uproject -run=VAudioBake -unattended -nopause -nosplash
UCLASS()
class UVAudioBakeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
