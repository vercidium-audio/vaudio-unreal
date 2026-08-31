#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VAudioListenerComponent.generated.h"

class AVAudioListener;

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
