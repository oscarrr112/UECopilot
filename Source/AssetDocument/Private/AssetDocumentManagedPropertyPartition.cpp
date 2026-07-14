// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentManagedPropertyPartition.h"

#include "Dom/JsonObject.h"

namespace
{
struct FAssetDocumentManagedTopLevelPropertyInfo
{
	TSet<FString> BodyPaths;
};

FString AssetDocumentManagedPropertyGetTopLevelName(const FString& PropertyPath)
{
	FString TrimmedPath = PropertyPath;
	TrimmedPath.TrimStartAndEndInline();

	const int32 SeparatorIndex = TrimmedPath.Find(TEXT("."));
	return SeparatorIndex == INDEX_NONE ? TrimmedPath : TrimmedPath.Left(SeparatorIndex);
}

TMap<FName, FAssetDocumentManagedTopLevelPropertyInfo> AssetDocumentCollectManagedProperties(
	const IAssetDocumentProfile* Profile)
{
	TMap<FName, FAssetDocumentManagedTopLevelPropertyInfo> ManagedProperties;
	if (!Profile)
	{
		return ManagedProperties;
	}

	for (const FAssetDocumentRegionPolicy& Policy : Profile->GetRegionPolicies())
	{
		for (const FString& PropertyPath : Policy.ManagedUePropertyPaths)
		{
			const FString TopLevelName = AssetDocumentManagedPropertyGetTopLevelName(PropertyPath);
			if (TopLevelName.IsEmpty())
			{
				continue;
			}

			FAssetDocumentManagedTopLevelPropertyInfo& ManagedProperty = ManagedProperties.FindOrAdd(FName(*TopLevelName));
			if (!Policy.BodyPath.IsEmpty())
			{
				ManagedProperty.BodyPaths.Add(Policy.BodyPath);
			}
		}
	}

	return ManagedProperties;
}

FString AssetDocumentDescribeBodyOwnership(const FAssetDocumentManagedTopLevelPropertyInfo& ManagedProperty)
{
	TArray<FString> SortedBodyPaths = ManagedProperty.BodyPaths.Array();
	SortedBodyPaths.Sort();
	return SortedBodyPaths.Num() > 0 ? FString::Join(SortedBodyPaths, TEXT(", ")) : TEXT("a structured Body region");
}
}

TSet<FName> FAssetDocumentManagedPropertyPartition::CollectTopLevelPropertyNames(const IAssetDocumentProfile* Profile)
{
	TSet<FName> PropertyNames;
	for (const TPair<FName, FAssetDocumentManagedTopLevelPropertyInfo>& Pair : AssetDocumentCollectManagedProperties(Profile))
	{
		PropertyNames.Add(Pair.Key);
	}
	return PropertyNames;
}

FAssetDocumentCapabilityResult FAssetDocumentManagedPropertyPartition::ValidateTopLevelProperties(
	const IAssetDocumentProfile* Profile,
	const TSharedPtr<FJsonObject>& Properties)
{
	if (!Properties.IsValid() || Properties->Values.Num() == 0 || !Profile)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	const TMap<FName, FAssetDocumentManagedTopLevelPropertyInfo> ManagedProperties = AssetDocumentCollectManagedProperties(Profile);
	TArray<FString> AuthoredPropertyNames;
	Properties->Values.GetKeys(AuthoredPropertyNames);
	AuthoredPropertyNames.Sort();

	FAssetDocumentCapabilityResult Result = FAssetDocumentCapabilityResult::Success();
	for (const FString& PropertyName : AuthoredPropertyNames)
	{
		const FAssetDocumentManagedTopLevelPropertyInfo* ManagedProperty = ManagedProperties.Find(FName(*PropertyName));
		if (!ManagedProperty)
		{
			continue;
		}

		const FString Message = FString::Printf(
			TEXT("Property '%s' is owned by %s and cannot be authored through top-level Properties"),
			*PropertyName,
			*AssetDocumentDescribeBodyOwnership(*ManagedProperty));
		if (Result.bSuccess)
		{
			Result.bSuccess = false;
			Result.Message = Message;
		}

		FAssetDocumentDiagnostic Diagnostic;
		Diagnostic.Path = FString::Printf(TEXT("/Properties/%s"), *PropertyName);
		Diagnostic.Code = TEXT("BodyOwnedProperty");
		Diagnostic.Message = Message;
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
	}

	return Result;
}
