#pragma once

#include "CoreMinimal.h"
#include "BlueprintBusterTypes.h"

class UBlueprint;

namespace BlueprintBusterDump
{
	FString GetDefaultDumpDirectory();
	FString MakeDumpFilePath(const FString& OutputDir, const UBlueprint* Blueprint);
	bool DumpBlueprintToJsonFile(UBlueprint* Blueprint, const FString& OutputFilePath, int32 MaxDepth, FBPDumpData* OutDump = nullptr, bool bFailOnUnsupported = false);
	bool DumpBlueprintToJsonFilesRecursive(UBlueprint* Blueprint,
	                                      const FString& OutputDir,
	                                      int32 MaxDepth,
	                                      int32 MaxDependencyDepth = 10,
	                                      FBPDumpData* OutRootDump = nullptr,
	                                      bool bFailOnUnsupported = false,
	                                      TSet<FString>* InOutVisitedBlueprintPaths = nullptr);
}

