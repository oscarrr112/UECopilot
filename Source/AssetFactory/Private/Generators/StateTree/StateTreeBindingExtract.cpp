// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/StateTree/StateTreeBindingExtract.h"

#include "Dom/JsonObject.h"
#include "PropertyBindingBinding.h"
#include "PropertyBindingPath.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorPropertyBindings.h"

namespace
{
	FString GuidToString(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	TSharedPtr<FJsonValue> ExtractPathSegment(const FPropertyBindingPathSegment& Segment)
	{
		const FString Name = Segment.GetName().ToString();
		const int32 ArrayIndex = Segment.GetArrayIndex();
		const UStruct* InstanceStruct = Segment.GetInstanceStruct();
#if WITH_EDITORONLY_DATA
		const FGuid PropertyGuid = Segment.GetPropertyGuid();
#endif

		if (ArrayIndex == INDEX_NONE
			&& InstanceStruct == nullptr
#if WITH_EDITORONLY_DATA
			&& !PropertyGuid.IsValid()
#endif
			)
		{
			return MakeShared<FJsonValueString>(Name);
		}

		TSharedPtr<FJsonObject> SegmentJson = MakeShared<FJsonObject>();
		SegmentJson->SetStringField(TEXT("name"), Name);
		if (ArrayIndex != INDEX_NONE)
		{
			SegmentJson->SetNumberField(TEXT("arrayIndex"), ArrayIndex);
		}
#if WITH_EDITORONLY_DATA
		if (PropertyGuid.IsValid())
		{
			SegmentJson->SetStringField(TEXT("guid"), GuidToString(PropertyGuid));
		}
#endif
		if (InstanceStruct)
		{
			SegmentJson->SetStringField(TEXT("instanceStruct"), InstanceStruct->GetPathName());
		}
		return MakeShared<FJsonValueObject>(SegmentJson);
	}

	TArray<TSharedPtr<FJsonValue>> ExtractPathSegments(const FPropertyBindingPath& Path)
	{
		TArray<TSharedPtr<FJsonValue>> SegmentValues;
		for (const FPropertyBindingPathSegment& Segment : Path.GetSegments())
		{
			SegmentValues.Add(ExtractPathSegment(Segment));
		}
		return SegmentValues;
	}

	TSharedPtr<FJsonObject> ExtractEndpoint(const UStateTreeEditorData& EditorData, const FPropertyBindingPath& Path)
	{
		TSharedPtr<FJsonObject> EndpointJson = MakeShared<FJsonObject>();
		if (Path.GetStructID() == EditorData.GetRootParametersGuid())
		{
			EndpointJson->SetStringField(TEXT("kind"), TEXT("rootParameter"));
		}
		else
		{
			EndpointJson->SetStringField(TEXT("kind"), TEXT("node"));
			EndpointJson->SetStringField(TEXT("node"), GuidToString(Path.GetStructID()));
			EndpointJson->SetStringField(TEXT("section"), TEXT("instance"));
		}
		EndpointJson->SetArrayField(TEXT("path"), ExtractPathSegments(Path));
		return EndpointJson;
	}

	TSharedPtr<FJsonObject> ExtractBinding(const UStateTreeEditorData& EditorData, const FPropertyBindingBinding& Binding)
	{
#if WITH_EDITOR
		if (Binding.GetPropertyFunctionNode().IsValid())
		{
			return nullptr;
		}
#endif

		TSharedPtr<FJsonObject> BindingJson = MakeShared<FJsonObject>();
		BindingJson->SetObjectField(TEXT("source"), ExtractEndpoint(EditorData, Binding.GetSourcePath()));
		BindingJson->SetObjectField(TEXT("target"), ExtractEndpoint(EditorData, Binding.GetTargetPath()));
		return BindingJson;
	}
}

TArray<TSharedPtr<FJsonValue>> UE::AssetFactory::StateTree::ExtractPropertyBindings(const UStateTreeEditorData* EditorData)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	if (!EditorData)
	{
		return Result;
	}

	const FStateTreeEditorPropertyBindings* EditorBindings = EditorData->GetPropertyEditorBindings();
	if (!EditorBindings)
	{
		return Result;
	}

	EditorBindings->ForEachBinding([EditorData, &Result](const FPropertyBindingBinding& Binding)
	{
		TSharedPtr<FJsonObject> BindingJson = ExtractBinding(*EditorData, Binding);
		if (BindingJson.IsValid())
		{
			Result.Add(MakeShared<FJsonValueObject>(BindingJson));
		}
	});

	return Result;
}
