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
struct VAMesh;
class AVAEmitter;
class AVAListener;
class UVAMaterialBase;
class UVACustomMaterial;
class UVAMaterialComponent;
class UShapeComponent;
class UStaticMeshComponent;
class UStaticMesh;
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

// Identifies triangle data that every placement of a static mesh can share
struct FVAMeshKey
{
	TWeakObjectPtr<UStaticMesh> Mesh;

	// The render LOD, or 0 for a convex hull
	int32 LOD = 0;

	// A convex hull's index in the mesh's simple collision, or INDEX_NONE for the render triangles
	int32 ConvexIndex = INDEX_NONE;

	bool operator==(const FVAMeshKey& other) const { return Mesh == other.Mesh && LOD == other.LOD && ConvexIndex == other.ConvexIndex; }
	friend uint32 GetTypeHash(const FVAMeshKey& key) { return HashCombine(GetTypeHash(key.Mesh), HashCombine(GetTypeHash(key.LOD), GetTypeHash(key.ConvexIndex))); }
};

struct FVASharedMesh
{
	VAMesh* Mesh = nullptr;
	int32 ReferenceCount = 0;
};

struct FVAPrimitiveBinding
{
	TWeakObjectPtr<USceneComponent> Component;

	// The material component this primitive's material came from, on the component's own actor or inherited from an attach parent
	TWeakObjectPtr<UVAMaterialComponent> Source;

	void* Primitive = nullptr;
	EVAPrimitiveKind Kind = EVAPrimitiveKind::Mesh;

	// The shared mesh a Mesh primitive was created from
	FVAMeshKey MeshKey;

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

UENUM(BlueprintType)
enum class EVABoundsFollow : uint8
{
	// The actor's location is the bounds' minimum corner
	None,

	// The bounds are centred on the current listener during play
	Listener,
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

	// The bounds are always an axis-aligned box, so the rotate and scale gizmos do nothing
	virtual void EditorApplyRotation(const FRotator& DeltaRotation, bool bAltDown, bool bShiftDown, bool bCtrlDown) override {}
	virtual void EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown) override {}
#endif

public:
	virtual void Tick(float DeltaTime) override;

	// --- World bounds ---

	// Size of the raytraced box. The actor's location is the box's minimum corner, like Godot's VAWorld, and its rotation and scale are ignored
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World", meta = (ClampMin = "0.0001"))
	FVector BoundsSize = FVector(6000.f, 6000.f, 6000.f);

	// What the bounds follow during play. With Listener, set BoundsSize to cover the area that's loaded around the player, e.g. twice the World Partition loading range, and the actor's location only places the box in the editor and until a listener begins play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	EVABoundsFollow BoundsFollow = EVABoundsFollow::None;

	// How far the actor (or the current listener, with BoundsFollow Listener) must move during play, in world units, before the bounds follow it. Every move of the bounds re-checks all raytraced trails, so moving bounds should update in steps. The default is 10 m at the default MetersPerUnit. 0 follows every move
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World", meta = (ClampMin = "0.0"))
	float BoundsUpdateDistance = 1000.0f;

	// The bounds' minimum corner: the position last pushed to the SDK during play, otherwise the actor's location
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	FVector GetBoundsPosition() const { return World ? BoundsPosition : GetActorLocation(); }

	// Changes BoundsSize during play
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio")
	void SetBoundsSize(const FVector& size);

	// Changes BoundsFollow during play, moving the bounds straight away
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio")
	void SetBoundsFollow(EVABoundsFollow follow);

	UPROPERTY(VisibleInstanceOnly, Category = "Vercidium Audio|World", meta = (AllowPrivateAccess = "true"))
	UVAWorldBoundsComponent* WorldBounds;

	// Colour of the bounds box drawn in the editor
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vercidium Audio|World")
	FColor BoundsColor = FColor(223, 149, 157, 255);

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

	// Maximum number of background threads used for raytracing. 0 uses one less than the number of logical cores
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Threading", meta = (ClampMin = "0", UIMax = "32"))
	int32 MaximumConcurrencyLevel = 0;

	// When true, stops submitting work to background threads. Safe to destroy the world once threads have drained.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Threading")
	bool bPendingShutdown = false;

	// --- Debug ---

	// Whether to render the raytraced world to a separate debug window.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug")
	bool bRenderingEnabled = true;

	// Free-fly speed of the debug window's camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug", meta = (ClampMin = "0.01", ClampMax="1000"))
	float CameraSpeed = 10;

	// While simulating or ejected from PIE, the debug window's camera follows the level editor viewport instead of the current listener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug")
	bool bSyncViewport = true;

	// Shows emitter, grouped EAX and timing status lines on screen every tick
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Debug")
	bool bShowDebugMessages = true;

	// --- Mode ---

	// Silence all direct audio but keep reverb submix sends active.
	// Useful for auditioning the acoustic response of a space in isolation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|World")
	bool bReverbOnly = false;

	// --- Debug ---

	// Exports to vaudio_export.va in the project directory
	UFUNCTION(CallInEditor, Category = "Vercidium Audio", meta = (DisplayName = "Export World"))
	void ExportWorld();

	// Writes the world's settings, materials, primitives and emitters to a file (dev SDK build only). A relative path is relative to the project directory
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio")
	bool ExportToFile(const FString& Path);

	// --- Stats ---

	// Average milliseconds vaWorldUpdate spends on the game thread
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Stats")
	double GetMainThreadTime() const;

	// Average milliseconds spent in the preparation thread
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Stats")
	double GetPreparationTime() const;

	// Average milliseconds spent in the raytracing threads
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Stats")
	double GetRaytracingTime() const;

	// Average milliseconds spent in the analysis thread
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Stats")
	double GetAnalysisTime() const;

	// Number of grouped EAX reverbs the SDK produced in the last raytracing pass
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Stats")
	int32 GetGroupedEAXCount() const;

	// The grouped EAX reverb's values, or 0 if Index is out of range
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Stats")
	float GetGroupedEAXGainLF(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Stats")
	float GetGroupedEAXGainHF(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Vercidium Audio|Stats")
	float GetGroupedEAXDecayTime(int32 Index) const;

	// --- Baked geometry (shipping fallback) ---

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "Vercidium Audio", meta = (DisplayName = "Bake Geometry For Shipping"))
	void BakeGeometry();
#endif

	// Populated by BakeGeometry and saved with the level. Used as a fallback source of triangle data when the live mesh's render data is unavailable
	UPROPERTY(VisibleAnywhere, Category = "Vercidium Audio", AdvancedDisplay)
	TArray<FVABakedMesh> BakedMeshes;

	// --- Materials ---

	// Material assets applied to this world on BeginPlay. An asset can be shared by several worlds, as custom material IDs are assigned per world. Use SetMaterials to change this during play from Blueprint
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Materials")
	TArray<UVAMaterialBase*> Materials;

	// Changes Materials during play. Added materials are applied, removed default materials restore the SDK's built-in values, and every primitive is rebuilt. Custom materials can't be removed at runtime
	UFUNCTION(BlueprintCallable, Category = "Vercidium Audio")
	void SetMaterials(const TArray<UVAMaterialBase*>& newMaterials);

	// The SDK ID this world assigned to a custom material, or 0 if it hasn't applied it
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	int32 GetCustomMaterialId(const UVACustomMaterial* material) const;

	// Whether geometry can use this material asset: it's in Materials, or it's a custom material this world applied before it was removed
	bool HasMaterial(const UVAMaterialBase* material) const;

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

	// How many distinct meshes the mesh primitives were built from. Every placement of the same static mesh shares one
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	int32 GetSharedMeshCount() const { return SharedMeshes.Num(); }

	// Called by UVAMaterialComponent. Adds the geometry of its actor, and of attached child actors without their own VAMaterialComponent
	void AddMaterialPrimitives(UVAMaterialComponent* source);
	void RemoveMaterialPrimitives(UVAMaterialComponent* source);

	static TArray<TWeakObjectPtr<AVAWorld>> RunningWorlds;

	// The VAWorld in the context object's level, once it has begun play. Every VA actor and component uses this, so none of them need a reference to it
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio", meta = (WorldContext = "WorldContextObject"))
	static AVAWorld* Find(const UObject* WorldContextObject);

	// --- Internal API used by emitters ---

	void InitializeVAWorld();

	// Repositions/resizes WorldBounds from GetBoundsPosition() and BoundsSize
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

	// The bounds' minimum corner last pushed to the SDK
	FVector BoundsPosition = FVector::ZeroVector;

	// Where BoundsFollow wants the bounds' minimum corner. Following a listener while there is none leaves the bounds where they are
	FVector GetBoundsTarget() const;

	// Moves the bounds to GetBoundsTarget()
	void PlaceBounds();

	// PlaceBounds once the target is BoundsUpdateDistance from the bounds
	void FollowBounds();

	// Transient: populated in BeginPlay from NewObject() and must never be saved into the level —
	// saving these as real exports corrupts the package (they don't round-trip through a reload).
	UPROPERTY(Transient)
	TArray<USubmixEffectReverbPreset*> GroupedEAXPresets;

	UPROPERTY(Transient)
	TArray<UVASubmixEffectDirectionalPanPreset*> GroupedEAXPanPresets;

	TArray<FVAPrimitiveBinding> PrimitiveBindings;

	TMap<USceneComponent*, TArray<int32>> PrimitiveBindingsByComponent;

	// One SDK mesh per static mesh LOD or convex hull, shared by every mesh primitive placed from it and destroyed when the last one goes
	TMap<FVAMeshKey, FVASharedMesh> SharedMeshes;

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

	// Custom material IDs (1000 and up) assigned by this world, like Godot's VAWorld.custom_materials. Removed custom materials keep their ID, so re-adding one reuses it
	TMap<TWeakObjectPtr<const UVACustomMaterial>, int32> CustomMaterialIds;

	// The materials applied by the last SyncMaterials, to find removed ones
	UPROPERTY(Transient)
	TArray<TObjectPtr<UVAMaterialBase>> AppliedMaterials;

	// Applies Materials to the SDK world, and undoes materials removed since the last call. Returns whether anything was added or removed
	bool SyncMaterials();
	void DestroyPrimitives();

	// Moves the debug window's camera to the level editor viewport (bSyncViewport) or the current listener
	void SyncDebugCamera();

	void ShowDebugMessages();
	// Pushes the listener and grouped EAX reverb to their submixes
	void OnReverbUpdated();

	// Adds the actor's geometry, then recurses into attached children that don't have their own VAMaterialComponent
	void AddActorTree(AActor* actor, UVAMaterialComponent* source);
	void AddActorPrimitives(AActor* actor, UVAMaterialComponent* source);
	void AddShapePrimitive(UShapeComponent* shape, UVAMaterialComponent* source, int32 materialId);
	void AddStaticMeshPrimitives(UStaticMeshComponent* meshComponent, UVAMaterialComponent* source, const FTransform& meshTransform, int32 materialId, bool useFlatTransmission);
	// readVertices is only called when no placement of this mesh exists yet. It fills the triangle list in mesh space, or returns false if there is none
	bool AddMeshPrimitive(const FVAMeshKey& key, TFunctionRef<bool(TArray<FVector3f>&)> readVertices, UStaticMeshComponent* meshComponent, UVAMaterialComponent* source, const FTransform& meshTransform, int32 materialId, bool useFlatTransmission);
	bool PassesCollisionFilter(const UPrimitiveComponent* component) const;

	// Sets the primitive's transform, adds it to the vaWorld and tracks its component's movement. Destroys it if the SDK rejects it
	bool AddBinding(FVAPrimitiveBinding binding, const TCHAR* typeName);

	void RemoveBindings(TFunctionRef<bool(const FVAPrimitiveBinding&)> predicate);
	void DestroyPrimitive(const FVAPrimitiveBinding& binding);
	VAMesh* AcquireSharedMesh(const FVAMeshKey& key, TFunctionRef<bool(TArray<FVector3f>&)> readVertices, const UStaticMeshComponent* meshComponent);
	void ReleaseSharedMesh(const FVAMeshKey& key);
	static void RefreshPrimitiveTransform(const FVAPrimitiveBinding& Binding);

	void OnPrimitiveComponentMoved(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	// The bounds follow the actor in the editor, and during play unless they follow the listener
	void OnRootMoved(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	// Bound to the OnEndPlay of every actor that contributed primitives, so destroyed or streamed-out geometry stops affecting raytracing
	UFUNCTION()
	void OnGeometryActorEndPlay(AActor* actor, EEndPlayReason::Type endPlayReason);

};
