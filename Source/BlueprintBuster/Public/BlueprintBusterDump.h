#pragma once

#include "CoreMinimal.h"
#include "BlueprintBusterTypes.h"

class UBlueprint;

namespace BlueprintBusterDump
{
	FString GetDefaultDumpDirectory();
	FString MakeDumpFilePath(const FString& OutputDir, const UBlueprint* Blueprint);
	bool DumpBlueprintToJsonFile(UBlueprint* Blueprint, const FString& OutputFilePath, int32 MaxDepth, FBPDumpData* OutDump = nullptr);
}

