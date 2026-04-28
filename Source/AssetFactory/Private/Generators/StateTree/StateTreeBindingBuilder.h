// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Generators/StateTree/StateTreeLinkResolver.h"

class UStateTreeEditorData;

namespace UE::AssetFactory::StateTree
{
	bool ApplyPropertyBindings(
		UStateTreeEditorData& EditorData,
		const FAFStateTreeStateIndex& StateIndex,
		const TSharedPtr<FJsonObject>& Config,
		FString& OutError);
}
