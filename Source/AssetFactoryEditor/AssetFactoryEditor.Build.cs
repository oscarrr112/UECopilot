// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AssetFactoryEditor : ModuleRules
{
	public AssetFactoryEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"AssetFactoryAI"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore",
				"UnrealEd",
				"EditorStyle",
				"EditorFramework",
				"ToolMenus",
				"LevelEditor",
				"BlueprintGraph",
				"Kismet",
				"GraphEditor",
				"WorkspaceMenuStructure",
				"Projects",
				"ContentBrowser"
			}
		);
	}
}
