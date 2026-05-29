// Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
// Plugin: BlueprintBuster

// BlueprintBusterCommandlet.cpp

#include "BlueprintBusterCommandlet.h"
#include "BlueprintBuster.h"
#include "BlueprintBusterDump.h"

#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"

// ─── UCommandlet ──────────────────────────────────────────────────────────────

UBlueprintBusterCommandlet::UBlueprintBusterCommandlet()
{
    IsClient  = false;
    IsServer  = false;
    IsEditor  = true;
    LogToConsole = true;
    ShowErrorCount = true;
}

int32 UBlueprintBusterCommandlet::Main(const FString& Params)
{
    UE_LOG(LogBlueprintBuster, Display, TEXT("=== BlueprintBuster commandlet started ==="));

    // Parse arguments.
    TArray<FString> Tokens, Switches;
    TMap<FString, FString> ParamValues;
    UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamValues);

    const FString* Target    = ParamValues.Find(TEXT("Target"));
    const FString* TargetBP  = ParamValues.Find(TEXT("TargetBP"));
    const FString* TargetDir = ParamValues.Find(TEXT("TargetDir"));
    const FString* OutputDir = ParamValues.Find(TEXT("OutputDir"));
    const FString* MaxDepthS = ParamValues.Find(TEXT("MaxDepth"));

    const FString OutputDirValue = OutputDir ? *OutputDir : BlueprintBusterDump::GetDefaultDumpDirectory();

    FString TargetBPValue;
    FString TargetDirValue;
    if (TargetBP)
    {
        TargetBPValue = *TargetBP;
    }
    else if (TargetDir)
    {
        TargetDirValue = *TargetDir;
    }
    else if (Target)
    {
        if (Target->Contains(TEXT(".")))
        {
            TargetBPValue = *Target;
        }
        else
        {
            TargetDirValue = *Target;
        }
    }

    if (TargetBPValue.IsEmpty() && TargetDirValue.IsEmpty())
    {
        UE_LOG(LogBlueprintBuster, Error,
               TEXT("Provide -Target, -TargetBP or -TargetDir."));
        return 1;
    }

    int32 MaxDepth = 64;
    if (MaxDepthS)
    {
        MaxDepth = FCString::Atoi(**MaxDepthS);
        if (MaxDepth <= 0) MaxDepth = 64;
    }

    IFileManager::Get().MakeDirectory(*OutputDirValue, true);

    int32 Successes = 0;
    if (!TargetBPValue.IsEmpty())
    {
        if (ProcessBlueprint(TargetBPValue, OutputDirValue, MaxDepth))
        {
            Successes++;
        }
    }
    else if (!TargetDirValue.IsEmpty())
    {
        Successes = ProcessDirectory(TargetDirValue, OutputDirValue, MaxDepth);
    }

    UE_LOG(LogBlueprintBuster, Display,
           TEXT("=== BlueprintBuster finished: %d blueprint(s) dumped ==="),
           Successes);

    return Successes > 0 ? 0 : 2;
}

bool UBlueprintBusterCommandlet::ProcessBlueprint(const FString& InBlueprintPath,
                                                   const FString& InOutputDir,
                                                   int32 InMaxDepth) const
{
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *InBlueprintPath);
    if (!IsValid(Blueprint))
    {
        UE_LOG(LogBlueprintBuster, Warning,
               TEXT("Could not load blueprint at '%s' — skipped"),
               *InBlueprintPath);
        return false;
    }

    const FString OutPath = BlueprintBusterDump::MakeDumpFilePath(InOutputDir, Blueprint);

    FBPDumpData Dump;
    if (!BlueprintBusterDump::DumpBlueprintToJsonFile(Blueprint, OutPath, InMaxDepth, &Dump))
    {
        UE_LOG(LogBlueprintBuster, Error,
               TEXT("Failed to write dump for '%s' to '%s'"),
               *Blueprint->GetName(), *OutPath);
        return false;
    }

    UE_LOG(LogBlueprintBuster, Display,
           TEXT("Dumped %s → %s (%d components, %d defaults, %d nodes, %d functions, %d unsupported)"),
           *Dump.BlueprintName, *OutPath,
           Dump.Components.Num(), Dump.Defaults.Num(),
           Dump.TotalNodeCount, Dump.CustomFunctions.Num(),
           Dump.UnsupportedNodeCount);

    return true;
}

int32 UBlueprintBusterCommandlet::ProcessDirectory(const FString& InDirectoryPath,
                                                    const FString& InOutputDir,
                                                    int32 InMaxDepth) const
{
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AR = AssetRegistryModule.Get();

    // The Asset Registry needs a synchronous scan before listing assets in a Commandlet.
    TArray<FString> ScanPaths;
    ScanPaths.Add(InDirectoryPath);
    AR.ScanPathsSynchronous(ScanPaths, /*bForceRescan=*/false);

    FARFilter Filter;
    Filter.bRecursivePaths   = true;
    Filter.bRecursiveClasses = true;
    Filter.PackagePaths.Add(FName(*InDirectoryPath));
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());

    TArray<FAssetData> Assets;
    AR.GetAssets(Filter, Assets);

    int32 Successes = 0;
    for (const FAssetData& Asset : Assets)
    {
        const FString ObjectPath = Asset.GetObjectPathString();
        if (ProcessBlueprint(ObjectPath, InOutputDir, InMaxDepth))
        {
            Successes++;
        }
    }

    UE_LOG(LogBlueprintBuster, Display,
           TEXT("Directory '%s': %d / %d blueprint(s) dumped"),
           *InDirectoryPath, Successes, Assets.Num());

    return Successes;
}
