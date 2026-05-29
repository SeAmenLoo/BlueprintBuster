// Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
// Plugin: BlueprintBuster

// BlueprintBuster.Build.cs

using UnrealBuildTool;

public class BlueprintBuster : ModuleRules
{
    public BlueprintBuster(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Json",
            "JsonUtilities",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "BlueprintGraph",
            "Kismet",
            "KismetCompiler",
            "AssetRegistry",
            "AssetTools",
            "ContentBrowser",
            "DesktopPlatform",
            "EditorSubsystem",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "Projects",
        });
    }
}
