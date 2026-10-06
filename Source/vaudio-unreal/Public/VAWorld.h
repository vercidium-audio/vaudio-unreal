#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "Sound/SoundSubmix.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "VAWorld.generated.h"

struct VAWorld;
struct VAEmitter;
class AVAEmitter;
class AVAListener;
class UVAMaterialBase;
class UVAMaterialComponent;
class UShapeComponent;
class UStaticMeshComponent;
enum class EVAPropagateMode : uint8;
class UVASubmixEffectDirectionalPanPreset;

UCLASS(NotBlueprintType, NotBlueprintable)
class VAUDIOUNREAL_API UVAWorldBoundsComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
};

enum class EVAPrimitiveKind : uint8
{
	Mesh,
	Capsule,
	Sphere,
	Prism,
	CapsuleFromMesh,
	SphereFromMesh,
	PrismFromMesh,
};

struct FVAPrimitiveBinding
{
	TWeakObjectPtr<USceneComponent> Component;

	// The material component this primitive's material came from, on the component's own actor or inherited from an attach parent
	TWeakObjectPtr<UVAMaterialComponent> Source;

	void* Primitive = nullptr;
	EVAPrimitiveKind Kind = EVAPrimitiveKind::Mesh;

	// Mesh space relative to the component, i.e. an instanced static mesh's instance transform. Identity otherwise
	FTransform MeshTransform = FTransform::Identity;

	// A simple collision element's rotation and translation in mesh space
	FTransform ElementTransform = FTransform::Identity;

	// A simple collision element's unscaled size
	FVector LocalExtent = FVector::ZeroVector;

	FDelegateHandle Handle;
};

USTRUCT()
struct FVABakedMesh
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

UCLASS(DisplayName = "VAWorld", HideCategories = (Shape, Collision, Rendering, Physics, HLOD, Navigation, VirtualTexture, Tags, Cooking, LOD, AssetUserData, Mobile, RayTracing))
class VAUDIOUNREAL_API AVAWorld : public AActor
{
	GENERATED_BODY()

public:
	AVAWorld();

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
	UVAWorldBoundsComponent* WorldBounds;

	// --- Physics ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World", meta = (ClampMin = "0.0001", Delta = "0.001"))
	float MetersPerUnit = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World", meta = (ClampMin = "0.0001", Delta = "1.0"))
	float SpeedOfSound = 343.0f;

	// Epsilon value used for ray offsets, world bounds clamping and line-of-sight tests.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	float Epsilon = 0.01f;

	// --- Air Absorption ---

	// Relative humidity as a percentage.
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

	// One submix per grouped EAX. The SDK assigns each source emitter a grouped EAX index, which selects the submix its audio is sent to. Must have at least 2 entries.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Reverb")
	TArray<USoundSubmix*> GroupedEAXSubmixes;

	// --- Emitters ---

	// Whether emitters outside the world have 0 occlusion/permeation energy (true) or maximum energy (false).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Emitters")
	bool bEmittersOutsideTheWorldAreMuffled = true;

	// Whether occlusion rays should be affected by the world bounds material.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Emitters")
	bool bOcclusionRaysLoseEnergyFromWorldBounds = false;

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

	// Populated by BakeGeometry and saved with the level. Used as a fallback source of triangle data when the live mesh's render data is unavailable
	UPROPERTY(VisibleAnywhere, Category = "Vercidium Audio", AdvancedDisplay)
	TArray<FVABakedMesh> BakedMeshes;

	// --- Materials ---

	// Material assets applied to this world on BeginPlay. Each asset is only ever used by one
	// world - not shared across worlds/levels.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Materials")
	TArray<UVAMaterialBase*> Materials;

	// Only colliders (shape components and static mesh simple collision) whose collision object type is listed here become raytracing geometry, like Godot's VAWorld.collision_layers. Empty adds every collider
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Materials")
	TArray<TEnumAsByte<ECollisionChannel>> CollisionObjectTypes;

	// Rebuilds an actor's raytracing geometry and that of its attached children, e.g. after changing its meshes or instances, or attaching it to or detaching it from an actor with a VAMaterialComponent
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio")
	void SyncPrimitive(AActor* actor);

	// Changes CollisionObjectTypes during play, rebuilding every primitive
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio")
	void SetCollisionObjectTypes(const TArray<TEnumAsByte<ECollisionChannel>>& objectTypes);

	// Rebuilds the geometry of every VAMaterialComponent in this world
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio")
	void RebuildPrimitives();

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	int32 GetPrimitiveCount() const { return PrimitiveBindings.Num(); }

	// Called by UVAMaterialComponent. Adds the geometry of its actor, and of attached child actors without their own VAMaterialComponent
	void AddMaterialPrimitives(UVAMaterialComponent* source);
	void RemoveMaterialPrimitives(UVAMaterialComponent* source);

	static TArray<TWeakObjectPtr<AVAWorld>> RunningWorlds;

	// The VAWorld in the context object's level, once it has begun play. Every VA actor and component uses this, so none of them need a reference to it
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio", meta = (WorldContext = "WorldContextObject"))
	static AVAWorld* Find(const UObject* WorldContextObject);

	// --- Internal API used by emitters ---

	void InitializeVAWorld();

	// Repositions/resizes WorldBounds from the current WorldPosition/WorldSize. Called from the
	// constructor and PostEditChangeProperty whenever either property changes in the editor.
	void RefreshWorldBounds();

	// Update the vaWorld with the latest properties
	void UpdateVAWorld();

	VAWorld* GetVAWorld() const { return World; }
	USoundSubmix* GetGroupedEAXSubmix(int32 Index) const;
	USubmixEffectReverbPreset* GetGroupedEAXPreset(int32 Index) const;

	// The pan last pushed to this grouped EAX's submix, from -1 (left) to 1 (right) relative to the player's audio listener
	float GetGroupedEAXPan(int32 Index) const;
	int32 GetGroupedEAXPresetCount() const { return GroupedEAXPresets.Num(); }
	int32 GetMaximumGroupedEAXCount() const { return GroupedEAXSubmixes.Num(); }
	// Adds a non-listener emitter to the vaWorld. It automatically becomes a target of the current listener, whichever of the two begins play first. Returns false (and logs why) if the SDK rejected it
	bool RegisterEmitter(AVAEmitter* emitter);
	void UnregisterEmitter(AVAEmitter* emitter);

	// Calls vaWorldAddEmitter on the emitter's handle. Returns false (and logs why) if the SDK rejected it
	bool AddEmitterToWorld(AVAEmitter* emitter);

	// Every listener in a world shares one SDK emitter. The first listener creates it, and it's handed over whenever the current listener changes, so targets stay connected. Returns false if the SDK rejected the first listener's handle
	bool RegisterListener(AVAListener* listener);

	// Promotes the first remaining listener if this one was current. The last listener takes the shared handle with it
	void UnregisterListener(AVAListener* listener);

	bool SetCurrentListener(AVAListener* listener);

	// Hands the shared handle to another listener. The only listener in the world stays current
	void ReleaseCurrentListener(AVAListener* listener);

	// The current listener
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	AVAListener* GetMainListener() const { return MainListener; }

	const TArray<AVAListener*>& GetListeners() const { return Listeners; }
	const TArray<AVAEmitter*>& GetRegisteredEmitters() const { return RegisteredEmitters; }

	// Called from the OnRemoved callback, after which the raytracing threads no longer read the emitter. Destroyed after the next vaWorldUpdate returns
	void DeferEmitterDestroy(VAEmitter* handle) { PendingEmitterDestroys.Add(handle); }

	// For a handle whose actor ended play while its removal was still pending (reverb tail). The world destroys it once OnRemoved fires, or when the world ends
	void AddOrphanedEmitter(VAEmitter* handle) { OrphanedEmitters.Add(handle, this); }
	static void OnOrphanedEmitterRemoved(VAEmitter* handle);

	int32 GetOrphanedEmitterCount() const;
	int32 GetPendingEmitterDestroyCount() const { return PendingEmitterDestroys.Num(); }

	// Called from the SDK callbacks, so FlushPendingEvents runs after vaWorldUpdate returns
	void QueueEmitterEvents(AVAEmitter* emitter);

	// Number of completed raytracing passes, so tests can wait for fresh results after changing the scene
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	int32 GetRaytraceCount() const { return RaytraceCount; }

private:
	VAWorld* World = nullptr;

	// Transient: populated in BeginPlay from NewObject() and must never be saved into the level —
	// saving these as real exports corrupts the package (they don't round-trip through a reload).
	UPROPERTY(Transient)
	TArray<USubmixEffectReverbPreset*> GroupedEAXPresets;

	UPROPERTY(Transient)
	TArray<UVASubmixEffectDirectionalPanPreset*> GroupedEAXPanPresets;

	TArray<FVAPrimitiveBinding> PrimitiveBindings;

	TMap<USceneComponent*, TArray<int32>> PrimitiveBindingsByComponent;

	// Every material component that has begun play in this world, including those whose geometry was all filtered out, so RebuildPrimitives can re-add them
	TSet<TWeakObjectPtr<UVAMaterialComponent>> MaterialSources;

	// Non-listener emitters, which are all targets of the main listener. Raw pointers are safe as emitters unregister in EndPlay
	TArray<AVAEmitter*> RegisteredEmitters;

	// Every listener that has begun play, current or not. Raw pointers are safe as listeners unregister in EndPlay
	TArray<AVAListener*> Listeners;

	// The current listener, which holds the shared SDK handle
	AVAListener* MainListener = nullptr;

	TArray<VAEmitter*> PendingEmitterDestroys;
	static TMap<VAEmitter*, AVAWorld*> OrphanedEmitters;

	TArray<TWeakObjectPtr<AVAEmitter>> PendingEventEmitters;

	int32 RaytraceCount = 0;
	static void OnReverbUpdatedTrampoline(VAWorld* world);

	void WirePendingTargets();
	void DestroyRemovedEmitters();

	TArray<FString> ActorsWithInvalidMaterials;

	// Mirrors bReverbOnly as of the last Tick(), so the dry-output loop over RegisteredEmitters
	// only runs on the tick where it actually changes.
	bool bWasReverbOnly = false;

	void InitialiseMaterials();
	void DestroyPrimitives();
	// Pushes the listener and grouped EAX reverb to their submixes
	void OnReverbUpdated();

	// Adds the actor's geometry, then recurses into attached children that don't have their own VAMaterialComponent
	void AddActorTree(AActor* actor, UVAMaterialComponent* source);
	void AddActorPrimitives(AActor* actor, UVAMaterialComponent* source);
	void AddShapePrimitive(UShapeComponent* shape, UVAMaterialComponent* source, int32 materialId);
	void AddStaticMeshPrimitives(UStaticMeshComponent* meshComponent, UVAMaterialComponent* source, const FTransform& meshTransform, int32 materialId, bool useFlatTransmission);
	bool AddMeshPrimitive(const TArray<FVector3f>& vertices, UStaticMeshComponent* meshComponent, UVAMaterialComponent* source, const FTransform& meshTransform, int32 materialId, bool useFlatTransmission);
	bool PassesCollisionFilter(const UPrimitiveComponent* component) const;

	// Sets the primitive's transform, adds it to the vaWorld and tracks its component's movement. Destroys it if the SDK rejects it
	bool AddBinding(FVAPrimitiveBinding binding, const TCHAR* typeName);

	void RemoveBindings(TFunctionRef<bool(const FVAPrimitiveBinding&)> predicate);
	static void DestroyPrimitive(void* primitive, EVAPrimitiveKind kind);
	static void RefreshPrimitiveTransform(const FVAPrimitiveBinding& Binding);

	void OnPrimitiveComponentMoved(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	// Bound to the OnEndPlay of every actor that contributed primitives, so destroyed or streamed-out geometry stops affecting raytracing
	UFUNCTION()
	void OnGeometryActorEndPlay(AActor* actor, EEndPlayReason::Type endPlayReason);

};
