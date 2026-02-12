// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"

/**
 * Shared dynamic config utilities for AssetFactoryAI.
 * Reads values from the [AssetFactoryAI.Dynamic] section in GEditorPerProjectIni.
 *
 * Placed in a named namespace (not anonymous) so that Unity Build
 * can safely include this header from multiple .cpp files.
 */
namespace AssetFactoryAI::DynamicConfig
{
	inline constexpr const TCHAR* Section = TEXT("AssetFactoryAI.Dynamic");

	inline FString GetString(const TCHAR* Key, const TCHAR* DefaultValue)
	{
		FString Value;
		if (GConfig && GConfig->GetString(Section, Key, Value, GEditorPerProjectIni) && !Value.IsEmpty())
		{
			return Value;
		}
		return FString(DefaultValue);
	}

	inline float GetFloat(const TCHAR* Key, float DefaultValue)
	{
		float Value = DefaultValue;
		if (GConfig)
		{
			GConfig->GetFloat(Section, Key, Value, GEditorPerProjectIni);
		}
		return Value;
	}

	inline int32 GetInt(const TCHAR* Key, int32 DefaultValue)
	{
		int32 Value = DefaultValue;
		if (GConfig)
		{
			GConfig->GetInt(Section, Key, Value, GEditorPerProjectIni);
		}
		return Value;
	}

	inline bool GetBool(const TCHAR* Key, bool DefaultValue)
	{
		bool Value = DefaultValue;
		if (GConfig)
		{
			GConfig->GetBool(Section, Key, Value, GEditorPerProjectIni);
		}
		return Value;
	}
}
