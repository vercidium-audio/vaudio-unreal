#include "VAudioListener.h"
#include "VAudioWorld.h"
#include "VAudioSource.h"
#include "VADebugMessageKeys.h"
#include "VAudioReverbConversion.h"
#include "GameFramework/PlayerController.h"
#include "AudioMixerBlueprintLibrary.h"

extern "C" {
#include "vaudio.h"
}

#include "VAConstants.h"
#include "VARawLog.h"

AVAudioListener::AVAudioListener()
{
}

void AVAudioListener::InitializeTypeSpecific()
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
			DisplayWarning(TEXT("[VA] Listener '%s' has a reverb submix assigned but does not cast reverb rays"), *GetActorNameOrLabel());
		}
	}


	// Add targets
	for (AVAudioEmitterBase* Target : TargetEmitters)
	{
		if (RegisteredTargets.Contains(Target))
		{
			DisplayWarning(TEXT("[VA] Listener '%s' has a duplicate target: %s"), *GetActorNameOrLabel(), *Target->GetActorNameOrLabel());
			continue;
		}

		// User added a null target, ignore it
		if (!Target)
		{
			DisplayWarning(TEXT("[VA] Listener '%s' has an empty target, please remove it"), *GetActorNameOrLabel());
			continue;
		}

		// Actor init order isn't guaranteed so just initialise the emitter here
		Target->TryInitializeEmitter();
		VAEmitter* vaEmitter = Target->GetVAEmitter();

		VAResult result = vaEmitterAddTarget(Emitter, Target->GetVAEmitter());

		if (result == VA_FEATURE_DISABLED)
		{
			DisplayWarning(TEXT("[VA] Listener '%s' cannot have targets as it does not cast occlusion or permeation rays"), *GetActorNameOrLabel());
		}

		RegisteredTargets.Add(Target);
	}
}

void AVAudioListener::TickTypeSpecific(float DeltaTime)
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
			vaEmitterSetPositionUnreal(Emitter, CamPos);
			SetActorLocation(CamPos);
		}
	}

	ApplyListenerReverb();


	// Update filters for each target VAudioSource
	for (AVAudioEmitterBase* Target : TargetEmitters)
	{
		// User added a null target
		if (!Target)
			continue;

		VAEmitter* vaEmitter = Target->GetVAEmitter();

		// Wait till we've raytraced the target
		if (!vaEmitterHasRaytracedTarget(Emitter, vaEmitter))
			continue;

		VALowPassFilter* lowPassFilter = vaEmitterGetTargetFilter(Emitter, vaEmitter);

		if (AVAudioSource* Source = Cast<AVAudioSource>(Target))
			Source->ApplySourceFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);
		else
		{
			// TODO - I think continuous / relative emitters update their own filter based on GetMufflingResult()
		}
	}
}

void AVAudioListener::UpdateVAEmitter()
{
	Super::UpdateVAEmitter();

	vaEmitterSetOcclusionRayCount(Emitter, OcclusionRayCount);
	vaEmitterSetOcclusionBounceCount(Emitter, OcclusionBounceCount);
	vaEmitterSetPermeationRayCount(Emitter, PermeationRayCount);
	vaEmitterSetPermeationBounceCount(Emitter, PermeationBounceCount);
	vaEmitterSetMinimumPermeationEnergy(Emitter, MinimumPermeationEnergy);
	vaEmitterSetRelativeReverbInnerThreshold(Emitter, RelativeReverbInnerThreshold);
	vaEmitterSetRelativeReverbOuterThreshold(Emitter, RelativeReverbOuterThreshold);

	vaEmitterSetHasRelativeReverb(Emitter, true);
	vaEmitterSetAffectsGroupedEAX(Emitter, false);
}

void AVAudioListener::ApplyListenerReverb()
{
	// This is null if no submix is assigned
	if (!ListenerReverbPreset)
		return;

	VAEAXReverb* EAX = vaEmitterGetEAX(Emitter);

	// Raytracing has not completed at least once yet
	if (!EAX)
		return;

	FSubmixEffectReverbSettings settings = VAEAXReverbToSubmixSettings(EAX);
	ListenerReverbPreset->SetSettings(settings);

	GEngine->AddOnScreenDebugMessage(VAListenerEAXMessage, 0.0f, FColor::Cyan,
		FString::Printf(TEXT("[VA] Listener EAX: decayTime=%.2f gainLF=%.2f gainHF=%.2f"), EAX->decayTime, EAX->gainLF, EAX->gainHF));
}

#if WITH_EDITOR
void AVAudioListener::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Emitter only exists while PIE/game is running, so ignore edits when we haven't hit Play yet
	if (!Emitter)
		return;

	UpdateVAEmitter();
}
#endif
