using UnrealBuildTool;

public class RoverReplicaEditor : ModuleRules
{
	public RoverReplicaEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RoverReplica",
			"AnimGraph",
			"AnimGraphRuntime",
			"AssetRegistry",
			"AssetTools",
			"BlueprintGraph",
			"Chaos",
			"DataflowCore",
			"FractureEngine",
			"GeometryCollectionEditor",
			"GeometryCollectionEngine",
			"Kismet",
			"UnrealEd"
		});
	}
}
