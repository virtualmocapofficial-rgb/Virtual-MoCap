using UnrealBuildTool;

public class MocapRecorderEditor : ModuleRules
{
    public MocapRecorderEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Editor module: can depend on editor-only modules.
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "MocapRecorder",

                "UnrealEd",
                "Slate",
                "SlateCore",
                "ToolMenus",

                "InputCore",
                "PropertyEditor", // <-- ADD THIS


                // Only include these if you actually use them in code:
                "AssetRegistry",
                "AssetTools",
                "ContentBrowser",
                "EditorFramework",
                "LevelEditor"
            }
        );

        bUseUnity = true;
        bEnforceIWYU = true;
        ShadowVariableWarningLevel = WarningLevel.Warning;
    }
}
