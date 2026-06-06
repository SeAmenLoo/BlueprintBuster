// Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
// Plugin: BlueprintBuster

// BlueprintBuster.h
// Editor-only module that hosts the UBlueprintBusterCommandlet and shared logging.
// Integrated with macro expansion engine and optional MCP resolver.

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
	void ProcessSelectedBlueprints(bool bGenerateCpp, bool bEnableMacroExpander = true, bool bEnableMCP = false);

	// Helper: get MCP server URL from environment or config
	FString GetMCPServerUrl() const;
	
	// Helper: validate macro expander availability at runtime
	bool IsMacroExpanderAvailable() const;
};
