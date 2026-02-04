// Copyright ProjectRPG. All Rights Reserved.

using UnrealBuildTool;

public class AssetFactory : ModuleRules
{
	public AssetFactory(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"EnhancedInput",
			"InputCore",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"DesktopPlatform",
			"Kismet",
			"BlueprintGraph",
			"EditorSubsystem",
			"AIModule",
			"UMG",
			"UMGEditor",
			// Material generation
			"RenderCore",
			"MaterialEditor"
		});
	}
}
