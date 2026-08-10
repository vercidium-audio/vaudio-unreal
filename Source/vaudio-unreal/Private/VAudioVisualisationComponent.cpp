#include "VAudioVisualisationComponent.h"
#include "VAudioEmitterBase.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

extern "C" {
#include "vaudio.h"
}

#include "VAConstants.h"
#include "VADebugMessageKeys.h"

// vaEmitterSetUserData() stashes the owning actor on the VAEmitter* itself (see
// VAudioEmitterBase.cpp), so this trampoline resolves the actor the same way the other
// callback trampolines do, then forwards to whichever UVAudioVisualisationComponent is
// currently attached to it (see AVAudioEmitterBase::VisualisationComponent).
static void VAVisualisationCallbackTrampoline(VAEmitter* emitter, VAVisualisationData* data, int32 count)
{
	if (AVAudioEmitterBase* Owner = static_cast<AVAudioEmitterBase*>(vaEmitterGetUserData(emitter)))
		if (Owner->VisualisationComponent)
			Owner->VisualisationComponent->OnVisualisationData(data, count);
}

UVAudioVisualisationComponent::UVAudioVisualisationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UVAudioVisualisationComponent::DisplayWarning(const TCHAR* fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	DisplayDebugWarningArgs(VAEmitterMessageBase + GetUniqueID(), fmt, args);
	va_end(args);
}

void UVAudioVisualisationComponent::ClearWarning() const
{
	ClearDebugWarning(VAEmitterMessageBase + GetUniqueID());
}

void UVAudioVisualisationComponent::OnRegister()
{
	Super::OnRegister();

	OwnerEmitter = Cast<AVAudioEmitterBase>(GetOwner());
}

void UVAudioVisualisationComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!OwnerEmitter)
	{
		DisplayWarning(TEXT("[VA] VisualisationComponent on '%s' must be attached to a VAudio emitter actor (Listener, Source, Continuous, AmbientSource) and will not render"), GetOwner() ? *GetOwner()->GetActorNameOrLabel() : TEXT("<none>"));
		return;
	}

	if (!OwnerEmitter->TryInitializeEmitter())
	{
		DisplayWarning(TEXT("[VA] VisualisationComponent on '%s' cannot initialise as its owner has no AudioWorld assigned"), *OwnerEmitter->GetActorNameOrLabel());
		return;
	}

	if (!DiamondMaterial)
	{
		DisplayWarning(TEXT("[VA] VisualisationComponent on '%s' has no DiamondMaterial assigned and will not render"), *OwnerEmitter->GetActorNameOrLabel());
		return;
	}

	CreateInstancedMesh();
	ApplyVisualisationSettings();

	OwnerEmitter->VisualisationComponent = this;
	VAResult result = vaEmitterSetVisualisationCallback(OwnerEmitter->GetVAEmitter(), &VAVisualisationCallbackTrampoline);
	check(result == VA_SUCCESS);
	bCallbackRegistered = true;

	SetComponentTickEnabled(true);
}

void UVAudioVisualisationComponent::TeardownVisualisation()
{
	if (bCallbackRegistered && OwnerEmitter && OwnerEmitter->GetVAEmitter())
	{
		vaEmitterSetVisualisationCallback(OwnerEmitter->GetVAEmitter(), nullptr);
		OwnerEmitter->VisualisationComponent = nullptr;
		bCallbackRegistered = false;
	}

	if (InstancedMesh)
	{
		InstancedMesh->DestroyComponent();
		InstancedMesh = nullptr;
	}

	ClearWarning();
}

void UVAudioVisualisationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TeardownVisualisation();

	Super::EndPlay(EndPlayReason);
}

void UVAudioVisualisationComponent::DestroyComponent(bool bPromoteChildren)
{
	TeardownVisualisation();

	Super::DestroyComponent(bPromoteChildren);
}

void UVAudioVisualisationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (DiamondMaterialInstance)
		DiamondMaterialInstance->SetScalarParameterValue(TEXT("CurrentTime"), GetWorld()->GetTimeSeconds());
}

UStaticMesh* UVAudioVisualisationComponent::BuildDiamondMesh()
{
	// Unit diamond in the local XY plane - orientated per-instance to the ray hit normal via
	// each ISMC instance's transform (see OnVisualisationData). World size is applied entirely
	// via the per-instance transform's scale (Size), so this base mesh is always unit-sized.
	FMeshDescription meshDescription;
	FStaticMeshAttributes attributes(meshDescription);
	attributes.Register();

	TVertexAttributesRef<FVector3f> vertexPositions = attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> vertexNormals = attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> vertexUVs = attributes.GetVertexInstanceUVs();

	FVertexID topVertex = meshDescription.CreateVertex();
	FVertexID rightVertex = meshDescription.CreateVertex();
	FVertexID bottomVertex = meshDescription.CreateVertex();
	FVertexID leftVertex = meshDescription.CreateVertex();

	vertexPositions[topVertex] = FVector3f(0.0f, 1.0f, 0.0f);
	vertexPositions[rightVertex] = FVector3f(1.0f, 0.0f, 0.0f);
	vertexPositions[bottomVertex] = FVector3f(0.0f, -1.0f, 0.0f);
	vertexPositions[leftVertex] = FVector3f(-1.0f, 0.0f, 0.0f);

	FPolygonGroupID polygonGroup = meshDescription.CreatePolygonGroup();
	attributes.GetPolygonGroupMaterialSlotNames()[polygonGroup] = FName("Diamond");

	auto addTriangle = [&](FVertexID first, FVertexID second, FVertexID third, const FVector2f& firstUV, const FVector2f& secondUV, const FVector2f& thirdUV)
	{
		FVertexInstanceID firstInstance = meshDescription.CreateVertexInstance(first);
		FVertexInstanceID secondInstance = meshDescription.CreateVertexInstance(second);
		FVertexInstanceID thirdInstance = meshDescription.CreateVertexInstance(third);

		vertexNormals[firstInstance] = FVector3f(0.0f, 0.0f, 1.0f);
		vertexNormals[secondInstance] = FVector3f(0.0f, 0.0f, 1.0f);
		vertexNormals[thirdInstance] = FVector3f(0.0f, 0.0f, 1.0f);

		vertexUVs[firstInstance] = firstUV;
		vertexUVs[secondInstance] = secondUV;
		vertexUVs[thirdInstance] = thirdUV;

		meshDescription.CreateTriangle(polygonGroup, { firstInstance, secondInstance, thirdInstance });
	};

	addTriangle(topVertex, rightVertex, bottomVertex, FVector2f(0.5f, 0.0f), FVector2f(1.0f, 0.5f), FVector2f(0.5f, 1.0f));
	addTriangle(topVertex, bottomVertex, leftVertex, FVector2f(0.5f, 0.0f), FVector2f(0.5f, 1.0f), FVector2f(0.0f, 0.5f));

	UStaticMesh* staticMesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	staticMesh->SetStaticMaterials({ FStaticMaterial() });

	UStaticMesh::FBuildMeshDescriptionsParams buildParams;
	buildParams.bFastBuild = true;

	TArray<const FMeshDescription*> meshDescriptionPtrs{ &meshDescription };
	staticMesh->BuildFromMeshDescriptions(meshDescriptionPtrs, buildParams);

	return staticMesh;
}

void UVAudioVisualisationComponent::CreateInstancedMesh()
{
	DiamondMesh = BuildDiamondMesh();

	InstancedMesh = NewObject<UInstancedStaticMeshComponent>(GetOwner(), TEXT("VADiamonds"), RF_Transient);

	// Each diamond's transform is written in world space once (see OnVisualisationData) and must
	// stay fixed in world space from then on - it marks a ray-bounce hit point, not something that
	// should follow the owning emitter/listener around. Attaching to `this` would normally make
	// InstancedMesh's own component-to-world transform track the parent (the emitter/listener) as
	// it moves, which would silently drag every already-placed instance along with it, since
	// per-instance transforms are stored relative to the component. Absolute location/rotation/
	// scale pins InstancedMesh's component-to-world transform to identity regardless of the
	// parent's movement, so bWorldSpace writes in OnVisualisationData stay put once written.
	InstancedMesh->SetUsingAbsoluteLocation(true);
	InstancedMesh->SetUsingAbsoluteRotation(true);
	InstancedMesh->SetUsingAbsoluteScale(true);
	InstancedMesh->SetupAttachment(this);
	InstancedMesh->RegisterComponent();
	InstancedMesh->SetMobility(EComponentMobility::Movable);
	InstancedMesh->SetStaticMesh(DiamondMesh);
	InstancedMesh->SetCastShadow(false);
	InstancedMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InstancedMesh->NumCustomDataFloats = 1;
	InstancedMesh->SetComponentTickEnabled(false);

	DiamondMaterialInstance = UMaterialInstanceDynamic::Create(DiamondMaterial, this);
	InstancedMesh->SetMaterial(0, DiamondMaterialInstance);

	ApplyMaterialParameters();

	int32 requiredCount = GetRequiredInstanceCount();
	FTransform identity = FTransform::Identity;

	for (int32 i = 0; i < requiredCount; i++)
		InstancedMesh->AddInstance(identity, /*bWorldSpace=*/false);

	NextInstance = 0;
}

void UVAudioVisualisationComponent::ApplyMaterialParameters()
{
	if (!DiamondMaterialInstance)
		return;

	DiamondMaterialInstance->SetScalarParameterValue(TEXT("FadeInMs"), (float)FadeInMilliseconds);
	DiamondMaterialInstance->SetScalarParameterValue(TEXT("FadeOutMs"), (float)FadeOutMilliseconds);
	DiamondMaterialInstance->SetScalarParameterValue(TEXT("DurationMs"), (float)DurationMilliseconds);
	DiamondMaterialInstance->SetScalarParameterValue(TEXT("MaxOpacity"), Color.A);
	DiamondMaterialInstance->SetVectorParameterValue(TEXT("BaseColor"), Color);
}

int32 UVAudioVisualisationComponent::GetRequiredInstanceCount() const
{
	if (!OwnerEmitter)
		return 0;

	int32 batchSize = FMath::Max(1, VisualisationRayCount * VisualisationBounceCount);
	int32 updateFrequency = FMath::Max(1, VisualisationUpdateFrequency);
	int32 batchesInFlight = (DurationMilliseconds / updateFrequency) + 2;

	return batchSize * batchesInFlight;
}

void UVAudioVisualisationComponent::ApplyVisualisationSettings() const
{
	if (!OwnerEmitter || !OwnerEmitter->GetVAEmitter())
		return;

	vaEmitterSetVisualisationRayCount(OwnerEmitter->GetVAEmitter(), VisualisationRayCount);
	vaEmitterSetVisualisationBounceCount(OwnerEmitter->GetVAEmitter(), VisualisationBounceCount);
	vaEmitterSetVisualisationUpdateFrequency(OwnerEmitter->GetVAEmitter(), VisualisationUpdateFrequency);
}

void UVAudioVisualisationComponent::OnVisualisationData(VAVisualisationData* data, int32 count)
{
	if (!InstancedMesh || count <= 0)
		return;

	int32 capacity = InstancedMesh->GetInstanceCount();
	int32 needed = FMath::Max(count, GetRequiredInstanceCount());

	if (needed > capacity)
	{
		FTransform identity = FTransform::Identity;

		for (int32 i = capacity; i < needed; i++)
			InstancedMesh->AddInstance(identity, /*bWorldSpace=*/false);

		capacity = needed;
		NextInstance = 0;
	}

	float now = GetWorld()->GetTimeSeconds();
	FVector emitterPosition = OwnerEmitter->GetActorLocation();
	float maxDistanceSquared = MaxDistance * MaxDistance;

	for (int32 i = 0; i < count; i++)
	{
		FVector position = VAVectorToFVector(data[i].position);

		if (MaxDistance > 0.0f && FVector::DistSquared(position, emitterPosition) > maxDistanceSquared)
			continue;

		FVector normal = VAVectorToFVector(data[i].normal);
		normal = normal.SizeSquared() > KINDA_SMALL_NUMBER ? normal.GetSafeNormal() : FVector::UpVector;

		FVector upHint = FMath::Abs(FVector::DotProduct(normal, FVector::UpVector)) > 0.999f ? FVector::ForwardVector : FVector::UpVector;
		FQuat rotation = FRotationMatrix::MakeFromZX(normal, upHint).ToQuat();

		FVector instancePosition = position + normal * NormalOffset;
		FTransform transform(rotation, instancePosition, FVector(Size));

		InstancedMesh->UpdateInstanceTransform(NextInstance, transform, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
		InstancedMesh->SetCustomDataValue(NextInstance, 0, now, /*bMarkRenderStateDirty=*/false);

		NextInstance = (NextInstance + 1) % capacity;
	}

	InstancedMesh->MarkRenderStateDirty();
}

#if WITH_EDITOR
void UVAudioVisualisationComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// InstancedMesh only exists while PIE/game is running, so ignore edits when we haven't hit Play yet
	if (!InstancedMesh)
		return;

	ApplyMaterialParameters();
	ApplyVisualisationSettings();
}
#endif
