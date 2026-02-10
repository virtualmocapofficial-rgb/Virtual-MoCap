using UnrealBuildTool;

public class MocapRecorder : ModuleRules
{
    public MocapRecorder(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Runtime module: MUST NOT depend on editor-only modules.
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",

                // Only keep this if your runtime module has AnimGraphRuntime types in PUBLIC headers.
                // If it's used only in .cpp files, move it to PrivateDependencyModuleNames instead.
                "AnimGraphRuntime"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Add runtime-only private deps here as needed.
            }
        );

        bUseUnity = true;
        bEnforceIWYU = true;
        ShadowVariableWarningLevel = WarningLevel.Warning;
    }
}
