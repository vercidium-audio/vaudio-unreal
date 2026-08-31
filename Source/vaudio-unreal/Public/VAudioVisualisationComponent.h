#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "VAudioVisualisationComponent.generated.h"

struct VAVisualisationData;
class AVAudioEmitterBase;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS(ClassGroup = ("Vercidium Audio"), meta = (BlueprintSpawnableComponent), DisplayName = "VA Visualisation")
class VAUDIOUNREAL_API UVAudioVisualisationComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UVAudioVisualisationComponent();

	UPROPERTY(EditAnywhere, Category = "Vercidium Audio|Visualisation")
	TObjectPtr<UMaterialInterface> DiamondMaterial = nullptr;

	// Number of visualisation rays cast
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 VisualisationRayCount = 0;

	// Number of bounces per visualisation ray
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 VisualisationBounceCount = 0;

	// How often to cast visualisation rays (milliseconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "1"))
	int32 VisualisationUpdateFrequency = 500;

	// How long, in milliseconds, each diamond takes to fade in from transparent to Color's alpha
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 FadeInMilliseconds = 750;

	// How long, in milliseconds, each diamond takes to fade out to transparent at the end of its lifetime
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0"))
	int32 FadeOutMilliseconds = 750;

	// How long, in milliseconds, each diamond remains visible in total, including FadeInMilliseconds and FadeOutMilliseconds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "1"))
	int32 DurationMilliseconds = 1500;

	// Colour of each diamond. Alpha is the maximum opacity reached once a diamond has fully faded in
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation")
	FLinearColor Color = FLinearColor(0.11f, 0.97f, 1.0f, 0.75f);

	// Radius of each diamond, in world units (cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0.01"))
	float Size = 10.0f;

	// How far, in world units, each diamond is pushed off the surface it landed on along the hit
	// normal. Increase if diamonds z-fight with nearby geometry
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0.0"))
	float NormalOffset = 2.0f;

	// Ray bounces further than this distance from the owning emitter are not rendered. 0 = no limit
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Visualisation", meta = (ClampMin = "0.0"))
	float MaxDistance = 2000.0f;

	// Called from the VAEmitterVisualisationCallback trampoline with a batch of ray-bounce
	// results, always on the main thread during vaWorldUpdate() - safe to touch UObjects directly
	void OnVisualisationData(VAVisualisationData* data, int32 count);

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "Vercidium Audio|Visualisation")
	void GenerateFadeNodes();
#endif

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void DestroyComponent(bool bPromoteChildren) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> InstancedMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DiamondMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DiamondMaterialInstance = nullptr;

	AVAudioEmitterBase* OwnerEmitter = nullptr;

	int32 NextInstance = 0;

	bool bCallbackRegistered = false;

	int32 GetRequiredInstanceCount() const;

	static UStaticMesh* BuildDiamondMesh();

	void CreateInstancedMesh();
	void ApplyMaterialParameters();
	void ApplyVisualisationSettings() const;
	void TeardownVisualisation();

	void ValidateDiamondMaterial() const;

	void DisplayWarning(const TCHAR* fmt, ...) const;
	void ClearWarning() const;
	void DisplayMaterialWarning(uint32 offset, const TCHAR* fmt, ...) const;
	void ClearMaterialWarning(uint32 offset) const;
};
