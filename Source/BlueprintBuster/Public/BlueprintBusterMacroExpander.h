// Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
// Plugin: BlueprintBuster

// BlueprintBusterMacroExpander.h
// Macro expansion strategy: recursively lower macro instances to inline AST

#pragma once

#include "CoreMinimal.h"
#include "BlueprintBusterTypes.h"

class UEdGraph;
class UEdGraphNode;
class UK2Node_MacroInstance;
class UK2Node_Tunnel;

namespace BlueprintBusterMacroExpander
{
	// Macro expansion support level enumeration
	enum class EMacroSupportLevel : uint8
	{
		// Node fully supported — inline expansion
		FullySupported = 0,

		// Standard macro with linear body — auto-expandable
		StandardMacro = 1,

		// Non-standard (custom project macro) — needs manual mapping
		CustomMacro = 2,

		// FlipFlop, ForLoop, WhileLoop — special handling
		BuiltInMacro = 3,

		// Cannot expand safely
		Unsupported = 4
	};

	// Description of a macro instance for expansion
	struct FMacroInstanceInfo
	{
		// The owning macro graph name ("MyMacro", "FlipFlop", "ForLoop")
		FString MacroGraphName;

		// Input data pins (param_name -> pin)
		TMap<FString, class UEdGraphPin*> InputPins;

		// Output data pins (result_name -> pin)
		TMap<FString, class UEdGraphPin*> OutputPins;

		// Exec input/output pins
		TArray<class UEdGraphPin*> ExecInputPins;
		TArray<class UEdGraphPin*> ExecOutputPins;

		// Support level determination
		EMacroSupportLevel SupportLevel = EMacroSupportLevel::Unsupported;
	};

	// Classify the macro by standard patterns
	EMacroSupportLevel ClassifyMacro(const UEdGraph* InMacroGraph);

	// Extract all inputs/outputs from a macro instance node
	bool AnalyzeMacroInstance(const UK2Node_MacroInstance* InMacroNode,
	                           FMacroInstanceInfo& OutInfo);

	// Expand a macro by tracing its body and cloning the AST inline
	// Returns nullptr if expansion fails; caller falls back to Unsupported
	TSharedPtr<FBPGraphNodeInfo> TryExpandMacroToAST(
		const UK2Node_MacroInstance* InMacroNode,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount);

	// Special handling for built-in macros
	TSharedPtr<FBPGraphNodeInfo> ExpandBuiltInMacro(
		const UK2Node_MacroInstance* InMacroNode,
		EMacroSupportLevel InLevel,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount);

	// Expand ForLoop macro: for (int32 Index = StartIndex; Index < LastIndex; ++Index)
	TSharedPtr<FBPGraphNodeInfo> ExpandForLoopMacro(
		const UK2Node_MacroInstance* InMacroNode,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount);

	// Expand WhileLoop macro: while (Condition)
	TSharedPtr<FBPGraphNodeInfo> ExpandWhileLoopMacro(
		const UK2Node_MacroInstance* InMacroNode,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount);

	// Expand IsValid macro: if (IsValid(Value)) then branch
	TSharedPtr<FBPGraphNodeInfo> ExpandIsValidMacro(
		const UK2Node_MacroInstance* InMacroNode,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount);
}
