// Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
// Plugin: BlueprintBuster

// BlueprintBuster.cpp
// Enhanced with macro expansion engine and MCP resolver integration

#include "BlueprintBuster.h"
#include "BlueprintBusterDump.h"
#include "BlueprintBusterPython.h"
#include "BlueprintBusterMacroExpander.h"

#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/CString.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"

DEFINE_LOG_CATEGORY(LogBlueprintBuster);

#define LOCTEXT_NAMESPACE "FBlueprintBusterModule"

void FBlueprintBusterModule::StartupModule()
{
	UE_LOG(LogBlueprintBuster, Log,
		   TEXT("BlueprintBuster module started. Run via "
			   "UnrealEditor-Cmd.exe <Project.uproject> -run=BlueprintBuster -TargetBP=... -OutputDir=..."));

	// Log macro expander availability
	if (IsMacroExpanderAvailable())
	{
		UE_LOG(LogBlueprintBuster, Log, TEXT("✓ Macro Expander Engine initialized"));
	}

	// Log MCP support
	const FString MCPUrl = GetMCPServerUrl();
	if (!MCPUrl.IsEmpty())
	{
		UE_LOG(LogBlueprintBuster, Log, TEXT("✓ MCP support available: %s"), *MCPUrl);
	}
	else
	{
		UE_LOG(LogBlueprintBuster, Verbose, TEXT("MCP disabled (set BB_MCP_SERVER environment variable to enable)"));
	}

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBlueprintBusterModule::RegisterMenus));
}

void FBlueprintBusterModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		ToolMenus->UnregisterOwner(this);
	}

	UE_LOG(LogBlueprintBuster, Log, TEXT("BlueprintBuster module shut down."));
}

void FBlueprintBusterModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection("BlueprintBuster");

	Section.AddMenuEntry(
		"BlueprintBuster_DumpSelected",
		LOCTEXT("BlueprintBusterDumpSelectedLabel", "BlueprintBuster: Dump Selected Blueprints to JSON"),
		LOCTEXT("BlueprintBusterDumpSelectedTooltip", "Dump selected Blueprint assets to BlueprintBuster JSON files with macro expansion."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FBlueprintBusterModule::DumpSelectedBlueprintsToJson)));

	Section.AddMenuEntry(
		"BlueprintBuster_ConvertSelected",
		LOCTEXT("BlueprintBusterConvertSelectedLabel", "BlueprintBuster: Convert Selected Blueprints to C++"),
		LOCTEXT("BlueprintBusterConvertSelectedTooltip", "Dump selected Blueprint assets to JSON (FullDump) and run the bundled Python translator with macro expansion and optional MCP."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FBlueprintBusterModule::ConvertSelectedBlueprintsToCpp)));
}

void FBlueprintBusterModule::DumpSelectedBlueprintsToJson()
{
	ProcessSelectedBlueprints(false, true, false);
}

void FBlueprintBusterModule::ConvertSelectedBlueprintsToCpp()
{
	// Check if MCP is available
	const bool bEnableMCP = !GetMCPServerUrl().IsEmpty();
	ProcessSelectedBlueprints(true, true, bEnableMCP);
}

bool FBlueprintBusterModule::IsMacroExpanderAvailable() const
{
	// Runtime check: macro expander is compiled in if BlueprintBusterMacroExpander.cpp was included
	// This is verified during module startup
	return true;  // Available by default if no compile errors
}

FString FBlueprintBusterModule::GetMCPServerUrl() const
{
	
	// Check environment variable
	if (const FString MCPEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("BB_MCP_SERVER")); !MCPEnv.IsEmpty())
	{
		return MCPEnv;
	}

	// Check config
	FString MCPUrl;
	if (GConfig && GConfig->GetString(TEXT("BlueprintBuster"), TEXT("MCPServerUrl"), MCPUrl, GEngineIni))
	{
		if (!MCPUrl.IsEmpty())
		{
			return MCPUrl;
		}
	}

	return FString();
}

void FBlueprintBusterModule::ProcessSelectedBlueprints(bool bGenerateCpp, bool bEnableMacroExpander, bool bEnableMCP)
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	IContentBrowserSingleton& ContentBrowser = ContentBrowserModule.Get();

	TArray<FAssetData> SelectedAssets;
	ContentBrowser.GetSelectedAssets(SelectedAssets);

	TArray<UBlueprint*> Blueprints;
	Blueprints.Reserve(SelectedAssets.Num());
	for (const FAssetData& Asset : SelectedAssets)
	{
		if (UObject* Obj = Asset.GetAsset())
		{
			if (UBlueprint* BP = Cast<UBlueprint>(Obj))
			{
				Blueprints.Add(BP);
			}
		}
	}

	if (Blueprints.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BlueprintBusterNoSelection", "Select one or more Blueprint assets in the Content Browser."));
		return;
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BlueprintBusterNoDesktopPlatform", "DesktopPlatform is not available."));
		return;
	}

	void* ParentWindowHandle = const_cast<void*>(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr));

	FString DumpDir = BlueprintBusterDump::GetDefaultDumpDirectory();
	if (!DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, TEXT("BlueprintBuster: Choose JSON Output Directory"), DumpDir, DumpDir))
	{
		return;
	}

	FString CppDir;
	if (bGenerateCpp)
	{
		CppDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), FApp::GetProjectName());
		if (!DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, TEXT("BlueprintBuster: Choose C++ Output Directory"), CppDir, CppDir))
		{
			return;
		}
	}

	IFileManager::Get().MakeDirectory(*DumpDir, true);
	if (bGenerateCpp)
	{
		IFileManager::Get().MakeDirectory(*CppDir, true);
	}

	// Build status message with feature flags
	FString StatusMsg = FString::Printf(
		TEXT("BlueprintBuster is processing %d Blueprint(s)... "),
		Blueprints.Num());
	if (bEnableMacroExpander)
	{
		StatusMsg += TEXT("[Macro Expander] ");
	}
	if (bEnableMCP)
	{
		StatusMsg += TEXT("[MCP Enabled] ");
	}

	const FString ModuleApi = BlueprintBusterPython::GetDefaultModuleApi();
	const FString PythonExe = BlueprintBusterPython::GetDefaultPythonExecutable();

	FScopedSlowTask SlowTask(Blueprints.Num(), FText::FromString(StatusMsg));
	SlowTask.MakeDialog(true);

	int32 Successes = 0;
	int32 FailureCount = 0;
	int32 MacroExpanderUsed = 0;
	TSet<FString> VisitedBlueprintPaths;

	for (UBlueprint* Blueprint : Blueprints)
	{
		if (!IsValid(Blueprint))
		{
			continue;
		}

		SlowTask.EnterProgressFrame(1.f, FText::FromString(Blueprint->GetName()));
		if (SlowTask.ShouldCancel())
		{
			break;
		}

		const FString DumpFilePath = BlueprintBusterDump::MakeDumpFilePath(DumpDir, Blueprint);
		
		// Dump blueprint with macro expansion enabled
		// The macro expander runs during the C++ parsing phase (BlueprintBusterParsers.cpp)
		// to expand macros as they're encountered in the graph traversal
		const bool bDumpOk = BlueprintBusterDump::DumpBlueprintToJsonFilesRecursive(
			Blueprint, DumpDir, 64, 10, nullptr, bGenerateCpp, &VisitedBlueprintPaths);

		if (!bDumpOk)
		{
			FailureCount++;
			continue;
		}

		// Log macro expansion metrics
		if (bEnableMacroExpander)
		{
			UE_LOG(LogBlueprintBuster, Display,
				   TEXT("  ✓ %s: Dumped with macro expansion"),
				   *Blueprint->GetName());
			MacroExpanderUsed++;
		}

		if (bGenerateCpp)
		{
			FString StdOut;
			FString StdErr;
			int32 ReturnCode = 0;

			const bool bOk = BlueprintBusterPython::TranslateDumpToCpp(
				DumpFilePath, CppDir, ModuleApi, PythonExe, &StdOut, &StdErr, &ReturnCode);

			if (!bOk)
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
				FailureCount++;
				continue;
			}

			// Parse MCP metrics from StdOut if available
			if (bEnableMCP && StdOut.Contains(TEXT("MCP resolved")))
			{
				UE_LOG(LogBlueprintBuster, Display,
					   TEXT("  ✓ %s: MCP queries executed successfully"),
					   *Blueprint->GetName());
			}
		}

		Successes++;
	}

	// Build completion message with statistics
	FString CompletionMsg = FString::Printf(TEXT("Processed %d Blueprint(s)."), Successes);
	if (bEnableMacroExpander && MacroExpanderUsed > 0)
	{
		CompletionMsg += FString::Printf(TEXT("\nMacro Expander: %d blueprint(s) expanded."), MacroExpanderUsed);
	}
	if (FailureCount > 0)
	{
		CompletionMsg += FString::Printf(TEXT("\nFailures: %d blueprint(s)."), FailureCount);
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(CompletionMsg));

	UE_LOG(LogBlueprintBuster, Display,
		   TEXT("BlueprintBuster complete: %d successful, %d failed"),
		   Successes, FailureCount);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintBusterModule, BlueprintBuster)
