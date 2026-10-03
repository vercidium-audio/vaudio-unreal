#include "VAListener.h"
#include "VAWorld.h"
#include "VASource.h"
#include "VAReverbConversion.h"
#include "GameFramework/PlayerController.h"
#include "AudioMixerBlueprintLibrary.h"

extern "C" {
#include "vaudio.h"
}

#include "VAConstants.h"
#include "VALog.h"

AVAListener::AVAListener()
{
}

void AVAListener::InitializeTypeSpecific()
{
	// Set ray counts and other settings
	UpdateVAEmitter();

	// Initialise the submix
	if (ListenerReverbSubmix)
	{
		ListenerReverbPreset = NewObject<USubmixEffectReverbPreset>(this);
		UAudioMixerBlueprintLibrary::AddSubmixEffect(this, ListenerReverbSubmix, ListenerReverbPreset);

		if (ReverbRayCount == 0 || ReverbBounceCount == 0)
		{
			VA_WARN_NAMED(TEXT("Has a reverb submix assigned but does not cast reverb rays"));
		}
	}
}

void AVAListener::AddTarget(AVAEmitterBase* target)
{
	if (!vaEmitterGetOcclusionEnabled(Emitter) && !vaEmitterGetPermeationEnabled(Emitter))
	{
		// Every emitter in the world is a target, so only say it once
		if (!warnedNoTargetRays)
		{
			VA_ERROR_NAMED(TEXT("Cannot determine how muffled other sounds are. Increase its occlusion or permeation ray counts and try again."));
			warnedNoTargetRays = true;
		}

		return;
	}

	VAResult result = vaEmitterAddTarget(Emitter, target->GetVAEmitter());

	switch (result)
	{
		case VA_SUCCESS:
		case VA_ALREADY_EXISTS:
			break;

		case VA_NOT_ADDED_TO_WORLD:
			VA_ERROR_NAMED(TEXT("Failed to add target '%s' as it has not been added to the same world as this listener."), *target->GetActorNameOrLabel());
			break;

		case VA_FEATURE_DISABLED:
			VA_ERROR_NAMED(TEXT("Failed to add target '%s' as this listener casts neither occlusion nor permeation rays."), *target->GetActorNameOrLabel());
			break;

		default:
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to add target '%s'."), *target->GetActorNameOrLabel());
			break;
	}
}

void AVAListener::DeinitializeTypeSpecific()
{
	if (ListenerReverbPreset)
	{
		UAudioMixerBlueprintLibrary::RemoveSubmixEffect(this, ListenerReverbSubmix, ListenerReverbPreset);
		ListenerReverbPreset = nullptr;
	}

	Super::DeinitializeTypeSpecific();
}

void AVAListener::TickTypeSpecific(float DeltaTime)
{
	// Follow the first person player controller
	if (bAutoFollowCamera)
	{
		APlayerController* playerController = GetWorld()->GetFirstPlayerController();
		APlayerCameraManager* cameraManager = playerController ? playerController->PlayerCameraManager : nullptr;

		// GetCameraCacheTime() is 0 until the camera manager has run its first UpdateCamera().
		//  Before that, CamPos will always be (0, 0, 0), teleporting the listener to the world origin for one frame, causing filters to spike.
		if (cameraManager && cameraManager->GetCameraCacheTime() > 0.0f)
		{
			FVector CamPos = cameraManager->GetCameraLocation();
			FRotator CamRot = cameraManager->GetCameraRotation();
			vaEmitterSetPositionUnreal(Emitter, CamPos);
			SetActorLocationAndRotation(CamPos, CamRot);
		}
	}

	if (ListenerReverbPreset)
		ApplyListenerReverb();

}

void AVAListener::UpdateVAEmitter()
{
	Super::UpdateVAEmitter();

	vaEmitterSetOcclusionRayCount(Emitter, OcclusionRayCount);
	vaEmitterSetOcclusionBounceCount(Emitter, OcclusionBounceCount);
	vaEmitterSetMinimumOcclusionEnergy(Emitter, MinimumOcclusionEnergy);
	vaEmitterSetPermeationRayCount(Emitter, PermeationRayCount);
	vaEmitterSetPermeationBounceCount(Emitter, PermeationBounceCount);
	vaEmitterSetMinimumPermeationEnergy(Emitter, MinimumPermeationEnergy);
	vaEmitterSetRelativeReverbInnerThreshold(Emitter, RelativeReverbInnerThreshold);
	vaEmitterSetRelativeReverbOuterThreshold(Emitter, RelativeReverbOuterThreshold);

	vaEmitterSetHasRelativeReverb(Emitter, true);
	vaEmitterSetAffectsGroupedEAX(Emitter, false);
}

void AVAListener::ApplyListenerReverb()
{
	VAEAXReverb* EAX = vaEmitterGetEAX(Emitter);

	// Raytracing has not completed at least once yet
	if (!EAX)
		return;

	FSubmixEffectReverbSettings settings = VAEAXReverbToSubmixSettings(EAX);
	ListenerReverbPreset->SetSettings(settings);

	VAShowMessage(VAMessageKey(this, EVAMessageSlot::ListenerEAX), 0.0f, FColor::Cyan,
		FString::Printf(TEXT("[VA] Listener EAX: decayTime=%.2f gainLF=%.2f gainHF=%.2f"), EAX->decayTime, EAX->gainLF, EAX->gainHF));
}

#if WITH_EDITOR
void AVAListener::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Emitter only exists while PIE/game is running, so ignore edits when we haven't hit Play yet
	if (!Emitter)
		return;

	UpdateVAEmitter();
}
#endif
