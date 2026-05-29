// Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
// Plugin: BlueprintBuster

// BlueprintBusterTypes.h
// POD-like structures used by parsers to accumulate dump data before JSON serialisation.
// Intentionally plain (no UPROPERTY) — parsers fill them, the commandlet hands them
// off to FJsonObjectConverter or builds JSON manually.

#pragma once

#include "CoreMinimal.h"

// ─── Component entry (SCS) ────────────────────────────────────────────────────

struct FBPComponentInfo
{
    // The C++ variable name as it will appear in the generated header.
    FString VariableName;

    // Fully qualified UClass path: "/Script/Engine.StaticMeshComponent".
    FString ClassPath;

    // Friendly type name (UStaticMeshComponent) — pre-resolved for the translator.
    FString ClassName;

    // Variable name of the attach-parent component, or empty for the root.
    FString AttachParentVarName;

    // Attach socket on the parent component, if any.
    FString AttachSocketName;

    // True if this node is the SCS root (DefaultSceneRoot or designer-overridden).
    bool bIsRoot = false;

    // Child component variable names (depth-first traversal).
    TArray<FString> ChildVariableNames;
};

// ─── Default value entry (CDO) ────────────────────────────────────────────────

struct FBPPropertyInfo
{
    // Property name exactly as it appears in the UPROPERTY declaration.
    FString PropertyName;

    // FProperty class name: "FloatProperty", "ObjectProperty", "BoolProperty", etc.
    FString PropertyTypeName;

    // For object/class properties — the referenced class path.
    FString InnerTypeName;

    // String-serialised value. JSON output decides whether to wrap in quotes.
    FString ValueString;

    // Editor category. Used by the translator to build `Category = "..."`.
    FString Category;

    // True if EditAnywhere / EditInstanceOnly (drives `InstanceSettings|` prefix).
    bool bIsInstanceEditable = false;

    // True if BlueprintReadOnly / BlueprintReadWrite.
    bool bIsBlueprintVisible = false;

    // Empty == unknown. Storage hints help the translator pick TObjectPtr vs TSoftObjectPtr.
    // Values: "Hard", "Soft", "Class", "SoftClass", "Weak", "None".
    FString PointerStorageHint;
};

// ─── Graph node entry ─────────────────────────────────────────────────────────

struct FBPCallArgumentInfo
{
    FString Name;
    FString Expr;
};

struct FBPGraphNodeInfo
{
    // Short logical type: "Event", "CallFunction", "Branch", "Sequence",
    // "VariableGet", "VariableSet", "MacroInstance", "Unsupported".
    FString NodeKind;

    // Human-readable label: "BeginPlay", "PrintString", "IsValid".
    FString NodeLabel;

    // For CallFunction — the target function name (qualified if non-self).
    FString FunctionName;

    // For CallFunction — the owning class path (empty == Self).
    FString TargetClassPath;

    // For CallFunction — owning class name (e.g. "UKismetSystemLibrary").
    FString TargetClassName;

    // For CallFunction — resolved call target expression ("this", "OtherActor", ...).
    FString TargetExpression;

    // For CallFunction — resolved argument expressions in signature order.
    TArray<FBPCallArgumentInfo> CallArguments;

    // For Branch — resolved condition expression.
    FString ConditionExpression;

    // For VariableSet — resolved value expression for the variable assignment.
    FString ValueExpression;

    // Successor nodes via the default Then/Exec pin.
    TArray<TSharedPtr<FBPGraphNodeInfo>> Next;

    // Branch-specific: nodes reached via the True pin.
    TArray<TSharedPtr<FBPGraphNodeInfo>> BranchTrue;

    // Branch-specific: nodes reached via the False pin.
    TArray<TSharedPtr<FBPGraphNodeInfo>> BranchFalse;

    // If the translator cannot emit C++ for this node, the reason goes here so
    // the generated TODO carries useful information to the human reviewer.
    FString UnsupportedReason;
};

// ─── Graph (one event tree) ───────────────────────────────────────────────────

struct FBPEventTreeInfo
{
    // Owning graph name (EventGraph, ConstructionScript, etc.).
    FString GraphName;

    // Root node — always a UK2Node_Event derivative.
    TSharedPtr<FBPGraphNodeInfo> EventRoot;
};

// ─── Custom-function signature & body ─────────────────────────────────────────

// Single input or output parameter of a custom Blueprint function.
struct FBPFunctionParameter
{
    // Identifier as the developer wrote it in the Blueprint (e.g. "TargetActor").
    FString ParameterName;

    // Logical type string used by the Python translator:
    // "bool", "byte", "int", "int64", "float", "double",
    // "string", "name", "text",
    // "object", "class", "softobject", "softclass", "weakobject",
    // "struct", "enum", "array", "map", "set".
    FString TypeName;

    // For object/class/struct/enum pins — the referenced C++ class or struct
    // name with the UE prefix already applied ("AActor", "UStaticMesh", "FVector").
    FString CppClassName;

    // True if the pin carries a TArray<...> / TMap / TSet container.
    bool bIsArray = false;
    bool bIsMap   = false;
    bool bIsSet   = false;

    // True if pin is passed by reference (output parameter via ref).
    bool bIsReference = false;

    // True if pin is marked Const.
    bool bIsConst = false;
};

// One custom function defined inside a Blueprint (i.e. living in
// UBlueprint::FunctionGraphs, excluding the UserConstructionScript graph).
struct FBPCustomFunctionInfo
{
    // Function name as it will appear in the generated C++ ("Heal", "GetCrewSize").
    FString FunctionName;

    // Source graph name — typically identical to FunctionName.
    FString GraphName;

    // True if the function is marked Pure (Blueprint metadata).
    bool bIsPure = false;

    // True if the function is marked Const.
    bool bIsConst = false;

    // True if the function is marked BlueprintCallable / public to BP.
    bool bIsBlueprintCallable = true;

    // Inputs as declared on UK2Node_FunctionEntry (data output pins).
    TArray<FBPFunctionParameter> InputParameters;

    // Outputs as declared on UK2Node_FunctionResult (data input pins).
    // Empty array == void return.
    TArray<FBPFunctionParameter> ReturnParameters;

    // Body of the function — chain traced from FunctionEntry's Then exec pin.
    // Reuses the same node info shape as event trees, so the translator only
    // needs one renderer.
    TSharedPtr<FBPGraphNodeInfo> FunctionRoot;
};

// ─── Final dump payload ───────────────────────────────────────────────────────

struct FBPDumpData
{
    FString BlueprintName;
    FString BlueprintPath;
    FString ParentClassPath;
    FString ParentClassName;

    // True for AActor-derived BPs, false for ActorComponent / UObject BPs.
    bool bIsActorDerived = false;

    TArray<FBPComponentInfo>      Components;
    TArray<FBPPropertyInfo>       Defaults;
    TArray<FBPEventTreeInfo>      EventTrees;
    TArray<FBPCustomFunctionInfo> CustomFunctions;

    // Diagnostic counters surfaced in INSTRUCTION.md "what was parsed".
    int32 UnsupportedNodeCount = 0;
    int32 TotalNodeCount       = 0;
};
