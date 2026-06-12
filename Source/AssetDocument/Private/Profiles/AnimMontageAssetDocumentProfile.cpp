// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/AnimMontageAssetDocumentProfile.h"

#include "Animation/AnimMontage.h"
#include "Dom/JsonValue.h"

namespace
{
TArray<FName> MakeAnimMontageBodyKeys()
{
	return {
		TEXT("Skeleton"),
		TEXT("PreviewMesh"),
		TEXT("SlotAnimTracks"),
		TEXT("CompositeSections"),
		TEXT("Notifies"),
		TEXT("NotifyStates"),
		TEXT("Blend"),
	};
}

TArray<TSharedPtr<FJsonValue>> MakeEmptyArray()
{
	return TArray<TSharedPtr<FJsonValue>>();
}
}

UClass* FAnimMontageAssetDocumentProfile::GetExactClass() const
{
	return UAnimMontage::StaticClass();
}

TSharedRef<FJsonObject> FAnimMontageAssetDocumentProfile::GetDocumentShape() const
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected CDO-diff properties"));
	Shape->SetObjectField(TEXT("Body"), BodyCapability.GetSchemaHint());
	return Shape;
}

TSharedRef<FJsonObject> FAnimMontageAssetDocumentProfile::CreateTemplate(const FAssetDocumentTemplateContext& Context) const
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetField(TEXT("Skeleton"), MakeShared<FJsonValueNull>());
	Body->SetField(TEXT("PreviewMesh"), MakeShared<FJsonValueNull>());
	Body->SetArrayField(TEXT("SlotAnimTracks"), MakeEmptyArray());
	Body->SetArrayField(TEXT("CompositeSections"), MakeEmptyArray());
	Body->SetArrayField(TEXT("Notifies"), MakeEmptyArray());
	Body->SetArrayField(TEXT("NotifyStates"), MakeEmptyArray());
	Body->SetObjectField(TEXT("Blend"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
	Template->SetNumberField(TEXT("SchemaVersion"), 1);
	Template->SetStringField(TEXT("Target"), Context.Target);
	Template->SetStringField(TEXT("Class"), TEXT("/Script/Engine.AnimMontage"));
	Template->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Template->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Template->SetObjectField(TEXT("Body"), Body);
	return Template;
}

TArray<FName> FAnimMontageAssetDocumentProfile::GetBodyKeys() const
{
	return MakeAnimMontageBodyKeys();
}

const IAssetDocumentCapability* FAnimMontageAssetDocumentProfile::ResolveBodyAdapter(FName BodyKey) const
{
	if (BodyKey == TEXT("Body"))
	{
		return &BodyCapability;
	}

	for (const FName& KnownBodyKey : MakeAnimMontageBodyKeys())
	{
		if (BodyKey == KnownBodyKey)
		{
			return &BodyCapability;
		}
	}

	return nullptr;
}
