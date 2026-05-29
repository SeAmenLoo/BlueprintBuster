#pragma once

#include "CoreMinimal.h"

namespace BlueprintBusterPython
{
	FString GetDefaultPythonExecutable();
	FString GetTranslatorScriptPath();
	FString GetDefaultModuleApi();
	bool TranslateDumpToCpp(const FString& DumpFilePath, const FString& OutputDir, const FString& ModuleApi, const FString& PythonExecutable, FString* OutStdOut = nullptr, FString* OutStdErr = nullptr, int32* OutReturnCode = nullptr);
}

