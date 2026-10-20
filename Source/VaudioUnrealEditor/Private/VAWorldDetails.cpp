#include "VAWorldDetails.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "VAWorld.h"

#define LOCTEXT_NAMESPACE "VAWorldDetails"

TSharedRef<IDetailCustomization> FVAWorldDetails::MakeInstance()
{
	return MakeShareable(new FVAWorldDetails);
}

void FVAWorldDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// The engine's Transform section (FActorDetails) always has rotation and scale, so it's hidden and rebuilt from the root component's location
	DetailBuilder.HideCategory("TransformCommon");

	if (DetailBuilder.HasClassDefaultObject())
		return;

	TArray<UObject*> roots;

	for (const TWeakObjectPtr<UObject>& selected : DetailBuilder.GetSelectedObjects())
	{
		if (AVAWorld* world = Cast<AVAWorld>(selected.Get()))
		{
			if (USceneComponent* root = world->GetRootComponent())
				roots.Add(root);
		}
	}

	if (roots.IsEmpty())
		return;

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("VAWorldTransform", LOCTEXT("Transform", "Transform"), ECategoryPriority::Transform);
	category.AddExternalObjectProperty(roots, USceneComponent::GetRelativeLocationPropertyName());
}

#undef LOCTEXT_NAMESPACE
