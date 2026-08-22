using UnrealBuildTool;

public class WorldSportsEditorTarget : TargetRules
{
	public WorldSportsEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("WorldSports");
		ExtraModuleNames.Add("WorldSportsAthletics");
		ExtraModuleNames.Add("WorldSportsEditor");
	}
}
