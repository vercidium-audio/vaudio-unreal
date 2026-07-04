#include "VAudioEmitter.h"
#include "VAudioWorld.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "AudioMixerBlueprintLibrary.h"
#include "ActiveSound.h"
#include "AudioDevice.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectFilter.h"

extern "C" {
#include "vaudio.h"
}

AVAudioEmitter::AVAudioEmitter()
{
	PrimaryActorTick.bCanEverTick = true;

	UBillboardComponent* Root = CreateDefaultSubobject<UBillboardComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AVAudioEmitter::BeginPlay()
{
	Super::BeginPlay();

	if (!AudioWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("VAudioEmitter '%s': AudioWorld is not assigned â€” emitter will not raytrace."), *GetActorLabel());
		return;
	}

	VAWorld* VAW = AudioWorld->GetVAWorld();
	if (!VAW) return;

	Emitter = vaEmitterCreate();

	FVector Pos = GetActorLocation();
	vaEmitterSetPosition(Emitter, vaVectorCreate((float)Pos.X, (float)Pos.Z, -(float)Pos.Y));

	vaEmitterSetReverbRayCount(Emitter, ReverbRayCount);
	vaEmitterSetReverbBounceCount(Emitter, ReverbBounceCount);
	vaEmitterSetReverbEnergyCap(Emitter, ReverbEnergyCap);
	vaEmitterSetMaxEchogramTime(Emitter, MaxEchogramTime);
	vaEmitterSetEchogramGranularity(Emitter, EchogramGranularity);

	vaEmitterSetOcclusionRayCount(Emitter, OcclusionRayCount);
	vaEmitterSetOcclusionBounceCount(Emitter, OcclusionBounceCount);
	vaEmitterSetOcclusionEnergyCap(Emitter, OcclusionEnergyCap);

	vaEmitterSetPermeationRayCount(Emitter, PermeationRayCount);
	vaEmitterSetPermeationBounceCount(Emitter, PermeationBounceCount);
	vaEmitterSetPermeationEnergyCap(Emitter, PermeationEnergyCap);

	vaEmitterSetRefreshRayCount(Emitter, RefreshRayCount);
	vaEmitterSetRefreshDistanceThreshold(Emitter, RefreshDistanceThreshold);

	vaEmitterSetAmbientOcclusionRayCount(Emitter, AmbientOcclusionRayCount);
	vaEmitterSetAmbientOcclusionBounceCount(Emitter, AmbientOcclusionBounceCount);
	vaEmitterSetAmbientOcclusionEnergyCap(Emitter, AmbientOcclusionEnergyCap);
	vaEmitterSetAmbientPermeationRayCount(Emitter, AmbientPermeationRayCount);
	vaEmitterSetAmbientPermeationBounceCount(Emitter, AmbientPermeationBounceCount);
	vaEmitterSetAmbientPermeationEnergyCap(Emitter, AmbientPermeationEnergyCap);

	bool bSDKAffectsGrouped = bAffectsGroupedEAX && !bIsMainListener;
	vaEmitterSetAffectsGroupedEAX(Emitter, bSDKAffectsGrouped);
	vaEmitterSetHasRelativeReverb(Emitter, bIsMainListener);
	UE_LOG(LogTemp, Log, TEXT("VA Emitter '%s': affectsGroupedEAX=%d (bAffectsGroupedEAX=%d bIsMainListener=%d)"),
		*GetActorLabel(), (int32)bSDKAffectsGrouped, (int32)bAffectsGroupedEAX, (int32)bIsMainListener);

	if (bIsMainListener)
	{
		if (ListenerReverbSubmix)
		{
			ListenerReverbPreset = NewObject<USubmixEffectReverbPreset>(this);
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, ListenerReverbSubmix, ListenerReverbPreset);
		}

		if (AmbientSound)
		{
			AmbientAudioComponent = UGameplayStatics::SpawnSound2D(GetWorld(), AmbientSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);
			if (AmbientAudioComponent)
			{
				AmbientAudioComponent->SetLowPassFilterEnabled(true);
				if (bAmbientThroughReverb && ListenerReverbSubmix)
					AmbientAudioComponent->SetSubmixSend(ListenerReverbSubmix, 1.0f);
			}
		}
		// Target wiring is deferred to the first Tick â€” target emitters may not have
		// called vaEmitterCreate yet if their BeginPlay runs after ours.
	}
	else if (SourceSound)
	{
		// Build the source effect chain (LPF only on the dry path; reverb submix taps the pre-effect signal).
		SourceLPFPreset = NewObject<USourceEffectFilterPreset>(this);
		FSourceEffectFilterSettings LPFSettings;
		LPFSettings.FilterCircuit    = ESourceEffectFilterCircuit::StateVariable;
		LPFSettings.FilterType       = ESourceEffectFilterType::LowPass;
		LPFSettings.CutoffFrequency  = 20000.0f;
		LPFSettings.FilterQ          = 0.707f;
		SourceLPFPreset->SetSettings(LPFSettings);

		SourceEffectChain = NewObject<USoundEffectSourcePresetChain>(this);
		FSourceEffectChainEntry ChainEntry;
		ChainEntry.Preset = SourceLPFPreset;
		ChainEntry.bBypass = false;
		SourceEffectChain->Chain.Add(ChainEntry);

		SourceAudioComponent = UGameplayStatics::SpawnSoundAtLocation(
			GetWorld(), SourceSound, GetActorLocation(),
			FRotator::ZeroRotator, 1.0f, 1.0f, 0.0f, nullptr, nullptr, bLooping);
		if (SourceAudioComponent)
		{
			SourceAudioComponent->SetSourceEffectChain(SourceEffectChain);

			// Submix assignment is deferred to Tick once the SDK assigns a groupedEAXIndex.
		}
	}

	vaWorldAddEmitter(VAW, Emitter);
	AudioWorld->RegisterEmitter(this);
}

void AVAudioEmitter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (AmbientAudioComponent)
	{
		AmbientAudioComponent->Stop();
		AmbientAudioComponent = nullptr;
	}

	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;
	}

	CurrentGroupedEAXIndex = -1;

	if (AudioWorld)
	{
		AudioWorld->UnregisterEmitter(this);

		VAWorld* VAW = AudioWorld->GetVAWorld();
		if (VAW && Emitter)
		{
			vaWorldRemoveEmitter(VAW, Emitter);
		}
	}

	if (Emitter)
	{
		vaEmitterDestroy(Emitter);
		Emitter = nullptr;
	}
}

void AVAudioEmitter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!Emitter) return;

	if (bIsMainListener && !bTargetsRegistered)
	{
		bTargetsRegistered = true;
		for (AVAudioEmitter* Target : TargetEmitters)
		{
			if (Target && Target->GetVAEmitter())
				vaEmitterAddTarget(Emitter, Target->GetVAEmitter());
			else
				UE_LOG(LogTemp, Warning, TEXT("VAudioEmitter '%s': target '%s' has no VA emitter â€” skipping"),
					*GetActorLabel(), Target ? *Target->GetActorLabel() : TEXT("null"));
		}
	}

	if (bIsMainListener)
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC && PC->PlayerCameraManager)
		{
			FVector CamPos = PC->PlayerCameraManager->GetCameraLocation();
			vaEmitterSetPosition(Emitter, vaVectorCreate((float)CamPos.X, (float)CamPos.Z, -(float)CamPos.Y));
			SetActorLocation(CamPos);
		}
	}
	else
	{
		FVector Pos = GetActorLocation();
		vaEmitterSetPosition(Emitter, vaVectorCreate((float)Pos.X, (float)Pos.Z, -(float)Pos.Y));
	}

	if (!bIsMainListener && bAffectsGroupedEAX)
	{
		UpdateSourceSubmix();

		if (GEngine)
		{
			int32 Idx = vaEmitterGetGroupedEAXIndex(Emitter);
			USoundSubmix* Submix = (AudioWorld && Idx >= 0) ? AudioWorld->GetGroupedEAXSubmix(Idx) : nullptr;
			GEngine->AddOnScreenDebugMessage((uint64)this + 100, 0.0f, FColor::Cyan,
				FString::Printf(TEXT("VA Source '%s': groupedEAXIndex=%d submix=%s sourceComp=%s"),
					*GetActorLabel(), Idx,
					Submix          ? TEXT("valid") : TEXT("null"),
					SourceAudioComponent ? TEXT("valid") : TEXT("null")));
		}
	}

	if (bIsMainListener)
	{
		ApplyListenerReverb();
		ApplyGroupedEAXReverb();
		ApplyAmbientFilter();

		for (AVAudioEmitter* Target : TargetEmitters)
		{
			if (!Target || !Target->GetVAEmitter()) continue;
			if (!vaEmitterHasRaytracedTarget(Emitter, Target->GetVAEmitter())) continue;

			VALowPassFilter* F = vaEmitterGetTargetFilter(Emitter, Target->GetVAEmitter());
			if (F)
			{
				if (GEngine)
					GEngine->AddOnScreenDebugMessage((uint64)Target, 0.0f, FColor::Orange,
						FString::Printf(TEXT("VA Source '%s' LPF: gainLF=%.3f  gainHF=%.3f"), *Target->GetActorLabel(), F->gainLF, F->gainHF));
				Target->ApplySourceFilter(F->gainLF, F->gainHF);
			}
		}
	}
}

void AVAudioEmitter::ApplyListenerReverb()
{
	if (!ListenerReverbPreset) return;

	VAEAXReverb* EAX = vaEmitterGetEAX(Emitter);
	if (!EAX) return;

	FSubmixEffectReverbSettings S;
	S.DecayTime           = FMath::Clamp(EAX->decayTime,           0.1f,  20.0f);
	S.DecayHFRatio        = FMath::Clamp(EAX->decayHFRatio,        0.1f,   2.0f);
	S.Density             = FMath::Clamp(EAX->density,             0.0f,   1.0f);
	S.Diffusion           = FMath::Clamp(EAX->diffusion,           0.0f,   1.0f);
	S.Gain                = FMath::Clamp(EAX->gain,                0.0f,   1.0f);
	S.GainHF              = FMath::Clamp(EAX->gainHF,              0.0f,   1.0f);
	S.ReflectionsGain     = FMath::Clamp(EAX->reflectionsGain,     0.0f,   3.16f);
	S.ReflectionsDelay    = FMath::Clamp(EAX->reflectionsDelay,    0.0f,   0.3f);
	S.LateGain            = FMath::Clamp(EAX->lateReverbGain,      0.0f,  10.0f);
	S.LateDelay           = FMath::Clamp(EAX->lateReverbDelay,     0.0f,   0.1f);
	S.AirAbsorptionGainHF = FMath::Clamp(EAX->airAbsorptionGainHF, 0.0f,   1.0f);
	S.WetLevel            = FMath::Clamp(EAX->returnedPercent,    0.0f,   1.0f);
	S.DryLevel            = 0.0f;
	ListenerReverbPreset->SetSettings(S);
}

void AVAudioEmitter::ApplyAmbientFilter()
{
	VALowPassFilter* F = vaEmitterGetAmbientFilter(Emitter);
	if (!F || !AmbientAudioComponent) return;

	const float MinFreq = 200.0f;
	const float MaxFreq = 20000.0f;
	AmbientAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MinFreq, MaxFreq, FMath::Clamp(F->gainHF, 0.0f, 1.0f)));
	AmbientAudioComponent->SetVolumeMultiplier(FMath::Clamp(F->gainLF, 0.0f, 1.0f));
}

#if WITH_EDITOR
void AVAudioEmitter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!Emitter) return;

	vaEmitterSetReverbRayCount(Emitter, ReverbRayCount);
	vaEmitterSetReverbBounceCount(Emitter, ReverbBounceCount);
	vaEmitterSetReverbEnergyCap(Emitter, ReverbEnergyCap);
	vaEmitterSetMaxEchogramTime(Emitter, MaxEchogramTime);
	vaEmitterSetEchogramGranularity(Emitter, EchogramGranularity);

	vaEmitterSetOcclusionRayCount(Emitter, OcclusionRayCount);
	vaEmitterSetOcclusionBounceCount(Emitter, OcclusionBounceCount);
	vaEmitterSetOcclusionEnergyCap(Emitter, OcclusionEnergyCap);

	vaEmitterSetPermeationRayCount(Emitter, PermeationRayCount);
	vaEmitterSetPermeationBounceCount(Emitter, PermeationBounceCount);
	vaEmitterSetPermeationEnergyCap(Emitter, PermeationEnergyCap);

	vaEmitterSetRefreshRayCount(Emitter, RefreshRayCount);
	vaEmitterSetRefreshDistanceThreshold(Emitter, RefreshDistanceThreshold);

	vaEmitterSetAmbientOcclusionRayCount(Emitter, AmbientOcclusionRayCount);
	vaEmitterSetAmbientOcclusionBounceCount(Emitter, AmbientOcclusionBounceCount);
	vaEmitterSetAmbientOcclusionEnergyCap(Emitter, AmbientOcclusionEnergyCap);
	vaEmitterSetAmbientPermeationRayCount(Emitter, AmbientPermeationRayCount);
	vaEmitterSetAmbientPermeationBounceCount(Emitter, AmbientPermeationBounceCount);
	vaEmitterSetAmbientPermeationEnergyCap(Emitter, AmbientPermeationEnergyCap);

	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX && !bIsMainListener);
}
#endif

static void SetAudioComponentDryOutputEnabled(UAudioComponent* Comp, bool bEnabled)
{
	if (!Comp) return;

	FAudioDevice* AudioDevice = Comp->GetAudioDevice();
	if (!AudioDevice) return;

	uint64 AudioComponentID = Comp->GetAudioComponentID();
	AudioDevice->SendCommandToActiveSounds(AudioComponentID, [bEnabled](FActiveSound& ActiveSound)
	{
		// Kill/restore the master submix output without touching submix send routing.
		// This lets reverb submix sends stay alive while silencing the dry signal.
		ActiveSound.bHasActiveMainSubmixOutputOverride = true;
		ActiveSound.bEnableMainSubmixOutputOverride = bEnabled;
	});
}

void AVAudioEmitter::SetDryOutputEnabled(bool bEnabled)
{
	if (bEnabled == bCurrentDryEnabled) return;
	bCurrentDryEnabled = bEnabled;
	SetAudioComponentDryOutputEnabled(SourceAudioComponent, bEnabled);
	SetAudioComponentDryOutputEnabled(AmbientAudioComponent, bEnabled);
}

void AVAudioEmitter::ApplySourceFilter(float GainLF, float GainHF)
{
	if (!SourceAudioComponent || !SourceLPFPreset) return;

	const float MinFreq = 200.0f;
	const float MaxFreq = 20000.0f;

	FSourceEffectFilterSettings S;
	S.FilterCircuit   = ESourceEffectFilterCircuit::StateVariable;
	S.FilterType      = ESourceEffectFilterType::LowPass;
	S.CutoffFrequency = FMath::Lerp(MinFreq, MaxFreq, FMath::Clamp(GainHF, 0.0f, 1.0f));
	S.FilterQ         = 0.707f;
	SourceLPFPreset->SetSettings(S);

	SourceAudioComponent->SetVolumeMultiplier(FMath::Clamp(GainLF, 0.0f, 1.0f));
}

void AVAudioEmitter::UpdateSourceSubmix()
{
	if (!SourceAudioComponent || !AudioWorld || !Emitter) return;

	int32 NewIndex = vaEmitterGetGroupedEAXIndex(Emitter);

	if (NewIndex != CurrentGroupedEAXIndex)
	{
		UE_LOG(LogTemp, Log, TEXT("VA UpdateSourceSubmix '%s': groupedEAXIndex %d -> %d"),
			*GetActorLabel(), CurrentGroupedEAXIndex, NewIndex);

		if (CurrentGroupedEAXIndex >= 0)
		{
			USoundSubmix* OldSubmix = AudioWorld->GetGroupedEAXSubmix(CurrentGroupedEAXIndex);
			UE_LOG(LogTemp, Log, TEXT("VA   removing send from submix[%d]: %s"),
				CurrentGroupedEAXIndex, OldSubmix ? TEXT("valid") : TEXT("null"));
			if (OldSubmix)
				SourceAudioComponent->SetSubmixSend(OldSubmix, 0.0f);
		}

		CurrentGroupedEAXIndex = NewIndex;
	}

	if (CurrentGroupedEAXIndex < 0) return;

	USoundSubmix* Submix = AudioWorld->GetGroupedEAXSubmix(CurrentGroupedEAXIndex);
	if (!Submix) return;

	// Default send level â€” overridden below if the listener has relative reverb data
	float SendLevel = 1.0f;
	float DebugGain = -1.0f;
	bool bHasRelative = false;

	VAWorld* VAW = AudioWorld->GetVAWorld();
	AVAudioEmitter* Listener = AudioWorld->GetMainListener();
	if (VAW && Listener && vaWorldGetReverbCalculated(VAW))
	{
		const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(VAW);
		if (GroupedEAX && GroupedEAX[CurrentGroupedEAXIndex])
		{
			const VAEAXReverb* EAX = GroupedEAX[CurrentGroupedEAXIndex];
			VAEmitter* ListenerVA = Listener->GetVAEmitter();

			float* Gain = vaEAXReverbGetRelativeGain(EAX, ListenerVA);
			if (Gain)
			{
				DebugGain = *Gain;
				SendLevel = FMath::Clamp(*Gain, 0.0f, 1.0f);
				bHasRelative = true;
			}
		}
	}

	SourceAudioComponent->SetSubmixSend(Submix, SendLevel);

	if (GEngine)
	{
		FString RelStr = bHasRelative
			? FString::Printf(TEXT("gain=%.3f send=%.3f"), DebugGain, SendLevel)
			: TEXT("no relative data (listener not ready?)");
		GEngine->AddOnScreenDebugMessage((uint64)this + 200, 0.0f, FColor::Magenta,
			FString::Printf(TEXT("VA Relative '%s': %s"), *GetActorLabel(), *RelStr));
	}
}

void AVAudioEmitter::ApplyGroupedEAXReverb()
{
	if (!AudioWorld) return;

	VAWorld* VAW = AudioWorld->GetVAWorld();
	if (!VAW) return;

	bool bReverbReady = vaWorldGetReverbCalculated(VAW);
	const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(VAW);
	int32 Count = vaWorldGetGroupedEAXCount(VAW);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(3000, 0.0f, FColor::White,
			FString::Printf(TEXT("VA GroupedEAX: reverbReady=%d count=%d presetSlots=%d"),
				(int32)bReverbReady, Count, AudioWorld->GetGroupedEAXPresetCount()));
	}

	if (!bReverbReady || !GroupedEAX) return;

	for (int32 i = 0; i < Count; ++i)
	{
		USubmixEffectReverbPreset* Preset = AudioWorld->GetGroupedEAXPreset(i);
		const VAEAXReverb* EAX = GroupedEAX[i];

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(3001 + i, 0.0f, FColor::White,
				FString::Printf(TEXT("VA GroupedEAX[%d]: preset=%s eax=%s decayTime=%.3f wetLevel=%.3f gain=%.3f"),
					i,
					Preset ? TEXT("valid") : TEXT("null"),
					EAX    ? TEXT("valid") : TEXT("null"),
					EAX    ? EAX->decayTime        : 0.0f,
					EAX    ? EAX->returnedPercent : 0.0f,
					EAX    ? EAX->gain             : 0.0f));
		}

		if (!Preset || !EAX) continue;

		FSubmixEffectReverbSettings S;
		S.DecayTime           = FMath::Clamp(EAX->decayTime,           0.1f,  20.0f);
		S.DecayHFRatio        = FMath::Clamp(EAX->decayHFRatio,        0.1f,   2.0f);
		S.Density             = FMath::Clamp(EAX->density,             0.0f,   1.0f);
		S.Diffusion           = FMath::Clamp(EAX->diffusion,           0.0f,   1.0f);
		S.Gain                = FMath::Clamp(EAX->gain,                0.0f,   1.0f);
		S.GainHF              = FMath::Clamp(EAX->gainHF,              0.0f,   1.0f);
		S.ReflectionsGain     = FMath::Clamp(EAX->reflectionsGain,     0.0f,   3.16f);
		S.ReflectionsDelay    = FMath::Clamp(EAX->reflectionsDelay,    0.0f,   0.3f);
		S.LateGain            = FMath::Clamp(EAX->lateReverbGain,      0.0f,  10.0f);
		S.LateDelay           = FMath::Clamp(EAX->lateReverbDelay,     0.0f,   0.1f);
		S.AirAbsorptionGainHF = FMath::Clamp(EAX->airAbsorptionGainHF, 0.0f,   1.0f);
		S.WetLevel            = FMath::Clamp(EAX->returnedPercent,    0.0f,   1.0f);
		S.DryLevel            = 0.0f;
		Preset->SetSettings(S);
	}
}
