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

#if WITH_EDITOR
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#endif

// Material-validation warnings each need their own on-screen message key (distinct from the
// general DisplayWarning key below) so several problems can be shown at once instead of
// overwriting each other - offsets stay well within VAEmitterMessageStride (1000) per component.
enum EVAVisualisationMaterialWarningOffset : uint32
{
	VAVisualisationWarningBlendMode = 1,
	VAVisualisationWarningMissingScalarParam = 3, // + parameter index, see RequiredScalarParameterNames
	VAVisualisationWarningMissingVectorParam = 10,
};

static const TCHAR* RequiredScalarParameterNames[] = { TEXT("CurrentTime"), TEXT("FadeInMs"), TEXT("FadeOutMs"), TEXT("DurationMs"), TEXT("MaxOpacity") };
static const TCHAR* RequiredVectorParameterName = TEXT("BaseColor");

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

void UVAudioVisualisationComponent::DisplayMaterialWarning(uint32 offset, const TCHAR* fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	DisplayDebugWarningArgs(VAEmitterMessageBase + GetUniqueID() * VAEmitterMessageStride + offset, fmt, args);
	va_end(args);
}

void UVAudioVisualisationComponent::ClearMaterialWarning(uint32 offset) const
{
	ClearDebugWarning(VAEmitterMessageBase + GetUniqueID() * VAEmitterMessageStride + offset);
}

void UVAudioVisualisationComponent::ValidateDiamondMaterial() const
{
	if (!DiamondMaterial)
		return;

	if (DiamondMaterial->GetBlendMode() != BLEND_Translucent)
		DisplayMaterialWarning(VAVisualisationWarningBlendMode, TEXT("[VA] DiamondMaterial '%s' on '%s' must have Blend Mode set to Translucent - click the Generate Fade Nodes button above Diamond Material"), *DiamondMaterial->GetName(), *GetOwner()->GetActorNameOrLabel());
	else
		ClearMaterialWarning(VAVisualisationWarningBlendMode);

	for (int32 i = 0; i < UE_ARRAY_COUNT(RequiredScalarParameterNames); i++)
	{
		float value;
		uint32 warningOffset = VAVisualisationWarningMissingScalarParam + i;

		if (!DiamondMaterial->GetScalarParameterValue(FName(RequiredScalarParameterNames[i]), value))
			DisplayMaterialWarning(warningOffset, TEXT("[VA] DiamondMaterial '%s' on '%s' is missing scalar parameter '%s' - click the Generate Fade Nodes button above Diamond Material"), *DiamondMaterial->GetName(), *GetOwner()->GetActorNameOrLabel(), RequiredScalarParameterNames[i]);
		else
			ClearMaterialWarning(warningOffset);
	}

	FLinearColor colorValue;

	if (!DiamondMaterial->GetVectorParameterValue(FName(RequiredVectorParameterName), colorValue))
		DisplayMaterialWarning(VAVisualisationWarningMissingVectorParam, TEXT("[VA] DiamondMaterial '%s' on '%s' is missing vector parameter '%s' - click the Generate Fade Nodes button above Diamond Material"), *DiamondMaterial->GetName(), *GetOwner()->GetActorNameOrLabel(), RequiredVectorParameterName);
	else
		ClearMaterialWarning(VAVisualisationWarningMissingVectorParam);
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

	ValidateDiamondMaterial();
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
// Tag applied to every node this function creates (via the base UMaterialExpression::Desc field),
// so a repeat press can find and delete only its own previous output and rebuild cleanly, without
// touching anything else the user has added to the material by hand.
static const TCHAR* VAGeneratedNodeTag = TEXT("VADiamondFade (generated)");

void UVAudioVisualisationComponent::GenerateFadeNodes()
{
	if (!DiamondMaterial)
	{
		DisplayWarning(TEXT("[VA] Cannot generate fade nodes on '%s' - no DiamondMaterial assigned"), *GetOwner()->GetActorNameOrLabel());
		return;
	}

	UMaterial* material = Cast<UMaterial>(DiamondMaterial);

	if (!material)
	{
		DisplayWarning(TEXT("[VA] DiamondMaterial '%s' on '%s' must be a Material asset, not a Material Instance, to generate fade nodes"), *DiamondMaterial->GetName(), *GetOwner()->GetActorNameOrLabel());
		return;
	}

	// Remove only nodes this function created on a previous press, leaving anything else the user
	// added by hand alone.
	TArray<TObjectPtr<UMaterialExpression>> existingExpressions(material->GetExpressions());

	for (const TObjectPtr<UMaterialExpression>& expression : existingExpressions)
		if (expression && expression->Desc == VAGeneratedNodeTag)
			UMaterialEditingLibrary::DeleteMaterialExpression(material, expression);

	// Translucent is required for Opacity (the fade math) to have any effect. Unlit is just a
	// sane default for a diamond sprite that doesn't need to receive lighting - not required for
	// fading, so feel free to switch it to Lit/other shading models afterwards if you want a
	// different look; this generator won't warn about that or fight you on it.
	material->BlendMode = BLEND_Translucent;
	material->SetShadingModel(MSM_Unlit);
	material->TwoSided = true;

	bool bNeedsRecompile = false;
	UMaterialEditingLibrary::SetMaterialUsage(material, MATUSAGE_InstancedStaticMeshes, bNeedsRecompile);

	auto createExpression = [&](TSubclassOf<UMaterialExpression> expressionClass, int32 posX, int32 posY) -> UMaterialExpression*
	{
		UMaterialExpression* expression = UMaterialEditingLibrary::CreateMaterialExpression(material, expressionClass, posX, posY);
		expression->Desc = VAGeneratedNodeTag;
		return expression;
	};

	UMaterialExpressionPerInstanceCustomData* spawnTimeExpression = Cast<UMaterialExpressionPerInstanceCustomData>(createExpression(UMaterialExpressionPerInstanceCustomData::StaticClass(), -600, 0));
	spawnTimeExpression->DataIndex = 0;

	UMaterialExpressionScalarParameter* currentTimeExpression = Cast<UMaterialExpressionScalarParameter>(createExpression(UMaterialExpressionScalarParameter::StaticClass(), -600, 100));
	currentTimeExpression->ParameterName = TEXT("CurrentTime");

	UMaterialExpressionScalarParameter* fadeInExpression = Cast<UMaterialExpressionScalarParameter>(createExpression(UMaterialExpressionScalarParameter::StaticClass(), -600, 200));
	fadeInExpression->ParameterName = TEXT("FadeInMs");

	UMaterialExpressionScalarParameter* fadeOutExpression = Cast<UMaterialExpressionScalarParameter>(createExpression(UMaterialExpressionScalarParameter::StaticClass(), -600, 300));
	fadeOutExpression->ParameterName = TEXT("FadeOutMs");

	UMaterialExpressionScalarParameter* durationExpression = Cast<UMaterialExpressionScalarParameter>(createExpression(UMaterialExpressionScalarParameter::StaticClass(), -600, 400));
	durationExpression->ParameterName = TEXT("DurationMs");

	UMaterialExpressionScalarParameter* maxOpacityExpression = Cast<UMaterialExpressionScalarParameter>(createExpression(UMaterialExpressionScalarParameter::StaticClass(), -600, 500));
	maxOpacityExpression->ParameterName = TEXT("MaxOpacity");

	UMaterialExpressionVectorParameter* baseColorExpression = Cast<UMaterialExpressionVectorParameter>(createExpression(UMaterialExpressionVectorParameter::StaticClass(), -600, 650));
	baseColorExpression->ParameterName = TEXT("BaseColor");

	UMaterialExpressionCustom* fadeExpression = Cast<UMaterialExpressionCustom>(createExpression(UMaterialExpressionCustom::StaticClass(), -200, 300));
	fadeExpression->OutputType = CMOT_Float1;
	fadeExpression->Code = TEXT(
		"float elapsedMs = (CurrentTime - SpawnTime) * 1000.0;\n"
		"float fadeIn = FadeInMs > 0.0 ? saturate(elapsedMs / FadeInMs) : 1.0;\n"
		"float fadeOut = FadeOutMs > 0.0 ? saturate((DurationMs - elapsedMs) / FadeOutMs) : 1.0;\n"
		"return (elapsedMs < 0.0 || elapsedMs > DurationMs) ? 0.0 : MaxOpacity * min(fadeIn, fadeOut);");

	fadeExpression->Inputs.SetNum(6);
	fadeExpression->Inputs[0].InputName = TEXT("SpawnTime");
	fadeExpression->Inputs[1].InputName = TEXT("CurrentTime");
	fadeExpression->Inputs[2].InputName = TEXT("FadeInMs");
	fadeExpression->Inputs[3].InputName = TEXT("FadeOutMs");
	fadeExpression->Inputs[4].InputName = TEXT("DurationMs");
	fadeExpression->Inputs[5].InputName = TEXT("MaxOpacity");
	fadeExpression->RebuildOutputs(); // populates Outputs - CreateMaterialExpression doesn't call PostEditChangeProperty, so this never runs implicitly

	UMaterialEditingLibrary::ConnectMaterialExpressions(spawnTimeExpression, TEXT(""), fadeExpression, TEXT("SpawnTime"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(currentTimeExpression, TEXT(""), fadeExpression, TEXT("CurrentTime"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(fadeInExpression, TEXT(""), fadeExpression, TEXT("FadeInMs"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(fadeOutExpression, TEXT(""), fadeExpression, TEXT("FadeOutMs"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(durationExpression, TEXT(""), fadeExpression, TEXT("DurationMs"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(maxOpacityExpression, TEXT(""), fadeExpression, TEXT("MaxOpacity"));

	UMaterialEditingLibrary::ConnectMaterialProperty(fadeExpression, TEXT(""), MP_Opacity);
	UMaterialEditingLibrary::ConnectMaterialProperty(baseColorExpression, TEXT(""), MP_EmissiveColor);

	UMaterialEditingLibrary::LayoutMaterialExpressions(material);
	UMaterialEditingLibrary::RecompileMaterial(material);

	ValidateDiamondMaterial();
}

void UVAudioVisualisationComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UVAudioVisualisationComponent, DiamondMaterial))
		ValidateDiamondMaterial();

	// InstancedMesh only exists while PIE/game is running, so ignore edits when we haven't hit Play yet
	if (!InstancedMesh)
		return;

	ApplyMaterialParameters();
	ApplyVisualisationSettings();
}
#endif
