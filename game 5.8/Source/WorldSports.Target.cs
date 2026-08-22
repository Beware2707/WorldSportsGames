using UnrealBuildTool;

public class WorldSportsTarget : TargetRules
{
	public WorldSportsTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("WorldSports");
		ExtraModuleNames.Add("WorldSportsAthletics");
	}
}
