// Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
// Plugin: BlueprintBuster

// BlueprintBusterCommandlet.h
// Editor-only batch tool. Run via:
//   UnrealEditor-Cmd.exe <Project.uproject> -run=BlueprintBuster
//     -Target="/Game/Path/To/BP_Foo.BP_Foo" (unified; treated as folder if no dot)
//     -TargetBP="/Game/Path/To/BP_Foo"  (single asset)
//     -TargetDir="/Game/Path"           (recursive scan — alternative to TargetBP)
//     [-OutputDir="C:/Dumps"]           (where the JSONs are written; default: Project/Saved/BlueprintBuster/Dumps)
//     [-MaxDepth=N]                     (graph-trace cap, default 64)
//     [-Verbose]                        (extra logging)

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BlueprintBusterCommandlet.generated.h"

UCLASS()
class BLUEPRINTBUSTER_API UBlueprintBusterCommandlet : public UCommandlet
{
    GENERATED_BODY()

    //*******************PROPERTIES*******************//
public:
protected:
private:

    //*******************FUNCTIONS********************//
public:
    UBlueprintBusterCommandlet();

    virtual int32 Main(const FString& Params) override;

protected:
private:
    // Processes a single blueprint and writes its dump file.
    // Returns true on success.
    bool ProcessBlueprint(const FString& InBlueprintPath,
                           const FString& InOutputDir,
                           int32 InMaxDepth,
                           bool bFullDump) const;

    // Locates every blueprint under InDirectoryPath using the AssetRegistry and
    // hands each one to ProcessBlueprint. Returns the number of successes.
    int32 ProcessDirectory(const FString& InDirectoryPath,
                            const FString& InOutputDir,
                            int32 InMaxDepth,
                            bool bFullDump) const;
};
