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
			"AssetFactoryAI",
			"AssetTools",
			"EnhancedInput",
			"InputCore",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"DesktopPlatform",
			"Kismet",
			"BlueprintGraph",
			"BehaviorTreeEditor",
			"AIGraph",
			"EditorSubsystem",
			"AIModule",
			"UMG",
			"UMGEditor",
			// Material generation
			"RenderCore",
			"MaterialEditor",
			// HTTP Server
			"HTTPServer",
			// Image compression for viewport screenshot
			"ImageWrapper",
			// Settings
			"DeveloperSettings",
			// GAS (Gameplay Ability System) - GameplayTags is always available
			"GameplayTags",
			"GameplayTagsEditor",
			// StateTree generation
			"StateTreeModule",
			"StateTreeEditorModule",
			"StateTreeDeveloper",
			"GameplayStateTreeModule",
			"StructUtils",
			"StructUtilsEditor",
			"PropertyBindingUtils",
			"PropertyBindingUtilsEditor",
			// Editor state & Python execution
			"ContentBrowser",
			"LevelEditor",
			"PythonScriptPlugin"
		});

		// GameplayAbilities is optional — only link when the plugin is enabled
		if (Target.bBuildDeveloperTools || DoesModuleExist("GameplayAbilities"))
		{
			PrivateDependencyModuleNames.Add("GameplayAbilities");
			PrivateDefinitions.Add("WITH_GAMEPLAY_ABILITIES=1");
		}
	}

	private bool DoesModuleExist(string ModuleName)
	{
		try
		{
			// If the module can be resolved, it exists
			return !string.IsNullOrEmpty(GetModuleDirectory(ModuleName));
		}
		catch
		{
			return false;
		}
	}
}
