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
	// Godot's VAListener defaults. Both are hidden in the Details panel
	bHasRelativeReverb = true;
	bAffectsGroupedEAX = false;
}

// Runs once when this listener begins play, whether or not it's current. Properties are pushed by Activate instead
void AVAListener::InitializeTypeSpecific()
{
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

bool AVAListener::AttachToWorld()
{
	InitializeTypeSpecific();
	return AudioWorld->RegisterListener(this);
}

void AVAListener::DetachFromWorld()
{
	AudioWorld->UnregisterListener(this);
}

void AVAListener::SetCurrent(bool value)
{
	// Not in a world yet, e.g. set before BeginPlay. RegisterListener reads the flag when this listener joins
	if (!IsRegistered())
	{
		bCurrent = value;
		return;
	}

	if (value)
		AudioWorld->SetCurrentListener(this);
	else
		AudioWorld->ReleaseCurrentListener(this);
}

void AVAListener::MakeCurrent()
{
	SetCurrent(true);
}

bool AVAListener::Activate(VAEmitter* sharedHandle)
{
	bCurrent = true;

	if (sharedHandle)
	{
		AdoptEmitter(sharedHandle);
		UpdateVAEmitter();
		return true;
	}

	CreateEmitter();

	// Ray counts are set before targets are added
	UpdateVAEmitter();

	if (AudioWorld->AddEmitterToWorld(this))
		return true;

	DestroyUnaddedEmitter();
	bCurrent = false;
	return false;
}

void AVAListener::Deactivate()
{
	bCurrent = false;
	Emitter = nullptr;
}

void AVAListener::AddTarget(AVAEmitter* target)
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
}

void AVAListener::ApplyListenerReverb()
{
	if (!ListenerReverbPreset)
		return;

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

	// Other properties are pushed by AVAEmitter while this listener is current
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(AVAListener, bCurrent))
		SetCurrent(bCurrent);
}
#endif
