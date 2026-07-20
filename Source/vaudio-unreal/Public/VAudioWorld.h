#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Sound/SoundSubmix.h"
#include "VAudioWorld.generated.h"

struct VAWorld;
struct VAMeshPrimitive;
struct VACapsulePrimitive;
struct VASpherePrimitive;
struct VAPrismPrimitive;
class AVAudioEmitter;
class UVAudioMaterialAssetBase;

// Baked local-space triangle mesh for one UStaticMeshComponent, captured in-editor via
// AVAudioWorld::BakeGeometry so shipping builds don't depend on the mesh's CPU-accessible
// render data (which UStaticMesh::bAllowCPUAccess does not reliably guarantee is retained
// after cooking for every mesh type/pipeline).
USTRUCT()
struct FVAudioBakedMesh
{
	GENERATED_BODY()

	// Owning actor + component name, used to match this entry back to its UStaticMeshComponent at runtime.
	UPROPERTY()
	FString ActorName;

	UPROPERTY()
	FName ComponentName;

	// Local-space (component space) vertex positions, already expanded/duplicated per-index
	// (i.e. Vertices[i] corresponds to triangle-list index i — no separate index array needed).
	UPROPERTY()
	TArray<FVector3f> Vertices;
};

// Place one of these in your level. It owns the VA raytracing world and scans
// for UVAudioMaterialComponent on BeginPlay to populate the scene geometry.
UCLASS(DisplayName = "VA Audio World")
class VAUDIOUNREAL_API AVAudioWorld : public AActor
{
	GENERATED_BODY()

public:
	AVAudioWorld();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// --- World bounds ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	FVector WorldPosition = FVector(-3000.f, -3000.f, -3000.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	FVector WorldSize = FVector(6000.f, 6000.f, 6000.f);

	// --- Physics ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	float MetersPerUnit = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	float SpeedOfSound = 343.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	bool bIsIndoors = false;

	// Epsilon value used for ray offsets, world bounds clamping and line-of-sight tests.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	float Epsilon = 0.01f;

	// --- Air Absorption ---

	// Relative humidity as a percentage (0–1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Humidity = 0.1f;

	// Air temperature in degrees Celsius.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption")
	float Temperature = 26.0f;

	// Atmospheric pressure in Pascals.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption", meta = (ClampMin = "0.0"))
	float Pressure = 101325.0f;

	// Whether air absorption is applied at all. When false, Humidity/Temperature/Pressure below are ignored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption")
	bool bAirAbsorptionEnabled = true;

	// Low-frequency reference (Hz) for air absorption, reverb, and material scattering.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption", meta = (ClampMin = "0.0001"))
	float ReferenceFrequencyLF = 300.0f;

	// High-frequency reference (Hz) for air absorption, reverb, and material scattering.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption", meta = (ClampMin = "0.0001"))
	float ReferenceFrequencyHF = 4000.0f;

	// --- Reverb ---

	// One submix per grouped EAX zone. The SDK assigns each source emitter a zone index;
	// that index selects which submix its audio is sent to. Must have at least 2 entries.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	TArray<USoundSubmix*> GroupedEAXSubmixes;

	// --- Emitters ---

	// Whether emitters outside the world have 0 occlusion/permeation energy (true) or maximum energy (false).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Emitters")
	bool bEmittersOutsideTheWorldAreMuffled = true;

	// --- Threading ---

	// Number of work items to split trails across for load balancing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Threading", meta = (ClampMin = "1"))
	int32 WorkItemCount = 128;

	// Maximum number of background threads used for raytracing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Threading", meta = (ClampMin = "1"))
	int32 MaximumConcurrencyLevel = 8;

	// When true, stops submitting work to background threads. Safe to destroy the world once threads have drained.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Threading")
	bool bPendingShutdown = false;

	// --- Mode ---

	// Silence all direct audio but keep reverb submix sends active.
	// Useful for auditioning the acoustic response of a space in isolation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	bool bReverbOnly = false;

	// --- Debug ---

	UFUNCTION(CallInEditor, Category = "Vercidium Audio", meta = (DisplayName = "Export World"))
	void ExportWorld();

	// Imports world settings, materials, primitives, and emitters from vaudio_export.va into this world.
	// Existing primitives and emitters are not removed before importing.
	UFUNCTION(CallInEditor, Category = "Vercidium Audio", meta = (DisplayName = "Import World"))
	void ImportWorld();

	// --- Baked geometry (shipping fallback) ---

#if WITH_EDITOR
	// Captures the local-space triangle mesh of every UStaticMeshComponent reachable from a
	// UVAudioMaterialComponent actor into BakedMeshes below, so ScanAndAddPrimitives can use it
	// in shipping builds where the live mesh render data may not be CPU-accessible. Re-run this
	// (and save the level) whenever affected meshes or actor placements change.
	UFUNCTION(CallInEditor, Category = "Vercidium Audio", meta = (DisplayName = "Bake Geometry For Shipping"))
	void BakeGeometry();
#endif

	// Populated by BakeGeometry and saved with the level. Used by ScanAndAddPrimitives as a
	// fallback source of triangle data when the live mesh's render data is unavailable.
	UPROPERTY(VisibleAnywhere, Category = "Vercidium Audio", AdvancedDisplay)
	TArray<FVAudioBakedMesh> BakedMeshes;

	// --- Materials ---

	// Material assets applied to this world on BeginPlay. Each asset is only ever used by one
	// world - not shared across worlds/levels.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Materials")
	TArray<UVAudioMaterialAssetBase*> Materials;

	// Every AVAudioWorld currently in play, so a UVAudioMaterialAssetBase's PostEditChangeProperty
	// can find the world(s) referencing it and re-apply live. Populated in BeginPlay, cleared in
	// EndPlay.
	static TArray<TWeakObjectPtr<AVAudioWorld>> RunningWorlds;

	// --- Internal API used by AVAudioEmitter ---

	VAWorld* GetVAWorld() const { return World; }
	USoundSubmix* GetGroupedEAXSubmix(int32 Index) const;
	USubmixEffectReverbPreset* GetGroupedEAXPreset(int32 Index) const;
	int32 GetGroupedEAXPresetCount() const { return GroupedEAXPresets.Num(); }
	int32 GetMaximumGroupedEAXCount() const { return GroupedEAXSubmixes.Num(); }
	void RegisterEmitter(AVAudioEmitter* Emitter);
	void UnregisterEmitter(AVAudioEmitter* Emitter);
	AVAudioEmitter* GetMainListener() const;

private:
	VAWorld* World = nullptr;

	// Transient: populated in BeginPlay from NewObject() and must never be saved into the level —
	// saving these as real exports corrupts the package (they don't round-trip through a reload).
	UPROPERTY(Transient)
	TArray<USubmixEffectReverbPreset*> GroupedEAXPresets;

	TArray<VAMeshPrimitive*>    MeshPrimitives;
	TArray<VACapsulePrimitive*> CapsulePrimitives;
	TArray<VASpherePrimitive*>  SpherePrimitives;
	TArray<VAPrismPrimitive*>   PrismPrimitives;

	TArray<AVAudioEmitter*> RegisteredEmitters;

	// Cached from RegisteredEmitters whenever an emitter with bIsMainListener == true is
	// (un)registered, so GetMainListener() and Tick() don't need to scan every frame.
	TWeakObjectPtr<AVAudioEmitter> MainListener;

	// Actors whose geometry failed to make it into raytracing - either a material configuration
	// problem (e.g. MaterialAsset isn't in this world's Materials array) or vaWorldAddPrimitive_
	// itself rejected a primitive (e.g. VA_ALREADY_EXISTS). Populated by ScanAndAddPrimitives,
	// surfaced as a persistent on-screen warning every Tick() so it isn't missed in the log.
	TArray<FString> ActorsWithInvalidMaterials;

	// Mirrors bReverbOnly as of the last Tick(), so the dry-output loop over RegisteredEmitters
	// only runs on the tick where it actually changes.
	bool bWasReverbOnly = false;

	void ApplyMaterials();
	void ScanAndAddPrimitives();
	void DestroyPrimitives();

	// Calls vaWorldAddPrimitive_ and checks the result. On failure, logs the actor/primitive/error
	// code and adds ActorName to ActorsWithInvalidMaterials (see Tick()'s on-screen warning) so a
	// rejected primitive is as visible as a material configuration problem. Returns true on success.
	bool TryAddPrimitive(void* Primitive, const TCHAR* PrimitiveTypeName, const FString& ActorName);
};
