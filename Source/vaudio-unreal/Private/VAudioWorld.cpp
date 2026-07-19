#include "VAudioWorld.h"
#include "VAudioEmitter.h"
#include "VAudioMaterial.h"
#include "VAudioMaterialComponent.h"
#include "VAudioMaterialConversion.h"
#include "Components/BillboardComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "AudioMixerBlueprintLibrary.h"

extern "C" {
#include "vaudio.h"
}

#include "VaRawLog.h"
#include "VADebugMessageKeys.h"

// ---------------------------------------------------------------------------
// Helpers (identical coordinate remapping to the original BPLib)
// ---------------------------------------------------------------------------

static VAMatrix MakeTranslationMatrix(const FVector& P)
{
	return vaMatrixCreateTranslation((float)P.X, (float)P.Y, (float)P.Z);
}

static VAMatrix MakeRotTransMatrix(const FTransform& T)
{
	FQuat Q = T.GetRotation();
	FVector P = T.GetTranslation();

	FVector AxX = Q.GetAxisX();
	FVector AxY = Q.GetAxisY();
	FVector AxZ = Q.GetAxisZ();

	return vaMatrixCreate(
		(float)AxX.X, (float)AxX.Y, (float)AxX.Z, 0.f,
		(float)AxY.X, (float)AxY.Y, (float)AxY.Z, 0.f,
		(float)AxZ.X, (float)AxZ.Y, (float)AxZ.Z, 0.f,
		(float)P.X,   (float)P.Y,   (float)P.Z,   1.f
	);
}

AVAudioWorld::AVAudioWorld()
{
	PrimaryActorTick.bCanEverTick = true;

	UBillboardComponent* Root = CreateDefaultSubobject<UBillboardComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AVAudioWorld::BeginPlay()
{
	Super::BeginPlay();

	// On-screen debug messages otherwise persist from the previous PIE/game session (GEngine
	// outlives individual play sessions), so stale entries from actors that no longer exist would
	// stick around and get mixed in with this session's messages.
	if (GEngine)
		GEngine->ClearOnScreenDebugMessages();

	World = vaWorldCreate();
	vaWorldSetLogMemoryAllocationWarnings(World, true);
	vaWorldSetCoordinateSystem(World, VACoordinateSystemUnreal);
	vaWorldSetLogCallback(World, &VaSdkLogCallback);
	vaWorldSetPosition(World, vaVectorCreate(
		(float)WorldPosition.X,
		(float)WorldPosition.Y,
		(float)WorldPosition.Z));
	vaWorldSetSize(World, vaVectorCreate(
		(float)WorldSize.X,
		(float)WorldSize.Y,
		(float)WorldSize.Z));
	vaWorldSetInverseSpeedOfSound(World, 1.0f / SpeedOfSound);
	vaWorldSetMetersPerUnit(World, MetersPerUnit);
	vaWorldSetWorldIsIndoors(World, bIsIndoors);
	vaWorldSetEpsilon(World, Epsilon);
	vaWorldSetEmittersOutsideTheWorldAreMuffled(World, bEmittersOutsideTheWorldAreMuffled);
	vaWorldSetWorkItemCount(World, FMath::Max(1, WorkItemCount));
	vaWorldSetMaximumConcurrencyLevel(World, FMath::Max(1, MaximumConcurrencyLevel));
	vaWorldSetPendingShutdown(World, bPendingShutdown);
	vaWorldSetReferenceFrequencyLF(World, ReferenceFrequencyLF);
	vaWorldSetReferenceFrequencyHF(World, ReferenceFrequencyHF);

	if (bAirAbsorptionEnabled)
	{
		vaWorldSetAirAbsorptionHumidity(World, Humidity);
		vaWorldSetAirAbsorptionTemperature(World, Temperature);
		vaWorldSetAirAbsorptionPressure(World, Pressure);
	}
	else
	{
		vaWorldSetAirAbsorption(World, nullptr);
	}

	int32 GroupedEAXCount = GroupedEAXSubmixes.Num();
	vaWorldSetMaximumGroupedEAXCount(World, GroupedEAXCount);

	for (int32 i = 0; i < GroupedEAXCount; i++)
	{
		USoundSubmix* Sub = GroupedEAXSubmixes[i];
		USubmixEffectReverbPreset* Preset = NewObject<USubmixEffectReverbPreset>(this);
		if (Sub)
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, Sub, Preset);
		GroupedEAXPresets.Add(Preset);
		VALog(L"GroupedEAX[%d] submix=%s", i, Sub ? *Sub->GetName() : TEXT("null"));
	}

	ApplyChildMaterials();
	ScanAndAddPrimitives();
}

void AVAudioWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	GroupedEAXPresets.Empty();

	if (World)
	{
		vaWorldWait(World);
		DestroyPrimitives();
		vaWorldDestroy(World);
		World = nullptr;
	}
}

void AVAudioWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (World)
	{
		vaWorldUpdate(World);

		static float TimeSinceHeartbeat = 0.0f;
		TimeSinceHeartbeat += DeltaTime;

		bool doHeartbeat = TimeSinceHeartbeat >= 1.0f;

		if (doHeartbeat)
		{
			VALog(L"RaytracingTime=%.3fms RegisteredEmitters=%d", vaWorldGetRaytracingTime(World), RegisteredEmitters.Num());
			VALog(L"Primitives: prisms=%d spheres=%d capsules=%d meshes=%d", PrismPrimitives.Num(), SpherePrimitives.Num(), CapsulePrimitives.Num(), MeshPrimitives.Num());
			TimeSinceHeartbeat = 0.0f;
		}

		if (bReverbOnly != bWasReverbOnly)
		{
			bWasReverbOnly = bReverbOnly;
			bool bDryEnabled = !bReverbOnly;

			for (AVAudioEmitter* Emitter : RegisteredEmitters)
				Emitter->SetDryOutputEnabled(bDryEnabled);
		}

		if (GEngine)
		{
			// GEngine draws these on-screen messages in the reverse of the order they're added
			// each tick (last call ends up at the top), so this block is sequenced bottom-up:
			// whatever should appear highest on screen is called LAST.

			// Per-emitter position and world-bounds check
			for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
			{
				AVAudioEmitter* emitter = RegisteredEmitters[i];
				VAEmitter* vaEmitter = emitter->GetVAEmitter();

				uint64 messageID = VAEmitterStatus + emitter->GetEmitterIndex();

				// Registered emitters can still have a null VAEmitter* if TryInitializeEmitter()
				// hasn't completed yet (e.g. this world's own BeginPlay ran after theirs) - skip
				// until it catches up on a later Tick.
				if (!vaEmitter)
				{
					GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange,
						FString::Printf(TEXT("[VA] Emitter %d '%s': initialising"), i, *emitter->GetActorNameOrLabel()));

					continue;
				}

				bool bInBounds = vaEmitterGetWithinWorldBounds(vaEmitter);
				VAVector P = vaEmitterGetPosition(vaEmitter);

				const wchar_t* boundsStatus = bInBounds ? TEXT("[in bounds]") : TEXT("[out of bounds]");


				if (emitter->bIsMainListener)
				{
					FColor color = bInBounds ? FColor::Green : FColor::Orange;

					GEngine->AddOnScreenDebugMessage(messageID, 0.0f, color,
						FString::Printf(TEXT("[VA] Main Listener Emitter %d '%s': (%.1f, %.1f, %.1f) %s"), i, *emitter->GetActorNameOrLabel(), P.x, P.y, P.z, boundsStatus));
				}
				else
				{
					const wchar_t* sourceStatus = emitter->SourceAudioComponent ? TEXT("valid") : TEXT("null");

					if (emitter->bAffectsGroupedEAX)
					{
						int32 groupedEAXIndex = vaEmitterGetGroupedEAXIndex(vaEmitter);
						USoundSubmix* Submix = GetGroupedEAXSubmix(groupedEAXIndex);

						FColor color = bInBounds && Submix != NULL && emitter->SourceAudioComponent != NULL ? FColor::Green : FColor::Orange;

						const wchar_t* submixStatus = Submix ? TEXT("valid") : TEXT("null");

						GEngine->AddOnScreenDebugMessage(messageID, 0.0f, color,
							FString::Printf(TEXT("[VA] Source Emitter %d '%s': (%.1f, %.1f, %.1f), %s, source=%s groupedEAXIndex=%d, submix=%s"), i, *emitter->GetActorNameOrLabel(), P.x, P.y, P.z, boundsStatus, sourceStatus, groupedEAXIndex, submixStatus));
					}
					else
					{
						FColor color = bInBounds && emitter->SourceAudioComponent != NULL ? FColor::Green : FColor::Orange;

						GEngine->AddOnScreenDebugMessage(messageID, 0.0f, color,
							FString::Printf(TEXT("[VA] Source Emitter %d '%s': (%.1f, %.1f, %.1f), %s, source=%s [No EAX]"), i, *emitter->GetActorNameOrLabel(), P.x, P.y, P.z, boundsStatus, sourceStatus));
					}
				}
			}

			// Per-source-emitter submix send level (mirrors the gain UpdateSourceSubmix() sends to
			// the grouped EAX submix - recomputed here purely for display, doesn't affect audio).
			for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
			{
				AVAudioEmitter* emitter = RegisteredEmitters[i];
				VAEmitter* vaEmitter = emitter->GetVAEmitter();

				if (!vaEmitter || emitter->bIsMainListener || !emitter->bAffectsGroupedEAX)
					continue;

				int32 groupedEAXIndex = vaEmitterGetGroupedEAXIndex(vaEmitter);
				if (groupedEAXIndex < 0)
					continue;

				float SendLevel = 1.0f;

				if (vaWorldGetInitialising(World) == false)
				{
					const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(World);
					AVAudioEmitter* Listener = GetMainListener();

					if (GroupedEAX && GroupedEAX[groupedEAXIndex] && Listener && Listener->GetVAEmitter())
					{
						// UE-LIMITATION - only relative gain is supported. Can't do directional reverb
						float* Gain = vaEAXReverbGetRelativeGain(GroupedEAX[groupedEAXIndex], Listener->GetVAEmitter());
						if (Gain)
							SendLevel = *Gain;
					}
				}

				uint64 messageID = VAEmitterMessageBase + emitter->GetEmitterIndex() * VAEmitterMessageStride + VAEmitterSubmixStatus;
				GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Green,
					FString::Printf(TEXT("VA Submix '%s': gain is %.3f"), *emitter->GetActorNameOrLabel(), SendLevel));
			}

			// Per-target LPF applied by the main listener (mirrors the filter AVAudioEmitter::Tick()
			// applies to each target's source - recomputed here purely for display).
			if (AVAudioEmitter* MessageListener = GetMainListener())
			{
				VAEmitter* ListenerVA = MessageListener->GetVAEmitter();

				if (ListenerVA)
				{
					for (int32 i = 0; i < MessageListener->TargetEmitters.Num(); ++i)
					{
						AVAudioEmitter* Target = MessageListener->TargetEmitters[i];

						uint64 messageID = VAEmitterMessageBase + MessageListener->GetEmitterIndex() * VAEmitterMessageStride + VAEmitterTargetStatus + i;

						if (!Target)
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has a null target"), *MessageListener->GetActorNameOrLabel()));
							continue;
						}

						if (!Target->GetVAEmitter())
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' target '%s' has no emitter. Ensure the target emitter is assigned to the same World"), *MessageListener->GetActorNameOrLabel(), *Target->GetActorNameOrLabel()));
							continue;
						}

						if (!vaEmitterHasRaytracedTarget(ListenerVA, Target->GetVAEmitter()))
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has not raytraced the '%s' emitter yet"), *MessageListener->GetActorNameOrLabel(), *Target->GetActorNameOrLabel()));
							continue;
						}

						VALowPassFilter* lowPassFilter = vaEmitterGetTargetFilter(ListenerVA, Target->GetVAEmitter());

						if (!lowPassFilter)
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has raytraced the '%s' emitter, but has an invalid low pass filter"), *MessageListener->GetActorNameOrLabel(), *Target->GetActorNameOrLabel()));
							continue;
						}

						GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Green, FString::Printf(TEXT("[VA] '%s' filter: gainLF=%.2f  gainHF=%.2f"), *Target->GetActorNameOrLabel(), lowPassFilter->gainLF, lowPassFilter->gainHF));
					}
				}
			}

			// Per-grouped-EAX-zone reverb data (mirrors the settings ApplyGroupedEAXReverb() sends
			// to each preset - recomputed here purely for display).
			if (vaWorldGetInitialising(World) == false)
			{
				const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(World);
				int32 GroupedEAXCount = vaWorldGetGroupedEAXCount(World);

				if (GroupedEAX)
				{
					for (int32 i = 0; i < GroupedEAXCount; ++i)
					{
						const VAEAXReverb* EAX = GroupedEAX[i];

						uint64 messageID = VAGroupedEAXMessageBase + i;
						if (!EAX)
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange,
								FString::Printf(TEXT("VA GroupedEAX[%d]: invalid"), i));

							continue;
						}

						GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Green,
							FString::Printf(TEXT("VA GroupedEAX[%d]: decayTime=%.3f wetLevel=%.3f gain=%.3f"), i, EAX->decayTime, EAX->returnedPercent, EAX->gain));
					}
				}
			}

			if (GroupedEAXSubmixes.Num() == 0)
			{
				GEngine->AddOnScreenDebugMessage(VAGroupedEAXStatusMessage, 0.0f, FColor::Orange, FString::Printf(TEXT("World '%s' has no Grouped EAX Submixes. Ensure at least one is added"), *GetActorNameOrLabel()));
			}


			if (AVAudioEmitter* CurrentMainListener = GetMainListener())
			{
				FVector ListenerPos = CurrentMainListener->GetActorLocation();

				int targetCount = CurrentMainListener->TargetEmitters.Num();
				FColor color = targetCount == 0 ? FColor::Orange : FColor::Green;

				const wchar_t* plural = targetCount == 1 ? TEXT("target") : TEXT("targets");

				GEngine->AddOnScreenDebugMessage(VAListenerStatusMessage, 0.0f, color, FString::Printf(TEXT("Listener '%s' has %d %s"), *CurrentMainListener->GetActorNameOrLabel(), targetCount, plural));

				VAEmitter* emitter = CurrentMainListener->GetVAEmitter();

				if (vaEmitterGetAmbientOcclusionEnabled(emitter) || vaEmitterGetAmbientPermeationEnabled(emitter))
				{
					// Wait for raytracing to complete at least once
					if (VALowPassFilter* ambientFilter = vaEmitterGetAmbientFilter(emitter))
					{
						GEngine->AddOnScreenDebugMessage(VAAmbientFilterMessage, 0.0f, FColor::Green, FString::Printf(TEXT("[VA] Ambient LPF: gainLF=%.3f  gainHF=%.3f"), ambientFilter->gainLF, ambientFilter->gainHF));
					}
				}
			}
			else
				GEngine->AddOnScreenDebugMessage(VAListenerStatusMessage, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] There is no main listener. Ensure one emitter has Listener > Is Main Listener enabled, and is assigned to a World")));

			GEngine->AddOnScreenDebugMessage(VAPrimitiveStatusMessage, 0.0f, FColor::Cyan, FString::Printf(TEXT("[VA] Primitives: prisms=%d spheres=%d capsules=%d meshes=%d"), PrismPrimitives.Num(), SpherePrimitives.Num(), CapsulePrimitives.Num(), MeshPrimitives.Num()));
			GEngine->AddOnScreenDebugMessage(VARaytracingTimeMessage, 0.0f, FColor::Cyan, FString::Printf(TEXT("[VA] Emitters: %d, Raytracing: %.3f ms"), vaWorldGetEmitterCount(World), vaWorldGetRaytracingTime(World)));
		}
	}
}

USoundSubmix* AVAudioWorld::GetGroupedEAXSubmix(int32 Index) const
{
	return GroupedEAXSubmixes.IsValidIndex(Index) ? GroupedEAXSubmixes[Index] : nullptr;
}

USubmixEffectReverbPreset* AVAudioWorld::GetGroupedEAXPreset(int32 Index) const
{
	return GroupedEAXPresets.IsValidIndex(Index) ? GroupedEAXPresets[Index] : nullptr;
}

void AVAudioWorld::RegisterEmitter(AVAudioEmitter* Emitter)
{
	RegisteredEmitters.AddUnique(Emitter);
	Emitter->SetEmitterIndex(RegisteredEmitters.Find(Emitter));

	if (Emitter->bIsMainListener)
	{
		if (MainListener.IsValid() && MainListener.Get() != Emitter)
		{
			VALog(L"'%s' registered as main listener, but '%s' is already the main listener - keeping the first one. Only one emitter should have bIsMainListener = true.",
				*Emitter->GetActorNameOrLabel(), *MainListener->GetActorNameOrLabel());
		}
		else
		{
			MainListener = Emitter;
		}
	}
}

void AVAudioWorld::UnregisterEmitter(AVAudioEmitter* Emitter)
{
	RegisteredEmitters.Remove(Emitter);
	Emitter->SetEmitterIndex(-1);

	// Removing shifts every later emitter's position in RegisteredEmitters - keep EmitterIndex
	// (used to build on-screen debug message keys, see VADebugMessageKeys.h) in sync so indices
	// stay dense and no two registered emitters ever share a key.
	for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
		RegisteredEmitters[i]->SetEmitterIndex(i);

	if (MainListener.Get() == Emitter)
		MainListener = nullptr;
}

AVAudioEmitter* AVAudioWorld::GetMainListener() const
{
	return MainListener.Get();
}


void AVAudioWorld::ExportWorld()
{
	if (!World)
	{
		VALog(L"world is null (press Play first).");
		return;
	}

	FString Path = FPaths::ProjectDir() + TEXT("vaudio_export.va");
	vaWorldExport(World, TCHAR_TO_UTF8(*Path));
}

void AVAudioWorld::ImportWorld()
{
	if (!World)
	{
		VALog(L"world is null (press Play first).");
		return;
	}

	FString Path = FPaths::ProjectDir() + TEXT("vaudio_export.va");
	VAEmitter** ImportedEmitters = nullptr;
	int32 ImportedEmitterCount = 0;

	VAResult Result = vaWorldImport(World, TCHAR_TO_UTF8(*Path), &ImportedEmitters, &ImportedEmitterCount);

	if (Result != VA_SUCCESS)
	{
		VALog(L"import failed (result=%d) for '%s'.", Result, *Path);
		return;
	}

	VALog(L"imported %d emitter(s) from '%s'.", ImportedEmitterCount, *Path);
	free(ImportedEmitters);
}

// ---------------------------------------------------------------------------
// Child material actors — AVAudioMaterial children of this world actor
// ---------------------------------------------------------------------------

void AVAudioWorld::ApplyChildMaterials()
{
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);

	int32 AppliedCount = 0;

	for (AActor* Child : AttachedActors)
	{
		AVAudioMaterial* Mat = Cast<AVAudioMaterial>(Child);

		if (Mat)
		{
			Mat->ApplyToWorld(World);
			++AppliedCount;
		}
	}
}

// ---------------------------------------------------------------------------
// Primitive scan — finds every actor with UVAudioMaterialComponent
// ---------------------------------------------------------------------------

// Walk the attach-parent chain to find the nearest UVAudioMaterialComponent.
static UVAudioMaterialComponent* FindMaterialInChain(AActor* Actor)
{
	for (AActor* actor = Actor; actor != nullptr; actor = actor->GetAttachParentActor())
	{
		UVAudioMaterialComponent* materialComponent = actor->FindComponentByClass<UVAudioMaterialComponent>();

		if (materialComponent)
			return materialComponent;
	}
	return nullptr;
}

// True if this mesh would use its simple collision (sphyl/sphere/box) rather than the
// triangle-mesh fallback, matching the bAddedSimple check in ScanAndAddPrimitives.
static bool HasSimpleCollision(UStaticMesh* Mesh)
{
	UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;

	if (!BodySetup)
		return false;

	const FKAggregateGeom& Agg = BodySetup->AggGeom;
	return !Agg.SphylElems.IsEmpty() || !Agg.SphereElems.IsEmpty() || !Agg.BoxElems.IsEmpty();
}

#if WITH_EDITOR
void AVAudioWorld::BakeGeometry()
{
	UWorld* UEWorld = GetWorld();

	if (!UEWorld)
	{
		VALog(L"no world (open a level first).");
		return;
	}

	Modify();
	BakedMeshes.Reset();

	int32 BakedCount = 0;

	for (TActorIterator<AActor> ActorIt(UEWorld); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;

		// Null if this actor (or its attach-parent chain) has no UVAudioMaterialComponent, or has
		// one that belongs to a different VAudioWorld - not baked geometry for this world.
		UVAudioMaterialComponent* MatComp = FindMaterialInChain(Actor);
		if (!MatComp || MatComp->AudioWorld != this) continue;

		TArray<UStaticMeshComponent*> MeshComps;
		Actor->GetComponents<UStaticMeshComponent>(MeshComps);

		for (UStaticMeshComponent* MeshComp : MeshComps)
		{
			// Null if the component has no mesh assigned. Simple-collision meshes are skipped
			// here too - ScanAndAddPrimitives() already picks up their live collision shapes
			// every run, so baking their triangle mesh as well would be redundant.
			UStaticMesh* Mesh = MeshComp->GetStaticMesh();
			if (!Mesh || HasSimpleCollision(Mesh)) continue;

			if (!Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty())
			{
				VALog(L"mesh '%s' on '%s' has no render data in-editor, skipping", *Mesh->GetName(), *Actor->GetActorNameOrLabel());
				continue;
			}

			FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
			FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;

			TArray<uint32> Indices;
			LOD.IndexBuffer.GetCopy(Indices);
			if (Indices.IsEmpty())
				continue;

			FVAudioBakedMesh& Baked = BakedMeshes.AddDefaulted_GetRef();
			Baked.ActorName = Actor->GetName();
			Baked.ComponentName = MeshComp->GetFName();
			Baked.Vertices.Reserve(Indices.Num());

			for (uint32 Idx : Indices)
				Baked.Vertices.Add(PosBuffer.VertexPosition(Idx));

			++BakedCount;

			VALog(L"baked '%s'.'%s' tris=%d", *Actor->GetActorNameOrLabel(), *MeshComp->GetName(), Indices.Num() / 3);
		}
	}

	MarkPackageDirty();
	VALog(L"baked %d mesh component(s). Save the level to persist.", BakedCount);
}
#endif

void AVAudioWorld::ScanAndAddPrimitives()
{
	UWorld* UEWorld = GetWorld();

	// Null if this actor isn't in a live level (e.g. called outside BeginPlay/PIE).
	if (!UEWorld)
		return;

	int32 SimpleCount = 0;
	int32 MeshCount = 0;
	int32 SkippedCount = 0;

	for (TActorIterator<AActor> ActorIt(UEWorld); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;

		UVAudioMaterialComponent* MatComp = FindMaterialInChain(Actor);
		if (!MatComp || MatComp->AudioWorld != this)
		{
			++SkippedCount;
			continue;
		}

		VAMaterialType Material = EVAudioMaterialToVA(MatComp->Material);

		TArray<UShapeComponent*> ShapeComps;
		Actor->GetComponents<UShapeComponent>(ShapeComps);

		for (UShapeComponent* ShapeComp : ShapeComps)
		{
			FTransform CompTransform = ShapeComp->GetComponentTransform();

			if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(ShapeComp))
			{
				VAMatrix Mat = MakeRotTransMatrix(CompTransform);

				VACapsulePrimitive* Cap = vaCapsulePrimitiveCreate();
				vaCapsulePrimitiveSetRadius(Cap,   Capsule->GetScaledCapsuleRadius());
				vaCapsulePrimitiveSetLength(Cap,   Capsule->GetScaledCapsuleHalfHeight_WithoutHemisphere() * 2.0f);
				vaCapsulePrimitiveSetMaterial(Cap, Material);
				vaCapsulePrimitiveSetTransform(Cap, &Mat);
				vaWorldAddPrimitive_(World, Cap);
				CapsulePrimitives.Add(Cap);
				++SimpleCount;
			}
			else if (USphereComponent* Sphere = Cast<USphereComponent>(ShapeComp))
			{
				FVector Center = CompTransform.GetTranslation();

				VASpherePrimitive* Sp = vaSpherePrimitiveCreate();
				vaSpherePrimitiveSetCenter(Sp, vaVectorCreate((float)Center.X, (float)Center.Y, (float)Center.Z));
				vaSpherePrimitiveSetRadius(Sp,   Sphere->GetScaledSphereRadius());
				vaSpherePrimitiveSetMaterial(Sp, Material);
				vaWorldAddPrimitive_(World, Sp);
				SpherePrimitives.Add(Sp);
				++SimpleCount;
			}
			else if (UBoxComponent* Box = Cast<UBoxComponent>(ShapeComp))
			{
				VAMatrix Mat = MakeRotTransMatrix(CompTransform);
				FVector Extent = Box->GetScaledBoxExtent();

				VAPrismPrimitive* Prism = vaPrismPrimitiveCreate();
				vaPrismPrimitiveSetSize(Prism, vaVectorCreate(Extent.X * 2.0f, Extent.Y * 2.0f, Extent.Z * 2.0f));
				vaPrismPrimitiveSetMaterial(Prism, Material);
				vaPrismPrimitiveSetTransform(Prism, &Mat);
				vaWorldAddPrimitive_(World, Prism);
				PrismPrimitives.Add(Prism);
				++SimpleCount;
			}
		}

		TArray<UStaticMeshComponent*> MeshComps;
		Actor->GetComponents<UStaticMeshComponent>(MeshComps);

		if (MeshComps.IsEmpty())
		{
			continue;
		}

		for (UStaticMeshComponent* MeshComp : MeshComps)
		{
			UStaticMesh* Mesh = MeshComp->GetStaticMesh();

			// Null if the component has no mesh assigned; simple collision shapes on
			// mesh-less actors are picked up separately via UShapeComponent above.
			if (!Mesh)
				continue;

			FTransform CompTransform = MeshComp->GetComponentTransform();
			FVector WorldPos = CompTransform.GetTranslation();
			FVector Scale = CompTransform.GetScale3D();

			bool bAddedSimple = false;
			UBodySetup* BodySetup = Mesh->GetBodySetup();

			if (BodySetup)
			{
				const FKAggregateGeom& Agg = BodySetup->AggGeom;

				for (const FKSphylElem& Sphyl : Agg.SphylElems)
				{
					FQuat   WorldRot    = Sphyl.GetTransform().GetRotation() * CompTransform.GetRotation();
					FVector WorldCenter = CompTransform.TransformPosition(Sphyl.GetTransform().GetTranslation());
					FTransform WT(WorldRot, WorldCenter, FVector::OneVector);
					VAMatrix Mat = MakeRotTransMatrix(WT);

					VACapsulePrimitive* Cap = vaCapsulePrimitiveCreate();
					vaCapsulePrimitiveSetRadius(Cap,   Sphyl.Radius * FMath::Max(Scale.X, Scale.Y));
					vaCapsulePrimitiveSetLength(Cap,   Sphyl.Length * Scale.Z);
					vaCapsulePrimitiveSetMaterial(Cap, Material);
					vaCapsulePrimitiveSetTransform(Cap, &Mat);
					vaWorldAddPrimitive_(World, Cap);
					CapsulePrimitives.Add(Cap);
					bAddedSimple = true;
					++SimpleCount;
				}

				for (const FKSphereElem& Sphere : Agg.SphereElems)
				{
					FVector Center = CompTransform.TransformPosition(Sphere.GetTransform().GetTranslation());
					float   Radius = Sphere.Radius * Scale.GetAbsMax();

					VASpherePrimitive* Sp = vaSpherePrimitiveCreate();
					vaSpherePrimitiveSetCenter(Sp, vaVectorCreate((float)Center.X, (float)Center.Y, (float)Center.Z));
					vaSpherePrimitiveSetRadius(Sp,   Radius);
					vaSpherePrimitiveSetMaterial(Sp, Material);
					vaWorldAddPrimitive_(World, Sp);
					SpherePrimitives.Add(Sp);
					bAddedSimple = true;
					++SimpleCount;
				}

				for (const FKBoxElem& Box : Agg.BoxElems)
				{
					FQuat   WorldRot    = Box.GetTransform().GetRotation() * CompTransform.GetRotation();
					FVector WorldCenter = CompTransform.TransformPosition(Box.GetTransform().GetTranslation());
					FTransform WT(WorldRot, WorldCenter, FVector::OneVector);
					VAMatrix Mat = MakeRotTransMatrix(WT);

					VAPrismPrimitive* Prism = vaPrismPrimitiveCreate();
					vaPrismPrimitiveSetSize(Prism, vaVectorCreate(Box.X * Scale.X, Box.Y * Scale.Y, Box.Z * Scale.Z));
					vaPrismPrimitiveSetMaterial(Prism, Material);
					vaPrismPrimitiveSetTransform(Prism, &Mat);
					vaWorldAddPrimitive_(World, Prism);
					PrismPrimitives.Add(Prism);
					bAddedSimple = true;
					++SimpleCount;
				}
			}

			if (bAddedSimple)
				continue;

			// Fall back to triangle mesh: prefer baked geometry (reliable in shipping builds
			// regardless of bAllowCPUAccess/cook quirks), otherwise use the mesh's live render
			// data (always up to date, but may be unavailable in cooked builds).
			const FVAudioBakedMesh* Baked = nullptr;

			for (const FVAudioBakedMesh& bakedMesh : BakedMeshes)
			{
				if (bakedMesh.ComponentName == MeshComp->GetFName() && bakedMesh.ActorName == Actor->GetName())
				{
					Baked = &bakedMesh;
					break;
				}
			}

			TArray<FVector3f> LocalPositions;
			if (Baked)
			{
				LocalPositions = Baked->Vertices;
			}
			else
			{
				if (!Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty())
				{
					VALog(L"mesh '%s' has no render data and no baked geometry, skipping. Run 'Bake Geometry For Shipping' on the VA Audio World and save the level.", *Mesh->GetName());
					continue;
				}

				FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
				FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;

				TArray<uint32> Indices;
				LOD.IndexBuffer.GetCopy(Indices);
				if (Indices.IsEmpty())
				{
					VALog(L"mesh '%s' has no index data and no baked geometry, skipping. Run 'Bake Geometry For Shipping' on the VA Audio World and save the level.", *Mesh->GetName());
					continue;
				}

				LocalPositions.Reserve(Indices.Num());
				for (uint32 Idx : Indices)
					LocalPositions.Add(PosBuffer.VertexPosition(Idx));
			}

			TArray<VAVector> VAVerts;
			VAVerts.Reserve(LocalPositions.Num());
			VAVector MinB = VECTOR_MAX;
			VAVector MaxB = VECTOR_MIN;

			for (const FVector3f& LocalPos : LocalPositions)
			{
				FVector RotScaled = CompTransform.GetRotation().RotateVector(Scale * FVector(LocalPos));
				VAVector V = vaVectorCreate((float)RotScaled.X, (float)RotScaled.Y, (float)RotScaled.Z);
				VAVerts.Add(V);
				MinB = vaVectorMin(MinB, V);
				MaxB = vaVectorMax(MaxB, V);
			}

			VAMatrix Transform = MakeTranslationMatrix(WorldPos);
			VAMeshPrimitive* MeshPrim = vaMeshPrimitiveCreate(
				Material, VAVerts.GetData(), VAVerts.Num(), MinB, MaxB, &Transform
			);

			vaMeshPrimitiveSetSupports3DPermeation(MeshPrim, MatComp->bSupports3DPermeation);

			vaWorldAddPrimitive_(World, MeshPrim);
			MeshPrimitives.Add(MeshPrim);
			++MeshCount;
		}
	}

	VALog(L"added %d simple + %d mesh primitives (%d actors skipped, no material)", SimpleCount, MeshCount, SkippedCount);
}

void AVAudioWorld::DestroyPrimitives()
{
	for (VAMeshPrimitive*    P : MeshPrimitives)    { vaWorldRemovePrimitive_(World, P); vaMeshPrimitiveDestroy(P); }
	for (VACapsulePrimitive* P : CapsulePrimitives) { vaWorldRemovePrimitive_(World, P); vaCapsulePrimitiveDestroy(P); }
	for (VASpherePrimitive*  P : SpherePrimitives)  { vaWorldRemovePrimitive_(World, P); vaSpherePrimitiveDestroy(P); }
	for (VAPrismPrimitive*   P : PrismPrimitives)   { vaWorldRemovePrimitive_(World, P); vaPrismPrimitiveDestroy(P); }

	MeshPrimitives.Empty();
	CapsulePrimitives.Empty();
	SpherePrimitives.Empty();
	PrismPrimitives.Empty();
}
