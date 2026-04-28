// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class UStateTreeEditorData;

namespace UE::AssetFactory::StateTree
{
	TArray<TSharedPtr<FJsonValue>> ExtractPropertyBindings(const UStateTreeEditorData* EditorData);
}
