using UnrealBuildTool;

public class RoverReplica : ModuleRules
{
	public RoverReplica(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayTags",
			"GeometryCollectionEngine",
			"MotionWarping",
			"Niagara",
			"PhysicsCore"
		});
	}
}
