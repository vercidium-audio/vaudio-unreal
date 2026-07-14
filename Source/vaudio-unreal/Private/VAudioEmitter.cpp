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

#include "VaRawLog.h"

const float MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
const float MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;

AVAudioEmitter::AVAudioEmitter()
{
	PrimaryActorTick.bCanEverTick = true;

	UBillboardComponent* Root = CreateDefaultSubobject<UBillboardComponent>(TEXT("Root"));
	SetRootComponent(Root);

	VaRawLog(L"VAudioEmitter.cpp: %s: Constructed", *GetName());
}

void AVAudioEmitter::BeginPlay()
{
	Super::BeginPlay();

	if (!AudioWorld)
	{
		VaRawLog(L"VAudioEmitter.cpp: BeginPlay(): %s: AudioWorld is null", *GetName());
		return;
	}

	// AVAudioWorld may not have run its own BeginPlay yet (actor BeginPlay order is
	// not guaranteed), in which case GetVAWorld() is still null here. TryInitializeEmitter()
	// is safe to call repeatedly - it no-ops if Emitter is already set - so Tick() retries
	// it every frame until AudioWorld's VAWorld becomes valid.
	TryInitializeEmitter();
}

bool AVAudioEmitter::TryInitializeEmitter()
{
	// Already initialised
	if (Emitter)
		return true;

	if (!AudioWorld)
	{
		VaRawLog(L"VAudioEmitter.cpp: TryInitializeEmitter(): %s: AudioWorld is null", *GetName());
		return false;
	}

	VAWorld* vaWorld = AudioWorld->GetVAWorld();

	if (!vaWorld)
	{
		VaRawLog(L"VAudioEmitter.cpp: TryInitializeEmitter(): %s: vaWorld is null", *GetName());
		return false;
	}

	Emitter = vaEmitterCreate();
	vaEmitterSetLogCallback(Emitter, &VaSdkLogCallback);
	vaEmitterSetLogErrorCallback(Emitter, &VaSdkLogCallback);

	FVector Pos = GetActorLocation();
	vaEmitterSetPosition(Emitter, vaVectorCreate((float)Pos.X, (float)Pos.Y, (float)Pos.Z));

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

	vaEmitterSetAmbientOcclusionRayCount(Emitter, AmbientOcclusionRayCount);
	vaEmitterSetAmbientOcclusionBounceCount(Emitter, AmbientOcclusionBounceCount);
	vaEmitterSetAmbientOcclusionEnergyCap(Emitter, AmbientOcclusionEnergyCap);
	vaEmitterSetAmbientPermeationRayCount(Emitter, AmbientPermeationRayCount);
	vaEmitterSetAmbientPermeationBounceCount(Emitter, AmbientPermeationBounceCount);
	vaEmitterSetAmbientPermeationEnergyCap(Emitter, AmbientPermeationEnergyCap);

	vaEmitterSetRefreshRayCount(Emitter, RefreshRayCount);
	vaEmitterSetRefreshDistanceThreshold(Emitter, RefreshDistanceThreshold);

	vaEmitterSetVisualisationRayCount(Emitter, VisualisationRayCount);
	vaEmitterSetVisualisationBounceCount(Emitter, VisualisationBounceCount);
	vaEmitterSetVisualisationUpdateFrequency(Emitter, VisualisationUpdateFrequency);

	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetType(Emitter, EmitterType);
	vaEmitterSetClampPosition(Emitter, bClampPosition);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);
	vaEmitterSetReservedEmitterCount(Emitter, ReservedEmitterCount);
	vaEmitterSetMinimumPermeationEnergy(Emitter, MinimumPermeationEnergy);
	vaEmitterSetRelativeReverbInnerThreshold(Emitter, RelativeReverbInnerThreshold);
	vaEmitterSetRelativeReverbOuterThreshold(Emitter, RelativeReverbOuterThreshold);

	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX);
	vaEmitterSetHasRelativeReverb(Emitter, bIsMainListener);

	if (bIsMainListener)
	{
		if (ListenerReverbSubmix)
		{
			ListenerReverbPreset = NewObject<USubmixEffectReverbPreset>(this);
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, ListenerReverbSubmix, ListenerReverbPreset);

			VaRawLog(L"VAudioEmitter.cpp: TryInitializeEmitter(): %s: Listener submix added", *GetName());
		}

		if (AmbientSound)
		{
			AmbientAudioComponent = UGameplayStatics::SpawnSound2D(GetWorld(), AmbientSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);

			if (AmbientAudioComponent)
			{
				AmbientAudioComponent->SetLowPassFilterEnabled(true);

				if (bAmbientThroughReverb && ListenerReverbSubmix)
				{
					AmbientAudioComponent->SetSubmixSend(ListenerReverbSubmix, 1.0f);
					VaRawLog(L"VAudioEmitter.cpp: TryInitializeEmitter(): %s: Ambient sound is sent through listener reverb submix", *GetName());
				}
			}
			else
			{
				VaRawLog(L"VAudioEmitter.cpp: TryInitializeEmitter(): %s: Failed to play ambient sound", *GetName());
			}
		}

		// Target wiring is deferred to the first Tick as target emitters may not have
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
		LPFSettings.FilterQ          = 0.707f; // TODO - what is resonance?
		SourceLPFPreset->SetSettings(LPFSettings);

		SourceEffectChain = NewObject<USoundEffectSourcePresetChain>(this);
		FSourceEffectChainEntry ChainEntry;
		ChainEntry.Preset = SourceLPFPreset;
		ChainEntry.bBypass = false;
		SourceEffectChain->Chain.Add(ChainEntry);

		// TODO - wait for raytracing to complete before playing the sound (else it starts clear then is instantly muffled)
		SourceAudioComponent = UGameplayStatics::SpawnSoundAtLocation(GetWorld(), SourceSound, GetActorLocation(), FRotator::ZeroRotator, 1.0f, 1.0f, 0.0f, nullptr, nullptr, bLooping);

		if (SourceAudioComponent)
		{
			SourceAudioComponent->SetSourceEffectChain(SourceEffectChain);

			// Submix assignment is deferred to Tick once the SDK assigns a groupedEAXIndex.
			VaRawLog(L"VAudioEmitter.cpp: TryInitializeEmitter(): %s: Low pass filter created and sound played", *GetName());
		}
		else
		{
			VaRawLog(L"VAudioEmitter.cpp: TryInitializeEmitter(): %s: Failed to play sound", *GetName());
		}
	}
	else
	{
		VaRawLog(L"VAudioEmitter.cpp: TryInitializeEmitter(): %s: Emitter is neither main listener nor has a sound", *GetName());
	}

	VAResult result = vaWorldAddEmitter(vaWorld, Emitter);

	// TODO - assert result is success. If not, add a warning message in the editor

	AudioWorld->RegisterEmitter(this);

	VaRawLog(L"VAudioEmitter.cpp: TryInitializeEmitter(): %s: Complete. vaWorldAddEmitter() returned %d", *GetName(), result);
	return true;
}

void AVAudioEmitter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (AmbientAudioComponent)
	{
		AmbientAudioComponent->Stop();
		AmbientAudioComponent = nullptr;

		VaRawLog(L"VAudioEmitter.cpp: EndPlay(): %s: Stopped ambient sound", *GetName());
	}

	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;

		VaRawLog(L"VAudioEmitter.cpp: EndPlay(): %s: Stopped source sound", *GetName());
	}

	// TODO - also set submix send gain to 0.0f? If not needed, all good
	CurrentGroupedEAXIndex = -1;

	if (AudioWorld)
	{
		AudioWorld->UnregisterEmitter(this);

		VAWorld* vaWorld = AudioWorld->GetVAWorld();

		if (!vaWorld)
		{
			// TODO - too much null propagation! Why is vaWorld null here?
			VaRawLog(L"VAudioEmitter.cpp: EndPlay(): %s: vaWorld is null.", *GetName());
		}
		else if (!Emitter)
		{
			// TODO - too much null propagation! Why is Emitter null here?
			VaRawLog(L"VAudioEmitter.cpp: EndPlay(): %s: Can't remove emitter from world as Emitter is null.", *GetName());
		}
		else
		{
			vaWorldRemoveEmitter(vaWorld, Emitter);
			VaRawLog(L"VAudioEmitter.cpp: EndPlay(): %s: Emitter successfully removed from vaWorld.", *GetName());
		}
	}

	if (Emitter)
	{
		vaEmitterDestroy(Emitter);
		Emitter = nullptr;
	}
	else
	{
		// TODO - too much null propagation! Why is Emitter null here?
		VaRawLog(L"VAudioEmitter.cpp: EndPlay(): %s: Can't destroy emitter as Emitter is null.", *GetName());
	}
}

void AVAudioEmitter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!Emitter)
	{
		VaRawLog(L"Emitter is null");

		if (!TryInitializeEmitter())
		{
			VaRawLog(L"Emitter init failed");

			static float TimeSinceLastNullLog = 0.0f;
			TimeSinceLastNullLog += DeltaTime;
			if (TimeSinceLastNullLog >= 2.0f)
			{
				TimeSinceLastNullLog = 0.0f;
				UE_LOG(LogTemp, Warning, TEXT("VA DEBUG: '%s' Tick() - Emitter still NULL, waiting on AudioWorld"), *GetName());
			}

			return;
		}

		VaRawLog(L"Emitter init succeeded");
	}

	if (bIsMainListener && !bTargetsRegistered)
	{
		bool bAllTargetsReady = true;
		for (AVAudioEmitter* Target : TargetEmitters)
		{
			if (RegisteredTargets.Contains(Target))
				continue;

			if (Target && Target->GetVAEmitter())
			{
				vaEmitterAddTarget(Emitter, Target->GetVAEmitter());
				RegisteredTargets.Add(Target);
			}
			else
			{
				// Target's BeginPlay/first Tick may not have run yet (actor init order
				// isn't guaranteed). Don't latch bTargetsRegistered until every target
				// has a VA emitter, otherwise stragglers are silently dropped forever.
				bAllTargetsReady = false;
				UE_LOG(LogTemp, Warning, TEXT("VAudioEmitter '%s': target '%s' has no VA emitter yet - will retry"),
					*GetName(), Target ? *Target->GetName() : TEXT("null"));
			}
		}
		bTargetsRegistered = bAllTargetsReady;
	}

	if (bIsMainListener)
	{
		// TODO - let the user set the main listener position manually via a script or blueprint or something
		APlayerController* PC = GetWorld()->GetFirstPlayerController();

		if (PC && PC->PlayerCameraManager)
		{
			FVector CamPos = PC->PlayerCameraManager->GetCameraLocation();
			vaEmitterSetPosition(Emitter, vaVectorCreate((float)CamPos.X, (float)CamPos.Y, (float)CamPos.Z));
			SetActorLocation(CamPos);
		}
	}
	else
	{
		FVector Pos = GetActorLocation();
		vaEmitterSetPosition(Emitter, vaVectorCreate((float)Pos.X, (float)Pos.Y, (float)Pos.Z));
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
					*GetName(), Idx,
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
			if (!Target || !Target->GetVAEmitter())
				continue;

			if (!vaEmitterHasRaytracedTarget(Emitter, Target->GetVAEmitter()))
				continue;

			VALowPassFilter* lowPassFilter = vaEmitterGetTargetFilter(Emitter, Target->GetVAEmitter());

			// TODO - assert here, low pass filter should be defined if vaEmitterHasRaytracedTarget above returned true
			if (!lowPassFilter)
				continue;

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage((uint64)Target, 0.0f, FColor::Orange, FString::Printf(TEXT("VA Source '%s' LPF: gainLF=%.3f  gainHF=%.3f"), *Target->GetName(), lowPassFilter->gainLF, lowPassFilter->gainHF));
			}

			VaRawLog(L"VA Source '%s' LPF: gainLF=%.3f  gainHF=%.3f", *Target->GetName(), lowPassFilter->gainLF, lowPassFilter->gainHF);

			Target->ApplySourceFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);
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

	vaEmitterSetVisualisationRayCount(Emitter, VisualisationRayCount);
	vaEmitterSetVisualisationBounceCount(Emitter, VisualisationBounceCount);
	vaEmitterSetVisualisationUpdateFrequency(Emitter, VisualisationUpdateFrequency);

	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetType(Emitter, EmitterType);
	vaEmitterSetClampPosition(Emitter, bClampPosition);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);
	vaEmitterSetReservedEmitterCount(Emitter, ReservedEmitterCount);
	vaEmitterSetMinimumPermeationEnergy(Emitter, MinimumPermeationEnergy);
	vaEmitterSetRelativeReverbInnerThreshold(Emitter, RelativeReverbInnerThreshold);
	vaEmitterSetRelativeReverbOuterThreshold(Emitter, RelativeReverbOuterThreshold);

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
	// TODO - assert GainLF and GainHF is in range 0.0f to 1.0f (inclusive)
	
	// TODO - why could these be null? There is too much null propagation, need to clean it up
	if (!SourceAudioComponent || !SourceLPFPreset)
		return;

	FSourceEffectFilterSettings settings;
	settings.FilterCircuit   = ESourceEffectFilterCircuit::StateVariable;
	settings.FilterType      = ESourceEffectFilterType::LowPass;
	settings.CutoffFrequency = FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, GainHF);
	settings.FilterQ         = 0.707f; // TODO - what is resonance?
	SourceLPFPreset->SetSettings(settings);

	SourceAudioComponent->SetVolumeMultiplier(GainLF);
}

void AVAudioEmitter::UpdateSourceSubmix()
{
	// TODO - why could these be null? There is too much null propagation, need to clean it up
	if (!SourceAudioComponent || !AudioWorld || !Emitter)
		return;

	int32 NewIndex = vaEmitterGetGroupedEAXIndex(Emitter);

	if (NewIndex != CurrentGroupedEAXIndex)
	{
		UE_LOG(LogTemp, Log, TEXT("VAudioEmitter.cpp: UpdateSourceSubmix(): '%s': groupedEAXIndex changed from %d to %d"), *GetName(), CurrentGroupedEAXIndex, NewIndex);

		// Unlink from the old submix
		if (CurrentGroupedEAXIndex >= 0)
		{
			USoundSubmix* OldSubmix = AudioWorld->GetGroupedEAXSubmix(CurrentGroupedEAXIndex);

			if (OldSubmix)
				SourceAudioComponent->SetSubmixSend(OldSubmix, 0.0f);
		}

		CurrentGroupedEAXIndex = NewIndex;
	}

	if (CurrentGroupedEAXIndex < 0)
		return;

	USoundSubmix* Submix = AudioWorld->GetGroupedEAXSubmix(CurrentGroupedEAXIndex);

	// TODO - assert or write a warning message somewhere that there aren't enough submixes allocated in the VAudioWorld actor
	if (!Submix)
		return;

	// Default send level is overridden below if the listener has relative reverb data
	float SendLevel = 1.0f;
	bool bHasRelative = false;

	VAWorld* vaWorld = AudioWorld->GetVAWorld();
	AVAudioEmitter* Listener = AudioWorld->GetMainListener();

	if (!vaWorld)
	{
		VaRawLog(L"VaudioEmitter.cpp: UpdateSourceSubmix(): Unable to access grouped EAX data as vaWorld is null");
	}
	// Wait for raytracing to run at least once
	else if (vaWorldGetReverbCalculated(vaWorld))
	{
		const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(vaWorld);

		if (!GroupedEAX)
		{
			// TODO - if vaWorldGetReverbCalculated above is true, GroupedEAX shouldn't be null, because world.maximumGroupedEAXCount has to be >= 2. Check if this >= 2 range is defined in vaudio.h
			VaRawLog(L"VaudioEmitter.cpp: UpdateSourceSubmix(): Reverb is calculated but vaWorldGetGroupedEAX() returned null");
		}
		else if (GroupedEAX[CurrentGroupedEAXIndex])
		{
			const VAEAXReverb* EAX = GroupedEAX[CurrentGroupedEAXIndex];

			if (!Listener)
			{
				VaRawLog(L"VaudioEmitter.cpp: UpdateSourceSubmix(): Unable to access relative EAX gain as Listener is null");
			}
			else
			{
				VAEmitter* ListenerVA = Listener->GetVAEmitter();

				// UE-LIMITATION - only relative gain is supported. Can't do directional reverb
				float* Gain = vaEAXReverbGetRelativeGain(EAX, ListenerVA);

				if (Gain)
				{
					// Assert gain is >= 0 and <= 1
					SendLevel = *Gain;
					bHasRelative = true;
				}
			}
		}
		else
		{
			VaRawLog(L"VaudioEmitter.cpp: UpdateSourceSubmix(): Reverb is calculated but GroupedEAX[CurrentGroupedEAXIndex] is null");
		}
	}
	else
	{
		VaRawLog(L"VaudioEmitter.cpp: UpdateSourceSubmix(): Unable to access grouped EAX data as vaWorldGetReverbCalculated() is false");
	}

	SourceAudioComponent->SetSubmixSend(Submix, SendLevel);

	if (GEngine)
	{
		FString RelStr = FString::Printf(TEXT("Submix gain is %.3f"), SendLevel);
		GEngine->AddOnScreenDebugMessage((uint64)this + 200, 0.0f, FColor::Magenta, FString::Printf(TEXT("VAudioEmitter.cpp: UpdateSourceSubmix(): '%s': %s"), *GetName(), *RelStr));
	}
}

void AVAudioEmitter::ApplyGroupedEAXReverb()
{
	if (!AudioWorld)
	{
		VaRawLog(L"VaudioEmitter.cpp: ApplyGroupedEAXReverb(): AudioWorld is null");
		return;
	}

	VAWorld* vaWorld = AudioWorld->GetVAWorld();
	if (!vaWorld)
	{
		VaRawLog(L"VaudioEmitter.cpp: ApplyGroupedEAXReverb(): vaWorld is null");
		return;
	}

	const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(vaWorld);
	int32 Count = vaWorldGetGroupedEAXCount(vaWorld);

	// Wait for raytracing to run at least once
	if (!vaWorldGetReverbCalculated(vaWorld))
		return;

	if (!GroupedEAX)
	{
		// Assert here - if vaWorldGetReverbCalculated is true, vaWorldGetGroupedEAX() shouldn't return null
		return;
	}

	for (int32 i = 0; i < Count; ++i)
	{
		USubmixEffectReverbPreset* Preset = AudioWorld->GetGroupedEAXPreset(i);

		if (!Preset)
		{
			// Assert here - why is it null? Submixes should all be set up by now. Need less null propagation in this codebase
			VaRawLog(L"VaudioEmitter.cpp: ApplyGroupedEAXReverb(): AudioWorld->GetGroupedEAXPreset[%d] is null", i);
			continue;
		}

		const VAEAXReverb* EAX = GroupedEAX[i];

		if (!EAX)
		{
			// Assert here - why is it null? Submixes should all be set up by now. Need less null propagation in this codebase
			VaRawLog(L"VaudioEmitter.cpp: ApplyGroupedEAXReverb(): GroupedEAX[%d] is null", i);
			continue;
		}

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

		// TODO - do we need to clamp? EAX is already clamped to the constants in vaudio.h
		FSubmixEffectReverbSettings settings;
		settings.DecayTime           = FMath::Clamp(EAX->decayTime,           0.1f,  20.0f);
		settings.DecayHFRatio        = FMath::Clamp(EAX->decayHFRatio,        0.1f,   2.0f);
		settings.Density             = FMath::Clamp(EAX->density,             0.0f,   1.0f);
		settings.Diffusion           = FMath::Clamp(EAX->diffusion,           0.0f,   1.0f);
		settings.Gain                = FMath::Clamp(EAX->gain,                0.0f,   1.0f);
		settings.GainHF              = FMath::Clamp(EAX->gainHF,              0.0f,   1.0f);
		settings.ReflectionsGain     = FMath::Clamp(EAX->reflectionsGain,     0.0f,   3.16f);
		settings.ReflectionsDelay    = FMath::Clamp(EAX->reflectionsDelay,    0.0f,   0.3f);
		settings.LateGain            = FMath::Clamp(EAX->lateReverbGain,      0.0f,  10.0f);
		settings.LateDelay           = FMath::Clamp(EAX->lateReverbDelay,     0.0f,   0.1f);
		settings.AirAbsorptionGainHF = FMath::Clamp(EAX->airAbsorptionGainHF, 0.0f,   1.0f);
		settings.WetLevel            = FMath::Clamp(EAX->returnedPercent,     0.0f,   1.0f);
		settings.DryLevel            = 0.0f;
		Preset->SetSettings(settings);
	}
}

