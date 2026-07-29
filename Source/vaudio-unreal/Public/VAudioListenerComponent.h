#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VAudioListenerComponent.generated.h"

class AVAudioListener;

// Add this component to any actor that needs a reference to an AVAudioListener. Assign
// VAudioListener explicitly where possible (e.g. a level-placed actor referencing another
// level-placed AVAudioListener). If left unset, BeginPlay falls back to searching the owner's
// attached actors for an AVAudioListener - covers the case where the listener is attached to
// this component's owner in the outliner (e.g. attached to a runtime-spawned character so it
// follows it around), since a level actor reference can't be baked into a Blueprint class's
// defaults - see UVAudioListenerComponent.cpp.
UCLASS(ClassGroup = ("Vercidium Audio"), meta = (BlueprintSpawnableComponent), DisplayName = "VA Listener Reference")
class VAUDIOUNREAL_API UVAudioListenerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVAudioListenerComponent();

	// The AVAudioListener this component points to. Leave unset to auto-resolve from the owner's
	// attached actors instead (see BeginPlay).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	AVAudioListener* VAudioListener = nullptr;

protected:
	virtual void BeginPlay() override;

private:
	// Searches the owner's attached actors for an AVAudioListener. Used as a BeginPlay fallback
	// when VAudioListener isn't explicitly assigned.
	AVAudioListener* FindListenerFromAttachedActors() const;

	void DisplayWarning(const TCHAR* fmt, ...) const;
};
