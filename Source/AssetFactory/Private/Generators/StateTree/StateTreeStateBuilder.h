// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UStateTreeEditorData;
class UStateTreeState;
class FJsonObject;

namespace UE::AssetFactory::StateTree
{
	bool ApplyStateTreeConfig(
		UStateTreeEditorData& EditorData,
		TSharedPtr<FJsonObject> Config,
		FString& OutError);
}
