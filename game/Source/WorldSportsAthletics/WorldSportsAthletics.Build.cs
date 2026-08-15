using UnrealBuildTool;

public class WorldSportsAthletics : ModuleRules
{
	public WorldSportsAthletics(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Athletics depends on the core; the core must NEVER depend back.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"WorldSports",
		});
	}
}
