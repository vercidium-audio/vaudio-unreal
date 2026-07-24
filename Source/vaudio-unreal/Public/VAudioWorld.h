#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Sound/SoundSubmix.h"
#include "Components/SceneComponent.h"
#include "VAudioWorld.generated.h"

struct VAWorld;
struct VAMeshPrimitive;
struct VACapsulePrimitive;
struct VASpherePrimitive;
struct VAPrismPrimitive;
class AVAudioEmitterBase;
class AVAudioListener;
class UVAudioMaterialAssetBase;

// Which VAXPrimitiveSet* calls RefreshPrimitiveTransform() should use for a given entry - there's
// no common base type across VAMeshPrimitive/VACapsulePrimitive/etc, so the primitive pointer is
// stored as void* and this tag says how to interpret it.
//
// Capsule/Sphere/Prism (from a UShapeComponent directly) re-read their size live from that
// component's own GetScaledCapsuleRadius()/GetScaledSphereRadius()/GetScaledBoxExtent(), which
// already account for the component's current scale. CapsuleFromMesh/SphereFromMesh/PrismFromMesh
// (simple collision baked from a UStaticMeshComponent's body setup, which has no such per-element
// accessor) instead re-derive their size from LocalExtent scaled by the component's current
// GetComponentTransform().GetScale3D(), matching the FMath::Max(Scale.X,Scale.Y)/GetAbsMax()/
// per-axis conventions ScanAndAddPrimitives originally used for sphyl/sphere/box respectively.
enum class EVAudioPrimitiveKind : uint8
{
	Mesh,
	Capsule,
	Sphere,
	Prism,
	CapsuleFromMesh,
	SphereFromMesh,
	PrismFromMesh,
};

// Tracks the live link between a moving USceneComponent (owned by some other actor in the level)
// and the VA primitive ScanAndAddPrimitives() created from its shape/mesh at BeginPlay. Bound to
// Component's TransformUpdated delegate so the primitive's transform (and, for shape primitives,
// its scale-derived size) is kept in sync whenever that actor moves or rotates at runtime.
//
// LocalOffset is this primitive's transform relative to Component - identity for a primitive
// built directly from a UShapeComponent, or a UStaticMeshComponent's per-element sphyl/sphere/box
// offset (see FKSphylElem::GetTransform() etc in ScanAndAddPrimitives) for simple collision baked
// from a mesh's body setup. The primitive's live world transform is always LocalOffset composed
// with Component's current world transform, recomputed from scratch on every move rather than
// incrementally, so drift can never accumulate.
struct FVAudioPrimitiveBinding
{
	TWeakObjectPtr<USceneComponent> Component;
	void* Primitive = nullptr;
	EVAudioPrimitiveKind Kind = EVAudioPrimitiveKind::Mesh;
	FTransform LocalOffset = FTransform::Identity;

	// Unscaled local radius/length/size, as read from the FKSphylElem/FKSphereElem/FKBoxElem at
	// scan time - only used by the …FromMesh kinds (see EVAudioPrimitiveKind comment above).
	// Meaning depends on Kind: CapsuleFromMesh uses X=radius, Z=length; SphereFromMesh uses
	// X=radius; PrismFromMesh uses X/Y/Z=full size (not half-extent).
	FVector LocalExtent = FVector::ZeroVector;

	FDelegateHandle Handle;
};

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
UCLASS(DisplayName = "VAudio World")
class VAUDIOUNREAL_API AVAudioWorld : public AActor
{
	GENERATED_BODY()

public:
	AVAudioWorld();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	// Re-applies World/Physics/AirAbsorption/Threading/Emitters settings below when edited live via
	// the details panel during PIE - without this, BeginPlay()'s one-shot vaWorldSet* calls mean
	// edits made after play-start would otherwise silently have no effect. Mirrors the pattern used
	// by AVAudioListener::PostEditChangeProperty.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	virtual void Tick(float DeltaTime) override;

	// --- World bounds ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	FVector WorldPosition = FVector(-3000.f, -3000.f, -3000.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	FVector WorldSize = FVector(6000.f, 6000.f, 6000.f);

	// --- Physics ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World", meta = (ClampMin = "0.0001", Delta = "0.001"))
	float MetersPerUnit = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World", meta = (ClampMin = "0.0001", Delta = "1.0"))
	float SpeedOfSound = 343.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	bool bIsIndoors = false;

	// Epsilon value used for ray offsets, world bounds clamping and line-of-sight tests.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	float Epsilon = 0.01f;

	// --- Air Absorption ---

	// Relative humidity as a percentage (0–1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.01"))
	float Humidity = 0.1f;

	// Air temperature in degrees Celsius.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption", meta = (ClampMin="-273.151", Delta = "1.0"))
	float Temperature = 26.0f;

	// Atmospheric pressure in Pascals.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption", meta = (ClampMin = "0.0", Delta = "10.0"))
	float Pressure = 101325.0f;

	// Whether air absorption is applied at all. When false, Humidity/Temperature/Pressure below are ignored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption")
	bool bAirAbsorptionEnabled = true;

	// Low-frequency reference (Hz) for air absorption, reverb, and material scattering.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption", meta = (ClampMin = "0.0001", Delta = "1.0"))
	float ReferenceFrequencyLF = 300.0f;

	// High-frequency reference (Hz) for air absorption, reverb, and material scattering.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|AirAbsorption", meta = (ClampMin = "0.0001", Delta = "1.0"))
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

	// --- Internal API used by AVAudioEmitterBase subclasses ---

	// Creates World and applies all vaWorldSet* settings/materials/primitives if this hasn't already
	// run - safe to call repeatedly (e.g. from an AVAudioEmitterBase whose own BeginPlay ran before
	// this world's, since actor BeginPlay order is not guaranteed). Called from BeginPlay() as well
	// as AVAudioEmitterBase::TryInitializeEmitter().
	void InitializeVAWorld();

	VAWorld* GetVAWorld() const { return World; }
	USoundSubmix* GetGroupedEAXSubmix(int32 Index) const;
	USubmixEffectReverbPreset* GetGroupedEAXPreset(int32 Index) const;
	int32 GetGroupedEAXPresetCount() const { return GroupedEAXPresets.Num(); }
	int32 GetMaximumGroupedEAXCount() const { return GroupedEAXSubmixes.Num(); }
	void RegisterEmitter(AVAudioEmitterBase* Emitter);
	void UnregisterEmitter(AVAudioEmitterBase* Emitter);

	// Returns the first registered AVAudioListener ("first one wins", warns on duplicates - see
	// RegisterEmitter()). If none is registered yet - e.g. the listener is a child actor of
	// something whose BeginPlay hasn't run yet, so the listener's own BeginPlay hasn't either -
	// finds it in the level and force-initialises it before returning (see TryInitializeEmitter()).
	AVAudioListener* GetMainListener();

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

	// One entry per primitive created in ScanAndAddPrimitives, so its transform can be kept in
	// sync if the owning component moves at runtime - see BindPrimitiveToComponent()/
	// OnPrimitiveComponentMoved(). A single component can own more than one primitive (e.g. a
	// static mesh's simple collision can contain several sphyl/sphere/box elements).
	TArray<FVAudioPrimitiveBinding> PrimitiveBindings;

	TArray<AVAudioEmitterBase*> RegisteredEmitters;

	// Cached from RegisteredEmitters whenever an AVAudioListener is (un)registered, so
	// GetMainListener() and Tick() don't need to scan every frame.
	TWeakObjectPtr<AVAudioListener> MainListener;

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
	void ApplyGroupedEAXReverb();

	// Calls vaWorldAddPrimitive_ and checks the result. On failure, logs the actor/primitive/error
	// code and adds ActorName to ActorsWithInvalidMaterials (see Tick()'s on-screen warning) so a
	// rejected primitive is as visible as a material configuration problem. Returns true on success.
	bool TryAddPrimitive(void* Primitive, const TCHAR* PrimitiveTypeName, const FString& ActorName);

	// Binds Component's TransformUpdated delegate so moving/rotating it at runtime updates
	// Primitive's transform (see OnPrimitiveComponentMoved). Called once per primitive right
	// after TryAddPrimitive succeeds for it. LocalOffset/LocalExtent are stored as-is on the
	// binding - see FVAudioPrimitiveBinding's comment for what they mean per Kind.
	void BindPrimitiveToComponent(void* Primitive, EVAudioPrimitiveKind Kind, USceneComponent* Component,
		const FTransform& LocalOffset = FTransform::Identity, const FVector& LocalExtent = FVector::ZeroVector);

	// Recomputes and applies Binding's live world transform (LocalOffset composed with
	// Binding.Component's current world transform) to its SDK primitive. Shared by both the
	// initial ScanAndAddPrimitives() creation and OnPrimitiveComponentMoved() refreshes so the
	// two can never compute it differently.
	static void RefreshPrimitiveTransform(const FVAudioPrimitiveBinding& Binding);

	// Fired by TransformUpdated (a non-dynamic multicast event, bound via AddUObject - see
	// BindPrimitiveToComponent) on any component we bound. Recomputes and re-applies the
	// transform (and, for shape primitives, the scale-derived size) of every primitive bound to
	// UpdatedComponent.
	void OnPrimitiveComponentMoved(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	// Unbinds every TransformUpdated delegate registered in PrimitiveBindings and empties it.
	// Called from DestroyPrimitives().
	void UnbindPrimitiveComponents();
};
