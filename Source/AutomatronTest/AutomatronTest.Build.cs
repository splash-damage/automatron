// Copyright 2020 Splash Damage, Ltd. - All Rights Reserved.

using UnrealBuildTool;

public class AutomatronTest : ModuleRules
{
	public AutomatronTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bEnforceIWYU = true;
        bLegacyPublicIncludePaths = false;

        PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"Automatron",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"Engine"
		});
	}
}
