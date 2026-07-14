// Copyright ProjectRPG. All Rights Reserved.

using UnrealBuildTool;

public class AssetDocument : ModuleRules
{
	public AssetDocument(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities",
			"AssetFactory"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Kismet",
			"AssetRegistry",
			"AssetTools",
			"AnimGraph",
			"AIGraph",
			"AIModule",
			"BehaviorTreeEditor",
			"BlueprintGraph",
			"DirectoryWatcher",
			"GameplayTags",
			"Projects",
			"UMG",
			"UMGEditor",
			"MovieScene",
			"MovieSceneTracks",
			"StructUtils"
		});
	}
}
