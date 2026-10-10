#include "VAVisualisation.h"
#include "VAEmitter.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Engine/World.h"
#include "UObject/Package.h"

extern "C" {
#include "vaudio.h"
}

#include "VAConstants.h"
#include "VALog.h"

#if WITH_EDITOR
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#endif

enum EVAVisualisationMaterialWarningOffset : uint32
{
	VAVisualisationWarningBlendMode = 1,
	VAVisualisationWarningMissingScalarParam = 3, // + parameter index, see RequiredScalarParameterNames
	VAVisualisationWarningMissingVectorParam = 10,
};

static const TCHAR* RequiredScalarParameterNames[] = { TEXT("CurrentTime"), TEXT("FadeInMs"), TEXT("FadeOutMs"), TEXT("DurationMs"), TEXT("MaxOpacity") };
static const TCHAR* RequiredVectorParameterName = TEXT("BaseColor");

static void VAVisualisationCallbackTrampoline(VAEmitter* emitter, VAVisualisationData* data, int32 count)
{
	if (AVAEmitter* Owner = static_cast<AVAEmitter*>(vaEmitterGetUserData(emitter)))
		if (Owner->VisualisationComponent)
			Owner->VisualisationComponent->OnVisualisationData(data, count);
}

UVAVisualisation::UVAVisualisation()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UVAVisualisation::ReportMaterialWarning(uint32 offset, const FString& message) const
{
	VAReport(ELogVerbosity::Warning, this, VAMessageKey(this, EVAMessageSlot::VisualisationMaterial, offset), message);
}

void UVAVisualisation::ClearMaterialWarning(uint32 offset) const
{
	VAClearMessage(VAMessageKey(this, EVAMessageSlot::VisualisationMaterial, offset));
}

void UVAVisualisation::ValidateDiamondMaterial() const
{
	if (!DiamondMaterial)
		return;

	if (DiamondMaterial->GetBlendMode() != BLEND_Translucent)
		ReportMaterialWarning(VAVisualisationWarningBlendMode, FString::Printf(TEXT("DiamondMaterial '%s' must have Blend Mode set to Translucent - click the Generate Fade Nodes button above Diamond Material"), *DiamondMaterial->GetName()));
	else
		ClearMaterialWarning(VAVisualisationWarningBlendMode);

	for (int32 i = 0; i < UE_ARRAY_COUNT(RequiredScalarParameterNames); i++)
	{
		float value;
		uint32 warningOffset = VAVisualisationWarningMissingScalarParam + i;

		if (!DiamondMaterial->GetScalarParameterValue(FName(RequiredScalarParameterNames[i]), value))
			ReportMaterialWarning(warningOffset, FString::Printf(TEXT("DiamondMaterial '%s' is missing scalar parameter '%s' - click the Generate Fade Nodes button above Diamond Material"), *DiamondMaterial->GetName(), RequiredScalarParameterNames[i]));
		else
			ClearMaterialWarning(warningOffset);
	}

	FLinearColor colorValue;

	if (!DiamondMaterial->GetVectorParameterValue(FName(RequiredVectorParameterName), colorValue))
		ReportMaterialWarning(VAVisualisationWarningMissingVectorParam, FString::Printf(TEXT("DiamondMaterial '%s' is missing vector parameter '%s' - click the Generate Fade Nodes button above Diamond Material"), *DiamondMaterial->GetName(), RequiredVectorParameterName));
	else
		ClearMaterialWarning(VAVisualisationWarningMissingVectorParam);
}

void UVAVisualisation::OnRegister()
{
	Super::OnRegister();

	OwnerEmitter = Cast<AVAEmitter>(GetOwner());
}

void UVAVisualisation::BeginPlay()
{
	Super::BeginPlay();

	if (!OwnerEmitter)
	{
		VA_WARN_NAMED_SLOT(EVAMessageSlot::Status, TEXT("Must be attached to a VAListener, VAEmitter or VASource actor and will not render"));
		return;
	}

	InitializeVisualisation();
}

void UVAVisualisation::InitializeVisualisation()
{
	if (bCallbackRegistered || !OwnerEmitter)
		return;

	if (!OwnerEmitter->TryInitializeEmitter())
	{
		// The level's VAWorld hasn't begun play yet. The owner calls this again once it joins
		if (!OwnerEmitter->GetAudioWorld())
			return;

		VA_WARN_NAMED_SLOT(EVAMessageSlot::Status, TEXT("Will not render as its owner failed to initialise"));
		return;
	}

	if (!DiamondMaterial)
	{
		VA_WARN_NAMED_SLOT(EVAMessageSlot::Status, TEXT("Has no DiamondMaterial assigned and will not render"));
		return;
	}

	// Every listener shares one handle, which only the current listener holds
	if (!OwnerEmitter->GetVAEmitter())
	{
		VA_WARN_NAMED_SLOT(EVAMessageSlot::Status, TEXT("Is attached to a listener that isn't current when play begins and will not render"));
		return;
	}

	ValidateDiamondMaterial();
	CreateInstancedMesh();
	ApplyVisualisationSettings();

	OwnerEmitter->VisualisationComponent = this;
	VAResult result = vaEmitterSetVisualisationCallback(OwnerEmitter->GetVAEmitter(), &VAVisualisationCallbackTrampoline);

	if (result != VA_SUCCESS)
	{
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to register the visualisation callback."));
		OwnerEmitter->VisualisationComponent = nullptr;
		return;
	}

	bCallbackRegistered = true;

	SetComponentTickEnabled(true);
}

void UVAVisualisation::TeardownVisualisation()
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

	VAClearMessage(VAMessageKey(this, EVAMessageSlot::Status));
}

void UVAVisualisation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TeardownVisualisation();

	Super::EndPlay(EndPlayReason);
}

void UVAVisualisation::DestroyComponent(bool bPromoteChildren)
{
	TeardownVisualisation();

	Super::DestroyComponent(bPromoteChildren);
}

void UVAVisualisation::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (DiamondMaterialInstance)
		DiamondMaterialInstance->SetScalarParameterValue(TEXT("CurrentTime"), GetWorld()->GetTimeSeconds());
}

UStaticMesh* UVAVisualisation::BuildDiamondMesh()
{
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

void UVAVisualisation::CreateInstancedMesh()
{
	DiamondMesh = BuildDiamondMesh();

	InstancedMesh = NewObject<UInstancedStaticMeshComponent>(GetOwner(), TEXT("VADiamonds"), RF_Transient);

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

void UVAVisualisation::ApplyMaterialParameters()
{
	if (!DiamondMaterialInstance)
		return;

	DiamondMaterialInstance->SetScalarParameterValue(TEXT("FadeInMs"), (float)FadeInMilliseconds);
	DiamondMaterialInstance->SetScalarParameterValue(TEXT("FadeOutMs"), (float)FadeOutMilliseconds);
	DiamondMaterialInstance->SetScalarParameterValue(TEXT("DurationMs"), (float)DurationMilliseconds);
	DiamondMaterialInstance->SetScalarParameterValue(TEXT("MaxOpacity"), Color.A);
	DiamondMaterialInstance->SetVectorParameterValue(TEXT("BaseColor"), Color);
}

int32 UVAVisualisation::GetRequiredInstanceCount() const
{
	if (!OwnerEmitter)
		return 0;

	int32 batchSize = FMath::Max(1, VisualisationRayCount * VisualisationBounceCount);
	int32 updateFrequency = FMath::Max(1, VisualisationUpdateFrequency);
	int32 batchesInFlight = (DurationMilliseconds / updateFrequency) + 2;

	return batchSize * batchesInFlight;
}

void UVAVisualisation::ApplyVisualisationSettings() const
{
	if (!OwnerEmitter || !OwnerEmitter->GetVAEmitter())
		return;

	vaEmitterSetVisualisationRayCount(OwnerEmitter->GetVAEmitter(), VisualisationRayCount);
	vaEmitterSetVisualisationBounceCount(OwnerEmitter->GetVAEmitter(), VisualisationBounceCount);
	vaEmitterSetVisualisationUpdateFrequency(OwnerEmitter->GetVAEmitter(), VisualisationUpdateFrequency);
}

void UVAVisualisation::OnVisualisationData(VAVisualisationData* data, int32 count)
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
static const TCHAR* VAGeneratedNodeTag = TEXT("VADiamondFade (generated)");

void UVAVisualisation::GenerateFadeNodes()
{
	if (!DiamondMaterial)
	{
		VA_WARN_NAMED(TEXT("Cannot generate fade nodes - no DiamondMaterial assigned"));
		return;
	}

	UMaterial* material = Cast<UMaterial>(DiamondMaterial);

	if (!material)
	{
		VA_WARN_NAMED(TEXT("DiamondMaterial '%s' must be a Material asset, not a Material Instance, to generate fade nodes"), *DiamondMaterial->GetName());
		return;
	}

	// Remove only nodes this function created on a previous press, leaving anything else the user
	// added by hand alone.
	TArray<TObjectPtr<UMaterialExpression>> existingExpressions(material->GetExpressions());

	for (const TObjectPtr<UMaterialExpression>& expression : existingExpressions)
		if (expression && expression->Desc == VAGeneratedNodeTag)
			UMaterialEditingLibrary::DeleteMaterialExpression(material, expression);

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

void UVAVisualisation::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UVAVisualisation, DiamondMaterial))
		ValidateDiamondMaterial();

	// InstancedMesh only exists while PIE/game is running, so ignore edits when we haven't hit Play yet
	if (!InstancedMesh)
		return;

	ApplyMaterialParameters();
	ApplyVisualisationSettings();
}
#endif
