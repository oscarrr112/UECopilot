// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AssetFactoryAI : ModuleRules
{
	public AssetFactoryAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"HTTP",
				"Json",
				"JsonUtilities"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"BlueprintGraph",
				"UnrealEd",
				"Kismet",
				"KismetCompiler",
				"GraphEditor",
				"DeveloperSettings",
				"EditorScriptingUtilities"
			}
		);
	}
}
