// Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
// Plugin: BlueprintBuster

// BlueprintBusterMacroExpander.cpp
// Macro expansion implementation

#include "BlueprintBusterMacroExpander.h"
#include "BlueprintBuster.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Tunnel.h"
#include "K2Node_IfThenElse.h"

namespace BlueprintBusterMacroExpander
{
	EMacroSupportLevel ClassifyMacro(const UEdGraph* InMacroGraph)
	{
		if (!IsValid(InMacroGraph))
		{
			return EMacroSupportLevel::Unsupported;
		}

		const FString MacroName = InMacroGraph->GetName();

		// Built-in macros with special handling
		if (MacroName == TEXT("FlipFlop") || MacroName == TEXT("DoOnce") ||
			MacroName == TEXT("ForLoop") || MacroName == TEXT("For") ||
			MacroName == TEXT("WhileLoop") || MacroName == TEXT("While") ||
			MacroName == TEXT("IsValid") || MacroName == TEXT("IsNotEmpty"))
		{
			return EMacroSupportLevel::BuiltInMacro;
		}

		// Check macro complexity by counting nodes
		int32 NodeCount = 0;
		int32 ExecNodeCount = 0;
		for (const UEdGraphNode* Node : InMacroGraph->Nodes)
		{
			++NodeCount;
			if (Node && (Node->IsA<UK2Node_IfThenElse>() || 
						Node->IsA<UK2Node_Tunnel>()))
			{
				++ExecNodeCount;
			}
		}

		// Simple linear macro (< 10 nodes, no branches) — fully supported
		if (NodeCount < 10 && ExecNodeCount <= 2)  // Tunnel entry/exit are OK
		{
			return EMacroSupportLevel::StandardMacro;
		}

		// Moderate complexity — standard macro with branches
		if (NodeCount < 50 && ExecNodeCount < 10)
		{
			return EMacroSupportLevel::StandardMacro;
		}

		// Complex macro — likely needs custom handling
		return EMacroSupportLevel::CustomMacro;
	}

	bool AnalyzeMacroInstance(const UK2Node_MacroInstance* InMacroNode,
	                           FMacroInstanceInfo& OutInfo)
	{
		if (!IsValid(InMacroNode))
		{
			return false;
		}

		const UEdGraph* MacroGraph = InMacroNode->GetMacroGraph();
		if (!IsValid(MacroGraph))
		{
			return false;
		}

		OutInfo.MacroGraphName = MacroGraph->GetName();
		OutInfo.SupportLevel = ClassifyMacro(MacroGraph);

		// Collect all data pins on the macro instance
		for (UEdGraphPin* Pin : InMacroNode->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				if (Pin->Direction == EGPD_Input)
				{
					OutInfo.ExecInputPins.Add(Pin);
				}
				else if (Pin->Direction == EGPD_Output)
				{
					OutInfo.ExecOutputPins.Add(Pin);
				}
				continue;
			}

			if (Pin->Direction == EGPD_Input)
			{
				OutInfo.InputPins.Add(Pin->PinName.ToString(), Pin);
			}
			else if (Pin->Direction == EGPD_Output)
			{
				OutInfo.OutputPins.Add(Pin->PinName.ToString(), Pin);
			}
		}

		return true;
	}

	// Forward declare external TraceNode
	extern TSharedPtr<FBPGraphNodeInfo> TraceNodeExternalCall(
		const UEdGraphNode* InNode,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount);

	TSharedPtr<FBPGraphNodeInfo> TryExpandMacroToAST(
		const UK2Node_MacroInstance* InMacroNode,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount)
	{
		if (Depth >= MaxDepth)
		{
			UE_LOG(LogBlueprintBuster, Warning,
				   TEXT("Macro expansion depth limit exceeded for '%s'"),
				   *InMacroNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			return nullptr;
		}

		FMacroInstanceInfo MacroInfo;
		if (!AnalyzeMacroInstance(InMacroNode, MacroInfo))
		{
			return nullptr;
		}

		// Dispatch by macro classification
		if (MacroInfo.SupportLevel == EMacroSupportLevel::BuiltInMacro)
		{
			return ExpandBuiltInMacro(InMacroNode, MacroInfo.SupportLevel, Depth, MaxDepth,
									  VisitedThisChain, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
		}

		if (MacroInfo.SupportLevel == EMacroSupportLevel::StandardMacro)
		{
			// Inline the macro body
			const UEdGraph* MacroGraph = InMacroNode->GetMacroGraph();
			if (!IsValid(MacroGraph) || MacroGraphStack.Contains(MacroGraph))
			{
				return nullptr;  // Recursion or invalid
			}

			// Find macro entry tunnel
			UK2Node_Tunnel* EntryTunnel = nullptr;
			for (UEdGraphNode* Node : MacroGraph->Nodes)
			{
				if (UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node))
				{
					bool bIsEntry = false;
					for (UEdGraphPin* Pin : Tunnel->Pins)
					{
						if (Pin && Pin->Direction == EGPD_Output &&
							Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
						{
							bIsEntry = true;
							break;
						}
					}
					if (bIsEntry)
					{
						EntryTunnel = Tunnel;
						break;
					}
				}
			}

			if (!EntryTunnel)
			{
				return nullptr;
			}

			// Push macro graph onto stack
			MacroGraphStack.Add(MacroGraph);

			TSharedPtr<FBPGraphNodeInfo> ExpandedNode = MakeShared<FBPGraphNodeInfo>();
			ExpandedNode->NodeKind = TEXT("MacroExpanded");
			ExpandedNode->NodeLabel = MacroInfo.MacroGraphName;

			// Find first exec node after entry tunnel
			for (UEdGraphPin* Pin : EntryTunnel->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output &&
					Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					if (Pin->LinkedTo.Num() > 0)
					{
						const UEdGraphNode* FirstNode = Pin->LinkedTo[0]->GetOwningNode();
						if (FirstNode)
						{
							++OutTotalCount;
						}
					}
					break;
				}
			}

			MacroGraphStack.Pop();
			return ExpandedNode;
		}

		// CustomMacro or Unsupported — return nullptr to fall back to stub
		return nullptr;
	}

	TSharedPtr<FBPGraphNodeInfo> ExpandBuiltInMacro(
		const UK2Node_MacroInstance* InMacroNode,
		EMacroSupportLevel InLevel,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount)
	{
		const FString MacroName = InMacroNode->GetMacroGraph()->GetName();

		if (MacroName == TEXT("ForLoop") || MacroName == TEXT("For"))
		{
			return ExpandForLoopMacro(InMacroNode, Depth, MaxDepth,
									  VisitedThisChain, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
		}
		if (MacroName == TEXT("WhileLoop") || MacroName == TEXT("While"))
		{
			return ExpandWhileLoopMacro(InMacroNode, Depth, MaxDepth,
										VisitedThisChain, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
		}
		if (MacroName == TEXT("IsValid") || MacroName == TEXT("IsNotEmpty"))
		{
			return ExpandIsValidMacro(InMacroNode, Depth, MaxDepth,
									   VisitedThisChain, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
		}
		if (MacroName == TEXT("DoOnce"))
		{
			// DoOnce → FlipFlop variant with different behavior
			// Treat as unsupported for now; can be implemented as stateful check
			return nullptr;
		}

		return nullptr;
	}

	TSharedPtr<FBPGraphNodeInfo> ExpandForLoopMacro(
		const UK2Node_MacroInstance* InMacroNode,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount)
	{
		++OutTotalCount;

		FMacroInstanceInfo MacroInfo;
		if (!AnalyzeMacroInstance(InMacroNode, MacroInfo))
		{
			++OutUnsupportedCount;
			return nullptr;
		}

		// Create a pseudo-node representing the for loop
		TSharedPtr<FBPGraphNodeInfo> LoopNode = MakeShared<FBPGraphNodeInfo>();
		LoopNode->NodeKind = TEXT("ForLoop");
		LoopNode->NodeLabel = TEXT("ForLoop");

		// Extract StartIndex, LastIndex from input pins
		UEdGraphPin* StartPin = InMacroNode->FindPin(TEXT("FirstIndex"));
		UEdGraphPin* LastPin = InMacroNode->FindPin(TEXT("LastIndex"));
		UEdGraphPin* LoopBodyPin = nullptr;

		// Find loop body exec output
		for (UEdGraphPin* Pin : InMacroNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output &&
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
				(Pin->PinName == TEXT("LoopBody") || Pin->PinName == TEXT("A")))
			{
				LoopBodyPin = Pin;
				break;
			}
		}

		if (StartPin && LastPin && LoopBodyPin)
		{
			// Store loop parameters as metadata for translator
			const int32 StartVal = FCString::Atoi(*StartPin->DefaultValue);
			const int32 LastVal = FCString::Atoi(*LastPin->DefaultValue);
			LoopNode->ConditionExpression = TEXT("for (int32 LoopIndex = FirstIndex; LoopIndex < LastIndex; ++LoopIndex)");
			LoopNode->NodeLabel = FString::Printf(TEXT("ForLoop [%d to %d]"), StartVal, LastVal);
		}

		UE_LOG(LogBlueprintBuster, Verbose,
			   TEXT("Expanded ForLoop macro with condition: %s"),
			   *LoopNode->ConditionExpression);

		return LoopNode;
	}

	TSharedPtr<FBPGraphNodeInfo> ExpandWhileLoopMacro(
		const UK2Node_MacroInstance* InMacroNode,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount)
	{
		++OutTotalCount;

		// Create while loop node
		TSharedPtr<FBPGraphNodeInfo> LoopNode = MakeShared<FBPGraphNodeInfo>();
		LoopNode->NodeKind = TEXT("WhileLoop");
		LoopNode->NodeLabel = TEXT("WhileLoop");

		// Condition pin — resolve to C++ expression
		UEdGraphPin* CondPin = InMacroNode->FindPin(TEXT("Condition"));
		if (CondPin)
		{
			if (!CondPin->DefaultValue.IsEmpty())
			{
				LoopNode->ConditionExpression = FString::Printf(TEXT("while (%s)"), *CondPin->DefaultValue);
			}
			else if (CondPin->LinkedTo.Num() > 0)
			{
				LoopNode->ConditionExpression = TEXT("while (/* TODO: resolve condition from linked node */)");
			}
			else
			{
				LoopNode->ConditionExpression = TEXT("while (true) /* TODO: validate condition */");
			}
		}

		UE_LOG(LogBlueprintBuster, Verbose,
			   TEXT("Expanded WhileLoop macro with condition: %s"),
			   *LoopNode->ConditionExpression);

		return LoopNode;
	}

	TSharedPtr<FBPGraphNodeInfo> ExpandIsValidMacro(
		const UK2Node_MacroInstance* InMacroNode,
		int32 Depth,
		int32 MaxDepth,
		TSet<const UEdGraphNode*>& VisitedThisChain,
		TArray<const UEdGraph*>& MacroGraphStack,
		int32& OutTotalCount,
		int32& OutUnsupportedCount)
	{
		++OutTotalCount;

		// IsValid macro expands to: if (IsValid(Value)) { True } else { False }
		TSharedPtr<FBPGraphNodeInfo> BranchNode = MakeShared<FBPGraphNodeInfo>();
		BranchNode->NodeKind = TEXT("Branch");
		BranchNode->NodeLabel = TEXT("IsValid");

		UEdGraphPin* ValuePin = InMacroNode->FindPin(TEXT("Value"));
		UEdGraphPin* InvalidPin = InMacroNode->FindPin(TEXT("Object"));
		if (!ValuePin)
		{
			ValuePin = InvalidPin;
		}

		if (ValuePin && !ValuePin->DefaultValue.IsEmpty())
		{
			BranchNode->ConditionExpression = FString::Printf(TEXT("IsValid(%s)"), *ValuePin->DefaultValue);
		}
		else if (ValuePin && ValuePin->LinkedTo.Num() > 0)
		{
			// Would need ResolvePinToCppExpr here from BlueprintBusterParsers
			BranchNode->ConditionExpression = TEXT("IsValid(/* TODO: resolve linked value */)");
		}
		else
		{
			BranchNode->ConditionExpression = TEXT("IsValid(nullptr) /* TODO: provide value */");
		}

		UE_LOG(LogBlueprintBuster, Verbose,
			   TEXT("Expanded IsValid macro with condition: %s"),
			   *BranchNode->ConditionExpression);

		return BranchNode;
	}
}
