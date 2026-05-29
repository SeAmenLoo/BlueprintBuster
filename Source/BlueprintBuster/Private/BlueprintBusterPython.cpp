#include "BlueprintBusterPython.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

FString BlueprintBusterPython::GetDefaultPythonExecutable()
{
	return TEXT("python");
}

FString BlueprintBusterPython::GetTranslatorScriptPath()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintBuster"));
	if (!Plugin.IsValid())
	{
		return FString();
	}

	return FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Python"), TEXT("bp_translator.py")));
}

FString BlueprintBusterPython::GetDefaultModuleApi()
{
	return FString::Printf(TEXT("%s_API"), *FString(FApp::GetProjectName()).ToUpper());
}

bool BlueprintBusterPython::TranslateDumpToCpp(const FString& DumpFilePath, const FString& OutputDir, const FString& ModuleApi, const FString& PythonExecutable, FString* OutStdOut, FString* OutStdErr, int32* OutReturnCode)
{
	const FString ScriptPath = GetTranslatorScriptPath();
	if (ScriptPath.IsEmpty())
	{
		return false;
	}

	const FString PythonExe = PythonExecutable.IsEmpty() ? GetDefaultPythonExecutable() : PythonExecutable;
	const FString CppOutDir = OutputDir.IsEmpty() ? FPaths::ProjectDir() : OutputDir;
	const FString Api = ModuleApi.IsEmpty() ? GetDefaultModuleApi() : ModuleApi;

	const FString Args = FString::Printf(TEXT("\"%s\" \"%s\" -o \"%s\" --module-api \"%s\""),
		*ScriptPath, *DumpFilePath, *CppOutDir, *Api);

	int32 ReturnCode = 0;
	FString StdOut;
	FString StdErr;
	const bool bOk = FPlatformProcess::ExecProcess(*PythonExe, *Args, &ReturnCode, &StdOut, &StdErr);

	if (OutStdOut)
	{
		*OutStdOut = StdOut;
	}
	if (OutStdErr)
	{
		*OutStdErr = StdErr;
	}
	if (OutReturnCode)
	{
		*OutReturnCode = ReturnCode;
	}

	return bOk && ReturnCode == 0;
}

