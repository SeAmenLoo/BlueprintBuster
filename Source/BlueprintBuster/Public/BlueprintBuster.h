// Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
// Plugin: BlueprintBuster

// BlueprintBuster.h
// Editor-only module that hosts the UBlueprintBusterCommandlet and shared logging.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBlueprintBuster, Log, All);

class FBlueprintBusterModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    void DumpSelectedBlueprintsToJson();
    void ConvertSelectedBlueprintsToCpp();
    void ProcessSelectedBlueprints(bool bGenerateCpp);
};
