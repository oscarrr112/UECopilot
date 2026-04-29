// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class UStateTreeEditorData;

namespace UE::AssetFactory::StateTree
{
	struct FAFStateTreeBindingDiagnostic
	{
		FString Code;
		FString Severity;
		FString Path;
		FString BindingTarget;
		FString Message;
	};

	struct FAFStateTreeBindingExtractionResult
	{
		TArray<TSharedPtr<FJsonValue>> Bindings;
		TArray<FAFStateTreeBindingDiagnostic> Diagnostics;
	};

	FAFStateTreeBindingExtractionResult ExtractPropertyBindings(const UStateTreeEditorData* EditorData);
}
