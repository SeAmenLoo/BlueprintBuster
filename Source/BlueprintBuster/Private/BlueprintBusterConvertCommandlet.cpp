#include "BlueprintBusterConvertCommandlet.h"

#include "BlueprintBuster.h"
#include "BlueprintBusterDump.h"
#include "BlueprintBusterPython.h"

#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	void GatherBlueprintObjectPathsFromDirectory(const FString& InDirectoryPath, TArray<FString>& OutObjectPaths)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = AssetRegistryModule.Get();

		TArray<FString> ScanPaths;
		ScanPaths.Add(InDirectoryPath);
		AR.ScanPathsSynchronous(ScanPaths, false);

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;
		Filter.PackagePaths.Add(FName(*InDirectoryPath));
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		for (const FAssetData& Asset : Assets)
		{
			OutObjectPaths.Add(Asset.GetObjectPathString());
		}
	}

	bool LooksLikeObjectPath(const FString& InPath)
	{
		return InPath.Contains(TEXT(".")) && InPath.StartsWith(TEXT("/"));
	}
}

UBlueprintBusterConvertCommandlet::UBlueprintBusterConvertCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UBlueprintBusterConvertCommandlet::Main(const FString& Params)
{
	UE_LOG(LogBlueprintBuster, Display, TEXT("=== BlueprintBusterConvert commandlet started ==="));

	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamValues;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamValues);

	const FString* Target = ParamValues.Find(TEXT("Target"));
	const FString* TargetBP = ParamValues.Find(TEXT("TargetBP"));
	const FString* TargetDir = ParamValues.Find(TEXT("TargetDir"));

	const FString* DumpDirParam = ParamValues.Find(TEXT("DumpDir"));
	const FString* OutputDirParam = ParamValues.Find(TEXT("OutputDir"));
	const FString DumpDir = DumpDirParam ? *DumpDirParam : (OutputDirParam ? *OutputDirParam : BlueprintBusterDump::GetDefaultDumpDirectory());

	const FString* CppDirParam = ParamValues.Find(TEXT("CppDir"));
	const FString CppDir = CppDirParam ? *CppDirParam : FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BlueprintBuster"), TEXT("Cpp"));

	const FString* PythonParam = ParamValues.Find(TEXT("Python"));
	const FString PythonExe = PythonParam ? *PythonParam : BlueprintBusterPython::GetDefaultPythonExecutable();

	const FString* ModuleApiParam = ParamValues.Find(TEXT("ModuleAPI"));
	const FString ModuleApi = ModuleApiParam ? *ModuleApiParam : BlueprintBusterPython::GetDefaultModuleApi();

	const FString* MaxDepthS = ParamValues.Find(TEXT("MaxDepth"));
	int32 MaxDepth = 64;
	if (MaxDepthS)
	{
		MaxDepth = FCString::Atoi(**MaxDepthS);
		if (MaxDepth <= 0)
		{
			MaxDepth = 64;
		}
	}

	const bool bFullDump = Switches.Contains(TEXT("FullDump")) || ParamValues.Contains(TEXT("FullDump"));

	if (!Target && !TargetBP && !TargetDir)
	{
		UE_LOG(LogBlueprintBuster, Error, TEXT("Provide -Target, -TargetBP or -TargetDir."));
		return 1;
	}

	IFileManager::Get().MakeDirectory(*DumpDir, true);
	IFileManager::Get().MakeDirectory(*CppDir, true);

	TArray<FString> BlueprintObjectPaths;
	if (TargetBP)
	{
		BlueprintObjectPaths.Add(*TargetBP);
	}
	else if (TargetDir)
	{
		GatherBlueprintObjectPathsFromDirectory(*TargetDir, BlueprintObjectPaths);
	}
	else if (Target)
	{
		if (LooksLikeObjectPath(*Target))
		{
			BlueprintObjectPaths.Add(*Target);
		}
		else
		{
			GatherBlueprintObjectPathsFromDirectory(*Target, BlueprintObjectPaths);
		}
	}

	if (BlueprintObjectPaths.Num() == 0)
	{
		UE_LOG(LogBlueprintBuster, Error, TEXT("No blueprints resolved from target."));
		return 2;
	}

	int32 Successes = 0;
	TSet<FString> VisitedBlueprintPaths;
	for (const FString& ObjectPath : BlueprintObjectPaths)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		if (!IsValid(Blueprint))
		{
			UE_LOG(LogBlueprintBuster, Warning, TEXT("Could not load blueprint at '%s' — skipped"), *ObjectPath);
			continue;
		}

		const FString DumpFilePath = BlueprintBusterDump::MakeDumpFilePath(DumpDir, Blueprint);
		if (!BlueprintBusterDump::DumpBlueprintToJsonFilesRecursive(Blueprint, DumpDir, MaxDepth, 10, nullptr, bFullDump, &VisitedBlueprintPaths))
		{
			if (bFullDump)
			{
				UE_LOG(LogBlueprintBuster, Error, TEXT("FullDump failed for '%s' (unsupported nodes remain). JSON still written: %s"), *Blueprint->GetName(), *DumpFilePath);
			}
			else
			{
				UE_LOG(LogBlueprintBuster, Error, TEXT("Dump failed for '%s'"), *Blueprint->GetName());
			}
			continue;
		}

		FString StdOut;
		FString StdErr;
		int32 ReturnCode = 0;
		const bool bTranslated = BlueprintBusterPython::TranslateDumpToCpp(DumpFilePath, CppDir, ModuleApi, PythonExe, &StdOut, &StdErr, &ReturnCode);
		if (!bTranslated)
		{
			UE_LOG(LogBlueprintBuster, Error, TEXT("Translator failed for '%s' (code %d)"), *Blueprint->GetName(), ReturnCode);
			if (!StdOut.IsEmpty())
			{
				UE_LOG(LogBlueprintBuster, Display, TEXT("%s"), *StdOut);
			}
			if (!StdErr.IsEmpty())
			{
				UE_LOG(LogBlueprintBuster, Error, TEXT("%s"), *StdErr);
			}
			continue;
		}

		if (!StdOut.IsEmpty())
		{
			UE_LOG(LogBlueprintBuster, Display, TEXT("%s"), *StdOut);
		}

		UE_LOG(LogBlueprintBuster, Display, TEXT("Converted %s → %s"), *Blueprint->GetName(), *CppDir);
		Successes++;
	}

	UE_LOG(LogBlueprintBuster, Display, TEXT("=== BlueprintBusterConvert finished: %d blueprint(s) converted ==="), Successes);
	return Successes > 0 ? 0 : 3;
}

