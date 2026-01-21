// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UECopilotEditor : ModuleRules
{
	public UECopilotEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UECopilot",
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
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
