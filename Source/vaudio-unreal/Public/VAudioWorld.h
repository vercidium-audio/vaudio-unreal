#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Sound/SoundSubmix.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "VAudioWorld.generated.h"

struct VAWorld;
struct VAMeshPrimitive;
struct VACapsulePrimitive;
struct VASpherePrimitive;
struct VAPrismPrimitive;
class AVAudioEmitterBase;
class AVAudioListener;
class UVAudioMaterialAssetBase;
class USubmixEffectDirectionalPanPreset;

UCLASS(NotBlueprintType, NotBlueprintable)
class VAUDIOUNREAL_API UVAudioWorldBoundsComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
};

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

struct FVAudioPrimitiveBinding
{
	TWeakObjectPtr<USceneComponent> Component;
	void* Primitive = nullptr;
	EVAudioPrimitiveKind Kind = EVAudioPrimitiveKind::Mesh;
	FTransform LocalOffset = FTransform::Identity;

	FVector LocalExtent = FVector::ZeroVector;

	FDelegateHandle Handle;
};

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

UCLASS(DisplayName = "VAudio World", HideCategories = (Shape, Collision, Rendering, Physics, HLOD, Navigation, VirtualTexture, Tags, Cooking, LOD, AssetUserData, Mobile, RayTracing))
class VAUDIOUNREAL_API AVAudioWorld : public AActor
{
	GENERATED_BODY()

public:
	AVAudioWorld();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	virtual void Tick(float DeltaTime) override;

	// --- World bounds ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	FVector WorldPosition = FVector(-3000.f, -3000.f, -3000.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	FVector WorldSize = FVector(6000.f, 6000.f, 6000.f);

	UPROPERTY(VisibleInstanceOnly, Category = "Vercidium Audio|World", meta = (AllowPrivateAccess = "true"))
	UVAudioWorldBoundsComponent* WorldBounds;

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

	// --- Debug ---

	// Whether to render the raytraced world to a separate debug window.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug")
	bool bRenderingEnabled = true;

	// Whether to render the raytraced world to a separate debug window.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|CameraSpeed", meta = (ClampMin = "0.01", ClampMax="1000"))
	float CameraSpeed = 10;

	// --- Mode ---

	// Silence all direct audio but keep reverb submix sends active.
	// Useful for auditioning the acoustic response of a space in isolation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	bool bReverbOnly = false;

	// --- Debug ---

	UFUNCTION(CallInEditor, Category = "Vercidium Audio", meta = (DisplayName = "Export World"))
	void ExportWorld();

	// --- Baked geometry (shipping fallback) ---

#if WITH_EDITOR
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

	static TArray<TWeakObjectPtr<AVAudioWorld>> RunningWorlds;

	// --- Internal API used by AVAudioEmitterBase subclasses ---

	void InitializeVAWorld();

	// Repositions/resizes WorldBounds from the current WorldPosition/WorldSize. Called from the
	// constructor and PostEditChangeProperty whenever either property changes in the editor.
	void RefreshWorldBounds();

	// Update the vaWorld with the latest properties
	void UpdateVAWorld();

	VAWorld* GetVAWorld() const { return World; }
	USoundSubmix* GetGroupedEAXSubmix(int32 Index) const;
	USubmixEffectReverbPreset* GetGroupedEAXPreset(int32 Index) const;
	int32 GetGroupedEAXPresetCount() const { return GroupedEAXPresets.Num(); }
	int32 GetMaximumGroupedEAXCount() const { return GroupedEAXSubmixes.Num(); }
	void RegisterEmitter(AVAudioEmitterBase* Emitter);
	void UnregisterEmitter(AVAudioEmitterBase* Emitter);

	AVAudioListener* GetMainListener();

private:
	VAWorld* World = nullptr;

	// Transient: populated in BeginPlay from NewObject() and must never be saved into the level —
	// saving these as real exports corrupts the package (they don't round-trip through a reload).
	UPROPERTY(Transient)
	TArray<USubmixEffectReverbPreset*> GroupedEAXPresets;

	UPROPERTY(Transient)
	TArray<USubmixEffectDirectionalPanPreset*> GroupedEAXPanPresets;

	TArray<VAMeshPrimitive*>    MeshPrimitives;
	TArray<VACapsulePrimitive*> CapsulePrimitives;
	TArray<VASpherePrimitive*>  SpherePrimitives;
	TArray<VAPrismPrimitive*>   PrismPrimitives;

	TArray<FVAudioPrimitiveBinding> PrimitiveBindings;

	TMap<USceneComponent*, TArray<int32>> PrimitiveBindingsByComponent;

	TArray<AVAudioEmitterBase*> RegisteredEmitters;

	// Cached from RegisteredEmitters whenever an AVAudioListener is (un)registered, so
	// GetMainListener() and Tick() don't need to scan every frame.
	TWeakObjectPtr<AVAudioListener> MainListener;

	TArray<FString> ActorsWithInvalidMaterials;

	// Mirrors bReverbOnly as of the last Tick(), so the dry-output loop over RegisteredEmitters
	// only runs on the tick where it actually changes.
	bool bWasReverbOnly = false;

	void InitialiseMaterials();
	void ScanAndAddPrimitives();
	void DestroyPrimitives();
	void ApplyGroupedEAXReverb();

	bool TryAddPrimitive(void* Primitive, const TCHAR* PrimitiveTypeName, const FString& ActorName);

	void BindPrimitiveToComponent(void* Primitive, EVAudioPrimitiveKind Kind, USceneComponent* Component,
		const FTransform& LocalOffset = FTransform::Identity, const FVector& LocalExtent = FVector::ZeroVector);

	static void RefreshPrimitiveTransform(const FVAudioPrimitiveBinding& Binding);

	void OnPrimitiveComponentMoved(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	// Unbinds every TransformUpdated delegate registered in PrimitiveBindings and empties it.
	// Called from DestroyPrimitives().
	void UnbindPrimitiveComponents();
};
