// Copyright (c) 2026 Shumov Sergey. All Rights Reserved.
// Plugin: BlueprintBuster

// BlueprintBusterParsers.cpp

#include "BlueprintBusterParsers.h"
#include "BlueprintBuster.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/SceneComponent.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"

#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_Knot.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Select.h"
#include "K2Node_Tunnel.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "UObject/Class.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealNames.h"

#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"

namespace BlueprintBusterParsers
{
    // ─── SCS ──────────────────────────────────────────────────────────────────

    static void CollectSCSNodeRecursive(USCS_Node* InNode,
                                        const FString& InAttachParentVarName,
                                        bool bInIsRoot,
                                        TArray<FBPComponentInfo>& OutComponents)
    {
        if (!IsValid(InNode))
        {
            return;
        }

        FBPComponentInfo Info;
        Info.VariableName        = InNode->GetVariableName().ToString();
        Info.AttachParentVarName = InAttachParentVarName;
        Info.AttachSocketName    = InNode->AttachToName.ToString();
        Info.bIsRoot             = bInIsRoot;

        if (UClass* ComponentClass = InNode->ComponentClass)
        {
            Info.ClassPath = ComponentClass->GetPathName();
            Info.ClassName = ComponentClass->GetName();
            // UE Blueprint conventionally drops the U-prefix for component classes
            // in display, but C++ always wants the U-prefix. Reattach if missing.
            if (!Info.ClassName.StartsWith(TEXT("U")) &&
                !Info.ClassName.StartsWith(TEXT("A")))
            {
                Info.ClassName = FString::Printf(TEXT("U%s"), *Info.ClassName);
            }
        }

        // Collect immediate child variable names (depth-first).
        for (USCS_Node* Child : InNode->GetChildNodes())
        {
            if (IsValid(Child))
            {
                Info.ChildVariableNames.Add(Child->GetVariableName().ToString());
            }
        }

        OutComponents.Add(MoveTemp(Info));

        // Recurse — child attaches to this node.
        const FString ThisVarName = InNode->GetVariableName().ToString();
        for (USCS_Node* Child : InNode->GetChildNodes())
        {
            CollectSCSNodeRecursive(Child, ThisVarName, /*bIsRoot=*/false, OutComponents);
        }
    }

    void ParseSimpleConstructionScript(UBlueprint* InBlueprint,
                                       TArray<FBPComponentInfo>& OutComponents)
    {
        OutComponents.Reset();

        if (!IsValid(InBlueprint) || !IsValid(InBlueprint->SimpleConstructionScript))
        {
            return;
        }

        USimpleConstructionScript* SCS = InBlueprint->SimpleConstructionScript;

        // Root nodes are nodes that have no parent (top of the hierarchy).
        for (USCS_Node* RootNode : SCS->GetRootNodes())
        {
            CollectSCSNodeRecursive(RootNode, /*ParentVarName=*/FString(),
                                    /*bIsRoot=*/true, OutComponents);
        }

        UE_LOG(LogBlueprintBuster, Verbose,
               TEXT("SCS: %d components extracted from %s"),
               OutComponents.Num(), *InBlueprint->GetName());
    }

    // ─── CDO ──────────────────────────────────────────────────────────────────

    // Returns true if the property is "interesting" — skip transient/editor-only/etc.
    static bool ShouldSerialiseProperty(const FProperty* InProperty)
    {
        if (!InProperty)
        {
            return false;
        }

        const uint64 Flags = InProperty->PropertyFlags;
        if (Flags & (CPF_Transient | CPF_DuplicateTransient | CPF_EditorOnly | CPF_Deprecated))
        {
            return false;
        }

        // We export anything visible to BP (Edit*/BlueprintRead*) — that's our user surface.
        const bool bVisible =
            (Flags & (CPF_Edit | CPF_BlueprintVisible)) != 0;
        return bVisible;
    }

    static FString ExportPropertyValue(const FProperty* InProperty,
                                        const void* InValuePtr,
                                        FString& OutPointerHint)
    {
        OutPointerHint = TEXT("None");

        if (!InProperty || !InValuePtr)
        {
            return FString();
        }

        // Soft pointers — emit path string, hint tells the translator to use TSoftObjectPtr.
        if (const FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(InProperty))
        {
            const FSoftObjectPtr& Soft =
                *static_cast<const FSoftObjectPtr*>(InValuePtr);
            OutPointerHint = TEXT("Soft");
            return Soft.ToString();
        }
        if (const FSoftClassProperty* SoftClsProp = CastField<FSoftClassProperty>(InProperty))
        {
            const FSoftObjectPtr& Soft =
                *static_cast<const FSoftObjectPtr*>(InValuePtr);
            OutPointerHint = TEXT("SoftClass");
            return Soft.ToString();
        }
        if (const FClassProperty* ClsProp = CastField<FClassProperty>(InProperty))
        {
            const UObject* Obj = ClsProp->GetObjectPropertyValue(InValuePtr);
            OutPointerHint = TEXT("Class");
            return Obj ? Obj->GetPathName() : FString();
        }
        if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(InProperty))
        {
            const UObject* Obj = ObjProp->GetObjectPropertyValue(InValuePtr);
            OutPointerHint = TEXT("Hard");
            return Obj ? Obj->GetPathName() : FString();
        }
        if (const FWeakObjectProperty* WeakProp = CastField<FWeakObjectProperty>(InProperty))
        {
            const FWeakObjectPtr& Weak =
                *static_cast<const FWeakObjectPtr*>(InValuePtr);
            const UObject* Obj = Weak.Get();
            OutPointerHint = TEXT("Weak");
            return Obj ? Obj->GetPathName() : FString();
        }

        // Generic export — covers numeric, string, name, enum, struct, array, map, set.
        FString Exported;
        InProperty->ExportTextItem_Direct(Exported, InValuePtr, /*DefaultData=*/nullptr,
                                          /*Parent=*/nullptr, PPF_None);
        return Exported;
    }

    void ParseClassDefaultObject(UBlueprint* InBlueprint,
                                 TArray<FBPPropertyInfo>& OutDefaults)
    {
        OutDefaults.Reset();

        if (!IsValid(InBlueprint))
        {
            return;
        }

        UClass* GenClass = InBlueprint->GeneratedClass;
        if (!IsValid(GenClass))
        {
            UE_LOG(LogBlueprintBuster, Warning,
                   TEXT("CDO parse: %s has no GeneratedClass (not compiled?)"),
                   *InBlueprint->GetName());
            return;
        }

        UObject* CDO       = GenClass->GetDefaultObject(/*bCreateIfNeeded=*/true);
        UClass*  ParentCls = GenClass->GetSuperClass();
        UObject* ParentCDO = IsValid(ParentCls)
            ? ParentCls->GetDefaultObject(/*bCreateIfNeeded=*/false)
            : nullptr;

        if (!IsValid(CDO))
        {
            return;
        }

        for (TFieldIterator<FProperty> It(GenClass); It; ++It)
        {
            FProperty* Property = *It;
            if (!ShouldSerialiseProperty(Property))
            {
                continue;
            }

            const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);

            // БЕЗОПАСНОЕ ПОЛУЧЕНИЕ РОДИТЕЛЬСКОГО ЗНАЧЕНИЯ:
            // Сначала проверяем, есть ли это свойство в иерархии родительского класса
            const void* ParentPtr = nullptr;
            if (ParentCDO && ParentCDO->IsA(Property->GetOwner<UClass>()))
            {
                ParentPtr = Property->ContainerPtrToValuePtr<void>(ParentCDO);
            }

            // Skip properties identical to the parent CDO — that's noise.
            if (ParentPtr &&
                Property->Identical(ValuePtr, ParentPtr, PPF_DeepComparison))
            {
                continue;
            }

            FBPPropertyInfo Info;
            Info.PropertyName     = Property->GetName();
            Info.PropertyTypeName = Property->GetClass()->GetName();
            Info.Category         = Property->GetMetaData(TEXT("Category"));
            Info.bIsInstanceEditable = (Property->PropertyFlags & CPF_Edit) != 0;
            Info.bIsBlueprintVisible = (Property->PropertyFlags & CPF_BlueprintVisible) != 0;
            Info.ValueString      = ExportPropertyValue(Property, ValuePtr,
                                                         Info.PointerStorageHint);

            // Inner type name for object/class properties — helps translator pick TObjectPtr<X>.
            if (const FObjectPropertyBase* ObjBase = CastField<FObjectPropertyBase>(Property))
            {
                if (IsValid(ObjBase->PropertyClass))
                {
                    Info.InnerTypeName = ObjBase->PropertyClass->GetName();
                    if (!Info.InnerTypeName.StartsWith(TEXT("U")) &&
                        !Info.InnerTypeName.StartsWith(TEXT("A")))
                    {
                        Info.InnerTypeName = FString::Printf(TEXT("U%s"), *Info.InnerTypeName);
                    }
                }
            }

            OutDefaults.Add(MoveTemp(Info));
        }

        UE_LOG(LogBlueprintBuster, Verbose,
               TEXT("CDO: %d non-default properties extracted from %s"),
               OutDefaults.Num(), *InBlueprint->GetName());
    }

    // ─── Execution graph ──────────────────────────────────────────────────────

    // Forward decl for mutual recursion.
    static TSharedPtr<FBPGraphNodeInfo> TraceNode(const UEdGraphNode* InNode,
                                                  int32 Depth,
                                                  int32 MaxDepth,
                                                  TSet<const UEdGraphNode*>& VisitedThisChain,
                                                  TArray<const UEdGraph*>& MacroGraphStack,
                                                  int32& OutTotalCount,
                                                  int32& OutUnsupportedCount);

    // Finds the first connected node from the given exec output pin.
    static const UEdGraphNode* GetLinkedExecNode(const UEdGraphPin* InOutputPin)
    {
        if (!InOutputPin || InOutputPin->LinkedTo.Num() == 0)
        {
            return nullptr;
        }
        const UEdGraphPin* InputPin = InOutputPin->LinkedTo[0];
        return InputPin ? InputPin->GetOwningNode() : nullptr;
    }

    static const UEdGraphNode* GetFirstLinkedExecFromNode(const UEdGraphNode* InNode)
    {
        if (!InNode)
        {
            return nullptr;
        }
        for (UEdGraphPin* Pin : InNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output &&
                Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                return GetLinkedExecNode(Pin);
            }
        }
        return nullptr;
    }

    static const UEdGraphNode* FindMacroEntryExecNode(const UEdGraph* InMacroGraph)
    {
        if (!IsValid(InMacroGraph))
        {
            return nullptr;
        }

        const UK2Node_Tunnel* BestEntry = nullptr;
        for (const UEdGraphNode* Node : InMacroGraph->Nodes)
        {
            const UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node);
            if (!Tunnel)
            {
                continue;
            }

            bool bHasExecOutput = false;
            bool bHasExecInput  = false;
            for (UEdGraphPin* Pin : Tunnel->Pins)
            {
                if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                {
                    continue;
                }
                if (Pin->Direction == EGPD_Output)
                {
                    bHasExecOutput = true;
                }
                else if (Pin->Direction == EGPD_Input)
                {
                    bHasExecInput = true;
                }
            }

            if (bHasExecOutput && !bHasExecInput)
            {
                BestEntry = Tunnel;
                break;
            }

            if (!BestEntry && bHasExecOutput)
            {
                BestEntry = Tunnel;
            }
        }

        return GetFirstLinkedExecFromNode(BestEntry);
    }

    static FString EscapeStringLiteral(const FString& InText)
    {
        FString Out = InText;
        Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
        Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
        Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
        Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
        return Out;
    }

    static FString NormaliseFloatLiteral(FString In)
    {
        In.TrimStartAndEndInline();
        if (In.IsEmpty())
        {
            return TEXT("0.0f");
        }
        if (In.EndsWith(TEXT("f")) || In.EndsWith(TEXT("F")))
        {
            return In;
        }
        if (!In.Contains(TEXT(".")) && !In.Contains(TEXT("e")) && !In.Contains(TEXT("E")))
        {
            In += TEXT(".0");
        }
        In += TEXT("f");
        return In;
    }

    static bool ParseStructDefaultPairs(const FString& InDefault, TMap<FString, FString>& OutPairs)
    {
        OutPairs.Reset();
        FString S = InDefault;
        S.TrimStartAndEndInline();
        if (S.StartsWith(TEXT("(")) && S.EndsWith(TEXT(")")) && S.Len() >= 2)
        {
            S = S.Mid(1, S.Len() - 2);
        }
        if (S.IsEmpty())
        {
            return false;
        }

        TArray<FString> Parts;
        S.ParseIntoArray(Parts, TEXT(","), true);
        for (FString Part : Parts)
        {
            Part.TrimStartAndEndInline();
            FString Key;
            FString Val;
            if (!Part.Split(TEXT("="), &Key, &Val))
            {
                continue;
            }
            Key.TrimStartAndEndInline();
            Val.TrimStartAndEndInline();
            if (!Key.IsEmpty())
            {
                OutPairs.Add(Key, Val);
            }
        }
        return OutPairs.Num() > 0;
    }

    static bool TryResolveStructDefaultToCppExpr(const UEdGraphPin* InPin, FString& OutExpr, FString& OutFailureReason)
    {
        const UScriptStruct* AsStruct = Cast<UScriptStruct>(InPin->PinType.PinSubCategoryObject.Get());
        if (!AsStruct)
        {
            OutFailureReason = TEXT("Struct pin has no PinSubCategoryObject");
            return false;
        }

        TMap<FString, FString> Pairs;
        if (!ParseStructDefaultPairs(InPin->DefaultValue, Pairs))
        {
            OutFailureReason = TEXT("Struct pin has empty or unparseable default value");
            return false;
        }

        const FString StructName = AsStruct->GetName();
        if (StructName == TEXT("Vector"))
        {
            const FString* X = Pairs.Find(TEXT("X"));
            const FString* Y = Pairs.Find(TEXT("Y"));
            const FString* Z = Pairs.Find(TEXT("Z"));
            if (!X || !Y || !Z)
            {
                OutFailureReason = TEXT("FVector default value is missing X/Y/Z");
                return false;
            }
            OutExpr = FString::Printf(TEXT("FVector(%s, %s, %s)"), *NormaliseFloatLiteral(*X), *NormaliseFloatLiteral(*Y), *NormaliseFloatLiteral(*Z));
            return true;
        }
        if (StructName == TEXT("LinearColor"))
        {
            const FString* R = Pairs.Find(TEXT("R"));
            const FString* G = Pairs.Find(TEXT("G"));
            const FString* B = Pairs.Find(TEXT("B"));
            const FString* A = Pairs.Find(TEXT("A"));
            if (!R || !G || !B || !A)
            {
                OutFailureReason = TEXT("FLinearColor default value is missing R/G/B/A");
                return false;
            }
            OutExpr = FString::Printf(TEXT("FLinearColor(%s, %s, %s, %s)"), *NormaliseFloatLiteral(*R), *NormaliseFloatLiteral(*G), *NormaliseFloatLiteral(*B), *NormaliseFloatLiteral(*A));
            return true;
        }
        if (StructName == TEXT("Rotator"))
        {
            const FString* Pitch = Pairs.Find(TEXT("Pitch"));
            const FString* Yaw = Pairs.Find(TEXT("Yaw"));
            const FString* Roll = Pairs.Find(TEXT("Roll"));
            if (!Pitch || !Yaw || !Roll)
            {
                OutFailureReason = TEXT("FRotator default value is missing Pitch/Yaw/Roll");
                return false;
            }
            OutExpr = FString::Printf(TEXT("FRotator(%s, %s, %s)"), *NormaliseFloatLiteral(*Pitch), *NormaliseFloatLiteral(*Yaw), *NormaliseFloatLiteral(*Roll));
            return true;
        }

        OutFailureReason = FString::Printf(TEXT("Struct default value is not supported: %s"), *StructName);
        return false;
    }

    static bool TryResolvePinTypeToCppType(const FEdGraphPinType& InPinType, FString& OutCppType)
    {
        const FName& Cat = InPinType.PinCategory;

        if (Cat == UEdGraphSchema_K2::PC_Boolean) { OutCppType = TEXT("bool"); return true; }
        if (Cat == UEdGraphSchema_K2::PC_Int)     { OutCppType = TEXT("int32"); return true; }
        if (Cat == UEdGraphSchema_K2::PC_Int64)   { OutCppType = TEXT("int64"); return true; }
        if (Cat == UEdGraphSchema_K2::PC_Float)   { OutCppType = TEXT("float"); return true; }
        if (Cat == UEdGraphSchema_K2::PC_Double)  { OutCppType = TEXT("double"); return true; }
        if (Cat == UEdGraphSchema_K2::PC_Real)
        {
            OutCppType = (InPinType.PinSubCategory == UEdGraphSchema_K2::PC_Double) ? TEXT("double") : TEXT("float");
            return true;
        }
        if (Cat == UEdGraphSchema_K2::PC_String)  { OutCppType = TEXT("FString"); return true; }
        if (Cat == UEdGraphSchema_K2::PC_Name)    { OutCppType = TEXT("FName"); return true; }
        if (Cat == UEdGraphSchema_K2::PC_Text)    { OutCppType = TEXT("FText"); return true; }

        if (Cat == UEdGraphSchema_K2::PC_Byte)
        {
            if (const UObject* SubObj = InPinType.PinSubCategoryObject.Get())
            {
                if (const UEnum* Enum = Cast<UEnum>(SubObj))
                {
                    OutCppType = Enum->GetName();
                    return true;
                }
            }
            OutCppType = TEXT("uint8");
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Object ||
            Cat == UEdGraphSchema_K2::PC_Interface)
        {
            const UObject* SubObj = InPinType.PinSubCategoryObject.Get();
            const UClass* AsClass = Cast<UClass>(SubObj);
            FString TypeName = AsClass ? AsClass->GetName() : FString();
            if (TypeName.IsEmpty()) TypeName = TEXT("UObject");
            if (AsClass && AsClass->IsChildOf(AActor::StaticClass()) && !TypeName.StartsWith(TEXT("A")))
            {
                TypeName = FString::Printf(TEXT("A%s"), *TypeName);
            }
            else if (!TypeName.StartsWith(TEXT("U")) && !TypeName.StartsWith(TEXT("A")))
            {
                TypeName = FString::Printf(TEXT("U%s"), *TypeName);
            }
            OutCppType = FString::Printf(TEXT("TObjectPtr<%s>"), *TypeName);
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Class)
        {
            const UObject* SubObj = InPinType.PinSubCategoryObject.Get();
            const UClass* AsClass = Cast<UClass>(SubObj);
            FString TypeName = AsClass ? AsClass->GetName() : FString();
            if (TypeName.IsEmpty()) TypeName = TEXT("UObject");
            if (!TypeName.StartsWith(TEXT("U")) && !TypeName.StartsWith(TEXT("A")))
            {
                TypeName = FString::Printf(TEXT("U%s"), *TypeName);
            }
            OutCppType = FString::Printf(TEXT("TSubclassOf<%s>"), *TypeName);
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Struct)
        {
            const UScriptStruct* AsStruct = Cast<UScriptStruct>(InPinType.PinSubCategoryObject.Get());
            FString TypeName = AsStruct ? AsStruct->GetName() : FString();
            if (TypeName.IsEmpty()) TypeName = TEXT("FStruct");
            if (!TypeName.StartsWith(TEXT("F")))
            {
                TypeName = FString::Printf(TEXT("F%s"), *TypeName);
            }
            OutCppType = TypeName;
            return true;
        }

        return false;
    }

    static bool TryResolveDefaultValueToCppExpr(const UEdGraphPin* InPin, FString& OutExpr, FString& OutFailureReason)
    {
        OutFailureReason.Reset();
        if (!InPin)
        {
            OutFailureReason = TEXT("Pin is null");
            return false;
        }

        const FName& Cat = InPin->PinType.PinCategory;
        const FString Def = InPin->DefaultValue;

        if (Cat == UEdGraphSchema_K2::PC_Boolean)
        {
            OutExpr = (Def.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Def == TEXT("1")) ? TEXT("true") : TEXT("false");
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Byte)
        {
            if (const UObject* SubObj = InPin->PinType.PinSubCategoryObject.Get())
            {
                if (const UEnum* Enum = Cast<UEnum>(SubObj))
                {
                    if (Def.IsEmpty() || Def == TEXT("None"))
                    {
                        OutExpr = FString::Printf(TEXT("static_cast<%s>(0)"), *Enum->GetName());
                        return true;
                    }
                    OutExpr = FString::Printf(TEXT("%s::%s"), *Enum->GetName(), *Def);
                    return true;
                }
            }
            OutExpr = Def.IsEmpty() ? TEXT("0") : Def;
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Int ||
            Cat == UEdGraphSchema_K2::PC_Int64)
        {
            OutExpr = Def.IsEmpty() ? TEXT("0") : Def;
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Float)
        {
            FString V = Def.IsEmpty() ? TEXT("0.0") : Def;
            if (!V.EndsWith(TEXT("f")) && !V.EndsWith(TEXT("F")))
            {
                V += TEXT("f");
            }
            OutExpr = V;
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Double)
        {
            OutExpr = Def.IsEmpty() ? TEXT("0.0") : Def;
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Real)
        {
            if (InPin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
            {
                OutExpr = Def.IsEmpty() ? TEXT("0.0") : Def;
                return true;
            }
            FString V = Def.IsEmpty() ? TEXT("0.0") : Def;
            if (!V.EndsWith(TEXT("f")) && !V.EndsWith(TEXT("F")))
            {
                V += TEXT("f");
            }
            OutExpr = V;
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_String)
        {
            OutExpr = FString::Printf(TEXT("TEXT(\"%s\")"), *EscapeStringLiteral(Def));
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Name)
        {
            OutExpr = (Def.IsEmpty() || Def == TEXT("None"))
                ? TEXT("NAME_None")
                : FString::Printf(TEXT("FName(TEXT(\"%s\"))"), *EscapeStringLiteral(Def));
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Text)
        {
            OutExpr = FString::Printf(TEXT("FText::FromString(TEXT(\"%s\"))"), *EscapeStringLiteral(Def));
            return true;
        }

        if (Cat == UEdGraphSchema_K2::PC_Object ||
            Cat == UEdGraphSchema_K2::PC_Class ||
            Cat == UEdGraphSchema_K2::PC_SoftObject ||
            Cat == UEdGraphSchema_K2::PC_SoftClass)
        {
            if (IsValid(InPin->DefaultObject))
            {
                const FString Path = InPin->DefaultObject->GetPathName();
                const UClass* PinClass = Cast<UClass>(InPin->PinType.PinSubCategoryObject.Get());
                FString TypeName = PinClass ? PinClass->GetName() : FString(TEXT("UObject"));
                if (!TypeName.StartsWith(TEXT("U")) && !TypeName.StartsWith(TEXT("A")))
                {
                    TypeName = FString::Printf(TEXT("U%s"), *TypeName);
                }

                if (Cat == UEdGraphSchema_K2::PC_Class || Cat == UEdGraphSchema_K2::PC_SoftClass)
                {
                    OutExpr = FString::Printf(TEXT("LoadClass<%s>(nullptr, TEXT(\"%s\"))"), *TypeName, *EscapeStringLiteral(Path));
                    return true;
                }

                OutExpr = FString::Printf(TEXT("LoadObject<%s>(nullptr, TEXT(\"%s\"))"), *TypeName, *EscapeStringLiteral(Path));
                return true;
            }
            if (Def.IsEmpty() || Def == TEXT("None"))
            {
                OutExpr = TEXT("nullptr");
                return true;
            }
            OutFailureReason = FString::Printf(TEXT("Pin has asset default value '%s' which is not supported"), *Def);
            return false;
        }

        if (Cat == UEdGraphSchema_K2::PC_Struct)
        {
            if (TryResolveStructDefaultToCppExpr(InPin, OutExpr, OutFailureReason))
            {
                return true;
            }
            return false;
        }

        OutFailureReason = FString::Printf(TEXT("Pin default value category '%s' is not supported"), *Cat.ToString());
        return false;
    }

    static bool CanInlinePureCallFunction(const UK2Node_CallFunction* InCallNode, FString& OutFailureReason)
    {
        OutFailureReason.Reset();
        if (!InCallNode)
        {
            OutFailureReason = TEXT("CallFunction node is null");
            return false;
        }
        if (!InCallNode->IsNodePure())
        {
            OutFailureReason = TEXT("CallFunction is not pure");
            return false;
        }

        for (const UEdGraphPin* Pin : InCallNode->Pins)
        {
            if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                OutFailureReason = TEXT("CallFunction has Exec pins");
                return false;
            }
        }

        UFunction* Func = InCallNode->GetTargetFunction();
        if (!Func)
        {
            OutFailureReason = TEXT("CallFunction has no target UFunction");
            return false;
        }

        bool bHasReturn = false;
        for (TFieldIterator<FProperty> It(Func); It; ++It)
        {
            FProperty* Prop = *It;
            if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Parm))
            {
                continue;
            }
            if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                bHasReturn = true;
                continue;
            }
            if (Prop->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm))
            {
                OutFailureReason = TEXT("CallFunction has out/ref parameters");
                return false;
            }
        }
        if (!bHasReturn)
        {
            OutFailureReason = TEXT("CallFunction has no return value");
            return false;
        }

        for (const UEdGraphPin* Pin : InCallNode->Pins)
        {
            if (!Pin)
            {
                continue;
            }
            if (Pin->Direction == EGPD_Output &&
                Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
                Pin->PinName != UEdGraphSchema_K2::PN_ReturnValue)
            {
                OutFailureReason = TEXT("CallFunction has non-return output pin");
                return false;
            }
        }

        return true;
    }

    static bool ResolvePinToCppExprRecursive(const UEdGraphPin* InPin,
                                            FString& OutExpr,
                                            FString& OutFailureReason,
                                            int32 Depth,
                                            int32 MaxDepth,
                                            TSet<const UEdGraphPin*>& VisitingPins);

    static bool ResolveCallFunctionInlineExpr(const UK2Node_CallFunction* InCallNode,
                                              FString& OutExpr,
                                              FString& OutFailureReason,
                                              int32 Depth,
                                              int32 MaxDepth,
                                              TSet<const UEdGraphPin*>& VisitingPins)
    {
        FString CanInlineReason;
        if (!CanInlinePureCallFunction(InCallNode, CanInlineReason))
        {
            OutFailureReason = FString::Printf(TEXT("Pure CallFunction cannot be inlined: %s"), *CanInlineReason);
            return false;
        }

        UFunction* Func = InCallNode->GetTargetFunction();
        if (!Func)
        {
            OutFailureReason = TEXT("Pure CallFunction has no target UFunction");
            return false;
        }

        const bool bIsStatic = Func->HasAnyFunctionFlags(FUNC_Static);
        FString TargetExpr;
        FString TargetClassName;

        if (bIsStatic)
        {
            const UClass* TargetCls = InCallNode->FunctionReference.GetMemberParentClass();
            if (!IsValid(TargetCls))
            {
                OutFailureReason = TEXT("Pure CallFunction static target class is missing");
                return false;
            }
            TargetClassName = TargetCls->GetName();
        }
        else
        {
            const UEdGraphPin* SelfPin = InCallNode->FindPin(UEdGraphSchema_K2::PN_Self);
            if (SelfPin && SelfPin->LinkedTo.Num() > 0)
            {
                if (!ResolvePinToCppExprRecursive(SelfPin, TargetExpr, OutFailureReason, Depth + 1, MaxDepth, VisitingPins))
                {
                    OutFailureReason = FString::Printf(TEXT("Pure CallFunction target expression cannot be resolved: %s"), *OutFailureReason);
                    return false;
                }
            }
            else
            {
                TargetExpr = TEXT("this");
            }
        }

        TArray<FString> ArgExprs;
        for (TFieldIterator<FProperty> It(Func); It; ++It)
        {
            FProperty* Prop = *It;
            if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Parm) || Prop->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                continue;
            }

            const UEdGraphPin* ParamPin = InCallNode->FindPin(Prop->GetFName());
            FString ArgExpr;
            if (!ResolvePinToCppExprRecursive(ParamPin, ArgExpr, OutFailureReason, Depth + 1, MaxDepth, VisitingPins))
            {
                OutFailureReason = FString::Printf(TEXT("Pure CallFunction argument '%s' cannot be resolved: %s"), *Prop->GetName(), *OutFailureReason);
                return false;
            }
            ArgExprs.Add(MoveTemp(ArgExpr));
        }

        const FString Args = FString::Join(ArgExprs, TEXT(", "));
        if (bIsStatic)
        {
            OutExpr = FString::Printf(TEXT("%s::%s(%s)"), *TargetClassName,
                                      *InCallNode->FunctionReference.GetMemberName().ToString(),
                                      *Args);
        }
        else if (TargetExpr == TEXT("this"))
        {
            OutExpr = FString::Printf(TEXT("this->%s(%s)"),
                                      *InCallNode->FunctionReference.GetMemberName().ToString(),
                                      *Args);
        }
        else
        {
            OutExpr = FString::Printf(TEXT("(%s)->%s(%s)"), *TargetExpr,
                                      *InCallNode->FunctionReference.GetMemberName().ToString(),
                                      *Args);
        }

        OutExpr = FString::Printf(TEXT("(%s)"), *OutExpr);
        return true;
    }

    static bool ResolveOutputPinToCppExprRecursive(const UEdGraphPin* InOutputPin,
                                                  FString& OutExpr,
                                                  FString& OutFailureReason,
                                                  int32 Depth,
                                                  int32 MaxDepth,
                                                  TSet<const UEdGraphPin*>& VisitingPins)
    {
        if (!InOutputPin)
        {
            OutFailureReason = TEXT("Output pin is null");
            return false;
        }
        if (Depth >= MaxDepth)
        {
            OutFailureReason = TEXT("Expression depth limit exceeded");
            return false;
        }

        const UEdGraphNode* Node = InOutputPin->GetOwningNode();
        if (!Node)
        {
            OutFailureReason = TEXT("Output pin has no owning node");
            return false;
        }

        if (const UK2Node_Knot* KnotNode = Cast<UK2Node_Knot>(Node))
        {
            const UEdGraphPin* InputPin = nullptr;
            for (const UEdGraphPin* Pin : KnotNode->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Input &&
                    Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                {
                    InputPin = Pin;
                    break;
                }
            }
            if (!InputPin)
            {
                OutFailureReason = TEXT("Reroute node has no input pin");
                return false;
            }
            return ResolvePinToCppExprRecursive(InputPin, OutExpr, OutFailureReason, Depth + 1, MaxDepth, VisitingPins);
        }

        if (const UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(Node))
        {
            OutExpr = GetNode->VariableReference.GetMemberName().ToString();
            if (OutExpr.IsEmpty())
            {
                OutFailureReason = TEXT("VariableGet has empty variable name");
                return false;
            }
            return true;
        }

        if (const UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(Node))
        {
            if (InOutputPin->PinName == FName(TEXT("Output_Get")))
            {
                OutExpr = SetNode->VariableReference.GetMemberName().ToString();
                if (OutExpr.IsEmpty())
                {
                    OutFailureReason = TEXT("VariableSet has empty variable name");
                    return false;
                }
                return true;
            }
            OutFailureReason = TEXT("Only VariableSet output-get pin can be used as expression");
            return false;
        }

        if (const UK2Node_EnumLiteral* EnumNode = Cast<UK2Node_EnumLiteral>(Node))
        {
            if (!IsValid(EnumNode->Enum))
            {
                OutFailureReason = TEXT("EnumLiteral has no Enum");
                return false;
            }
            const UEdGraphPin* EnumInputPin = EnumNode->FindPin(UK2Node_EnumLiteral::GetEnumInputPinName());
            if (!EnumInputPin)
            {
                OutFailureReason = TEXT("EnumLiteral has no Enum input pin");
                return false;
            }
            if (EnumInputPin->DefaultValue.IsEmpty())
            {
                OutFailureReason = TEXT("EnumLiteral has empty default value");
                return false;
            }
            OutExpr = FString::Printf(TEXT("%s::%s"), *EnumNode->Enum->GetName(), *EnumInputPin->DefaultValue);
            return true;
        }

        if (const UK2Node_MakeArray* MakeArrayNode = Cast<UK2Node_MakeArray>(Node))
        {
            UEdGraphPin* OutPin = MakeArrayNode->GetOutputPin();
            if (!OutPin)
            {
                OutFailureReason = TEXT("MakeArray has no output pin");
                return false;
            }
            FString ElemCppType;
            if (!TryResolvePinTypeToCppType(OutPin->PinType, ElemCppType))
            {
                OutFailureReason = TEXT("MakeArray element type is not supported");
                return false;
            }

            struct FIndexedPin
            {
                int32 Index = 0;
                const UEdGraphPin* Pin = nullptr;
            };
            TArray<FIndexedPin> Inputs;
            for (const UEdGraphPin* Pin : MakeArrayNode->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Input ||
                    Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    continue;
                }
                const FString NameStr = Pin->PinName.ToString();
                if (!NameStr.IsNumeric())
                {
                    continue;
                }
                Inputs.Add({ FCString::Atoi(*NameStr), Pin });
            }
            Inputs.Sort([](const FIndexedPin& A, const FIndexedPin& B) { return A.Index < B.Index; });

            TArray<FString> Items;
            Items.Reserve(Inputs.Num());
            for (const FIndexedPin& P : Inputs)
            {
                FString ItemExpr;
                if (!ResolvePinToCppExprRecursive(P.Pin, ItemExpr, OutFailureReason, Depth + 1, MaxDepth, VisitingPins))
                {
                    OutFailureReason = FString::Printf(TEXT("MakeArray input %d cannot be resolved: %s"), P.Index, *OutFailureReason);
                    return false;
                }
                Items.Add(MoveTemp(ItemExpr));
            }

            const FString Joined = FString::Join(Items, TEXT(", "));
            OutExpr = FString::Printf(TEXT("TArray<%s>{%s}"), *ElemCppType, *Joined);
            OutExpr = FString::Printf(TEXT("(%s)"), *OutExpr);
            return true;
        }

        if (const UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
        {
            const UEdGraphPin* IndexPin = SelectNode->GetIndexPin();
            if (!IndexPin)
            {
                OutFailureReason = TEXT("Select has no index pin");
                return false;
            }
            if (IndexPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Boolean)
            {
                OutFailureReason = TEXT("Only Select(bool) is supported");
                return false;
            }

            TArray<UEdGraphPin*> OptionPins;
            SelectNode->GetOptionPins(OptionPins);
            if (OptionPins.Num() != 2)
            {
                OutFailureReason = TEXT("Select(bool) must have exactly 2 options");
                return false;
            }

            UEdGraphPin* A = nullptr;
            UEdGraphPin* B = nullptr;
            for (UEdGraphPin* P : OptionPins)
            {
                if (P && P->PinName == TEXT("A")) A = P;
                if (P && P->PinName == TEXT("B")) B = P;
            }
            if (!A || !B)
            {
                A = OptionPins[0];
                B = OptionPins[1];
            }

            FString CondExpr;
            if (!ResolvePinToCppExprRecursive(IndexPin, CondExpr, OutFailureReason, Depth + 1, MaxDepth, VisitingPins))
            {
                OutFailureReason = FString::Printf(TEXT("Select condition cannot be resolved: %s"), *OutFailureReason);
                return false;
            }

            FString TrueExpr;
            if (!ResolvePinToCppExprRecursive(A, TrueExpr, OutFailureReason, Depth + 1, MaxDepth, VisitingPins))
            {
                OutFailureReason = FString::Printf(TEXT("Select A cannot be resolved: %s"), *OutFailureReason);
                return false;
            }

            FString FalseExpr;
            if (!ResolvePinToCppExprRecursive(B, FalseExpr, OutFailureReason, Depth + 1, MaxDepth, VisitingPins))
            {
                OutFailureReason = FString::Printf(TEXT("Select B cannot be resolved: %s"), *OutFailureReason);
                return false;
            }

            OutExpr = FString::Printf(TEXT("((%s) ? (%s) : (%s))"), *CondExpr, *TrueExpr, *FalseExpr);
            return true;
        }

        if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
        {
            if (InOutputPin->PinName != UEdGraphSchema_K2::PN_ReturnValue)
            {
                OutFailureReason = TEXT("Only ReturnValue pin can be used as expression for CallFunction");
                return false;
            }
            return ResolveCallFunctionInlineExpr(CallNode, OutExpr, OutFailureReason, Depth + 1, MaxDepth, VisitingPins);
        }

        if (const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
        {
            const UEdGraph* MacroGraph = MacroNode->GetMacroGraph();
            if (IsValid(MacroGraph) && MacroGraph->GetName() == TEXT("FlipFlop"))
            {
                const FString PinName = InOutputPin->PinName.ToString();
                if (InOutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean &&
                    (PinName.Equals(TEXT("IsA"), ESearchCase::IgnoreCase) ||
                     PinName.Equals(TEXT("bIsA"), ESearchCase::IgnoreCase)))
                {
                    const FString StateVar = FString::Printf(TEXT("bFlipFlop_%s"),
                                                             *MacroNode->NodeGuid.ToString(EGuidFormats::Digits));
                    OutExpr = FString::Printf(TEXT("bFlipFlopIsA_%s"),
                                              *MacroNode->NodeGuid.ToString(EGuidFormats::Digits));
                    return true;
                }
            }
        }

        OutFailureReason = FString::Printf(TEXT("Output pin node class %s is not supported for expression lowering"),
                                           *Node->GetClass()->GetName());
        return false;
    }

    static bool ResolvePinToCppExprRecursive(const UEdGraphPin* InPin,
                                            FString& OutExpr,
                                            FString& OutFailureReason,
                                            int32 Depth,
                                            int32 MaxDepth,
                                            TSet<const UEdGraphPin*>& VisitingPins)
    {
        if (!InPin)
        {
            OutFailureReason = TEXT("Pin is null");
            return false;
        }
        if (Depth >= MaxDepth)
        {
            OutFailureReason = TEXT("Expression depth limit exceeded");
            return false;
        }
        if (VisitingPins.Contains(InPin))
        {
            OutFailureReason = TEXT("Expression cycle detected");
            return false;
        }

        VisitingPins.Add(InPin);
        const bool bOk = [&]()
        {
            if (InPin->LinkedTo.Num() > 0 && InPin->LinkedTo[0])
            {
                return ResolveOutputPinToCppExprRecursive(InPin->LinkedTo[0], OutExpr, OutFailureReason, Depth + 1, MaxDepth, VisitingPins);
            }
            return TryResolveDefaultValueToCppExpr(InPin, OutExpr, OutFailureReason);
        }();
        VisitingPins.Remove(InPin);
        return bOk;
    }

    static bool ResolvePinToCppExpr(const UEdGraphPin* InPin, FString& OutExpr, FString& OutFailureReason, int32 MaxDepth)
    {
        TSet<const UEdGraphPin*> VisitingPins;
        return ResolvePinToCppExprRecursive(InPin, OutExpr, OutFailureReason, 0, MaxDepth, VisitingPins);
    }

    static bool NodeHasExecPins(const UEdGraphNode* InNode)
    {
        if (!InNode)
        {
            return false;
        }
        for (const UEdGraphPin* Pin : InNode->Pins)
        {
            if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                return true;
            }
        }
        return false;
    }

    // Walks the linear chain starting at InStartNode, following the default exec then-pin.
    static void TraceLinearChain(const UEdGraphNode* InStartNode,
                                 int32 Depth,
                                 int32 MaxDepth,
                                 TSet<const UEdGraphNode*>& VisitedThisChain,
                                 TArray<TSharedPtr<FBPGraphNodeInfo>>& OutChain,
                                  TArray<const UEdGraph*>& MacroGraphStack,
                                 int32& OutTotalCount,
                                 int32& OutUnsupportedCount)
    {
        const UEdGraphNode* Current = InStartNode;
        while (Current && Depth < MaxDepth)
        {
            if (VisitedThisChain.Contains(Current))
            {
                // Cycle — stop, but mark as cycle-end for the translator.
                break;
            }
            VisitedThisChain.Add(Current);

            TSharedPtr<FBPGraphNodeInfo> NodeInfo =
                TraceNode(Current, Depth, MaxDepth, VisitedThisChain, MacroGraphStack,
                          OutTotalCount, OutUnsupportedCount);
            if (!NodeInfo.IsValid())
            {
                break;
            }
            OutChain.Add(NodeInfo);

            // Default "Then" pin — find first Exec output that is not the Else of a Branch.
            const UEdGraphPin* NextPin = nullptr;
            for (UEdGraphPin* Pin : Current->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Output &&
                    Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    // For Branch/Sequence we hand off via dedicated traversal — bail here.
                    if (Current->IsA<UK2Node_IfThenElse>() ||
                        Current->IsA<UK2Node_ExecutionSequence>() ||
                        Current->IsA<UK2Node_MacroInstance>() ||
                        (Current->IsA<UK2Node_VariableGet>() && NodeHasExecPins(Current)))
                    {
                        NextPin = nullptr;
                        break;
                    }
                    NextPin = Pin;
                    break;
                }
            }

            const UEdGraphNode* NextExecNode = GetLinkedExecNode(NextPin);

            if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Current))
            {
                if (NodeInfo.IsValid() && NodeInfo->NodeKind == TEXT("CallFunction") && !CallNode->IsNodePure())
                {
                    if (const UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(NextExecNode))
                    {
                        const UEdGraphPin* ReturnPin = CallNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
                        const UEdGraphPin* SetValuePin = SetNode->FindPin(SetNode->GetVarName(), EGPD_Input);
                        const FString SetVarName = SetNode->VariableReference.GetMemberName().ToString();

                        const UEdGraphPin* SourcePin = nullptr;
                        if (SetValuePin && SetValuePin->LinkedTo.Num() > 0)
                        {
                            SourcePin = SetValuePin->LinkedTo[0];
                            while (SourcePin)
                            {
                                const UK2Node_Knot* Knot = Cast<UK2Node_Knot>(SourcePin->GetOwningNode());
                                if (!Knot)
                                {
                                    break;
                                }
                                const UEdGraphPin* KnotInput = nullptr;
                                for (const UEdGraphPin* P : Knot->Pins)
                                {
                                    if (P && P->Direction == EGPD_Input &&
                                        P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                                    {
                                        KnotInput = P;
                                        break;
                                    }
                                }
                                if (!KnotInput || KnotInput->LinkedTo.Num() == 0 || !KnotInput->LinkedTo[0])
                                {
                                    break;
                                }
                                SourcePin = KnotInput->LinkedTo[0];
                            }
                        }

                        const bool bIsReturnLinkedToSet =
                            ReturnPin && SetValuePin &&
                            SourcePin == ReturnPin &&
                            ReturnPin->LinkedTo.Num() == 1 &&
                            !SetVarName.IsEmpty();

                        if (bIsReturnLinkedToSet)
                        {
                            NodeInfo->AssignToVariable = SetVarName;

                            const UEdGraphPin* SetThenPin = nullptr;
                            for (UEdGraphPin* Pin : SetNode->Pins)
                            {
                                if (Pin && Pin->Direction == EGPD_Output &&
                                    Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
                                    Pin->PinName == UEdGraphSchema_K2::PN_Then)
                                {
                                    SetThenPin = Pin;
                                    break;
                                }
                            }

                            Current = GetLinkedExecNode(SetThenPin);
                            Depth += 2;
                            continue;
                        }
                    }
                }
            }

            Current = NextExecNode;
            ++Depth;
        }
    }

    static TSharedPtr<FBPGraphNodeInfo> TraceNode(const UEdGraphNode* InNode,
                                                  int32 Depth,
                                                  int32 MaxDepth,
                                                  TSet<const UEdGraphNode*>& VisitedThisChain,
                                                  TArray<const UEdGraph*>& MacroGraphStack,
                                                  int32& OutTotalCount,
                                                  int32& OutUnsupportedCount)
    {
        if (!InNode || Depth >= MaxDepth)
        {
            return nullptr;
        }

        ++OutTotalCount;
        TSharedPtr<FBPGraphNodeInfo> Info = MakeShared<FBPGraphNodeInfo>();

        // Event entry.
        if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(InNode))
        {
            Info->NodeKind  = TEXT("Event");
            const FName FnName = EventNode->GetFunctionName();
            Info->NodeLabel = FnName.IsNone() ? EventNode->GetNodeTitle(ENodeTitleType::ListView).ToString() : FnName.ToString();
            return Info;
        }

        // Function call.
        if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(InNode))
        {
            Info->NodeKind     = TEXT("CallFunction");
            Info->FunctionName = CallNode->FunctionReference.GetMemberName().ToString();
            Info->NodeLabel    = Info->FunctionName;

            const UClass* TargetCls = CallNode->FunctionReference.GetMemberParentClass();
            if (IsValid(TargetCls))
            {
                Info->TargetClassPath = TargetCls->GetPathName();
                Info->TargetClassName = TargetCls->GetName();
            }

            UFunction* Func = CallNode->GetTargetFunction();
            if (!Func)
            {
                Info->NodeKind          = TEXT("Unsupported");
                Info->NodeLabel         = TEXT("UK2Node_CallFunction");
                Info->UnsupportedReason = TEXT("CallFunction has no target UFunction");
                ++OutUnsupportedCount;
                return Info;
            }

            for (UEdGraphPin* Pin : CallNode->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Output &&
                    Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
                    Pin->PinName != UEdGraphSchema_K2::PN_ReturnValue)
                {
                    Info->NodeKind          = TEXT("Unsupported");
                    Info->UnsupportedReason = TEXT("CallFunction out-parameters are not supported");
                    ++OutUnsupportedCount;
                    return Info;
                }
            }

            const bool bIsStatic = Func->HasAnyFunctionFlags(FUNC_Static);
            if (!bIsStatic)
            {
                UEdGraphPin* SelfPin = CallNode->FindPin(UEdGraphSchema_K2::PN_Self);
                if (SelfPin && SelfPin->LinkedTo.Num() > 0)
                {
                    FString TargetExpr;
                    FString FailureReason;
                    if (!ResolvePinToCppExpr(SelfPin, TargetExpr, FailureReason, MaxDepth))
                    {
                        Info->NodeKind          = TEXT("Unsupported");
                        Info->UnsupportedReason = FString::Printf(TEXT("CallFunction target expression cannot be resolved: %s"), *FailureReason);
                        ++OutUnsupportedCount;
                        return Info;
                    }
                    Info->TargetExpression = TargetExpr;
                }
                else
                {
                    Info->TargetExpression = TEXT("this");
                }
            }

            for (TFieldIterator<FProperty> It(Func); It; ++It)
            {
                FProperty* Prop = *It;
                if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Parm))
                {
                    continue;
                }
                if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
                {
                    continue;
                }

                UEdGraphPin* ParamPin = CallNode->FindPin(Prop->GetFName());
                FString Expr;
                FString FailureReason;
                if (!ResolvePinToCppExpr(ParamPin, Expr, FailureReason, MaxDepth))
                {
                    Info->NodeKind          = TEXT("Unsupported");
                    Info->UnsupportedReason = FString::Printf(TEXT("CallFunction argument '%s' cannot be resolved: %s"), *Prop->GetName(), *FailureReason);
                    ++OutUnsupportedCount;
                    return Info;
                }

                FBPCallArgumentInfo Arg;
                Arg.Name = Prop->GetName();
                Arg.Expr = Expr;
                Info->CallArguments.Add(MoveTemp(Arg));
            }

            return Info;
        }

        // Branch (If/Then/Else).
        if (const UK2Node_IfThenElse* BranchNode = Cast<UK2Node_IfThenElse>(InNode))
        {
            Info->NodeKind  = TEXT("Branch");
            Info->NodeLabel = TEXT("Branch");

            FString CondExpr;
            FString FailureReason;
            if (!ResolvePinToCppExpr(BranchNode->GetConditionPin(), CondExpr, FailureReason, MaxDepth))
            {
                Info->NodeKind          = TEXT("Unsupported");
                Info->UnsupportedReason = FString::Printf(TEXT("Branch condition cannot be resolved: %s"), *FailureReason);
                ++OutUnsupportedCount;
                return Info;
            }
            Info->ConditionExpression = CondExpr;

            // Find Then / Else pins by name (UE convention).
            const UEdGraphPin* ThenPin = BranchNode->GetThenPin();
            const UEdGraphPin* ElsePin = BranchNode->GetElsePin();

            if (const UEdGraphNode* ThenNext = GetLinkedExecNode(ThenPin))
            {
                TraceLinearChain(ThenNext, Depth + 1, MaxDepth, VisitedThisChain,
                                 Info->BranchTrue, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
            }
            if (const UEdGraphNode* ElseNext = GetLinkedExecNode(ElsePin))
            {
                TraceLinearChain(ElseNext, Depth + 1, MaxDepth, VisitedThisChain,
                                 Info->BranchFalse, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
            }
            return Info;
        }

        // Sequence — flatten into Next chain (preserving each branch in order).
        if (const UK2Node_ExecutionSequence* SeqNode = Cast<UK2Node_ExecutionSequence>(InNode))
        {
            Info->NodeKind  = TEXT("Sequence");
            Info->NodeLabel = TEXT("Sequence");

            for (UEdGraphPin* Pin : SeqNode->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Output &&
                    Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    if (const UEdGraphNode* SeqNext = GetLinkedExecNode(Pin))
                    {
                        TraceLinearChain(SeqNext, Depth + 1, MaxDepth, VisitedThisChain,
                                         Info->Next, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
                    }
                }
            }
            return Info;
        }

        // Variable get / set.
        if (const UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(InNode))
        {
            const bool bHasExec = NodeHasExecPins(GetNode);
            if (bHasExec)
            {
                const FString VarName = GetNode->VariableReference.GetMemberName().ToString();
                if (VarName.IsEmpty())
                {
                    Info->NodeKind          = TEXT("Unsupported");
                    Info->NodeLabel         = TEXT("VariableGet");
                    Info->UnsupportedReason = TEXT("Validated VariableGet has empty variable name");
                    ++OutUnsupportedCount;
                    return Info;
                }

                Info->NodeKind  = TEXT("Branch");
                Info->NodeLabel = TEXT("IsValid");
                Info->ConditionExpression = FString::Printf(TEXT("IsValid(%s)"), *VarName);

                const UEdGraphPin* ThenPin = nullptr;
                const UEdGraphPin* ElsePin = nullptr;
                for (const UEdGraphPin* Pin : GetNode->Pins)
                {
                    if (!Pin || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                    {
                        continue;
                    }
                    if (Pin->PinName == UEdGraphSchema_K2::PN_Then)
                    {
                        ThenPin = Pin;
                    }
                    else if (Pin->PinName == UEdGraphSchema_K2::PN_Else)
                    {
                        ElsePin = Pin;
                    }
                }

                if (const UEdGraphNode* ThenNext = GetLinkedExecNode(ThenPin))
                {
                    TraceLinearChain(ThenNext, Depth + 1, MaxDepth, VisitedThisChain,
                                     Info->BranchTrue, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
                }
                if (const UEdGraphNode* ElseNext = GetLinkedExecNode(ElsePin))
                {
                    TraceLinearChain(ElseNext, Depth + 1, MaxDepth, VisitedThisChain,
                                     Info->BranchFalse, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
                }

                return Info;
            }

            Info->NodeKind  = TEXT("VariableGet");
            Info->NodeLabel = GetNode->VariableReference.GetMemberName().ToString();
            return Info;
        }
        if (const UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(InNode))
        {
            Info->NodeKind  = TEXT("VariableSet");
            Info->NodeLabel = SetNode->VariableReference.GetMemberName().ToString();
            FString ValueExpr;
            FString FailureReason;
            UEdGraphPin* ValuePin = SetNode->FindPin(SetNode->GetVarName(), EGPD_Input);
            if (!ResolvePinToCppExpr(ValuePin, ValueExpr, FailureReason, MaxDepth))
            {
                Info->NodeKind          = TEXT("Unsupported");
                Info->UnsupportedReason = FString::Printf(TEXT("VariableSet value cannot be resolved: %s"), *FailureReason);
                ++OutUnsupportedCount;
                return Info;
            }
            Info->ValueExpression = ValueExpr;
            return Info;
        }

        if (const UK2Node_AddDelegate* AddDelegateNode = Cast<UK2Node_AddDelegate>(InNode))
        {
            Info->NodeKind  = TEXT("AddDelegate");
            Info->NodeLabel = TEXT("AddDelegate");

            const FString DelegateName = AddDelegateNode->GetPropertyName().ToString();
            if (DelegateName.IsEmpty() || DelegateName == TEXT("None"))
            {
                Info->NodeKind          = TEXT("Unsupported");
                Info->UnsupportedReason = TEXT("AddDelegate has no delegate property name");
                ++OutUnsupportedCount;
                return Info;
            }
            Info->DelegatePropertyName = DelegateName;

            UEdGraphPin* TargetPin = AddDelegateNode->FindPin(UEdGraphSchema_K2::PN_Self);
            FString TargetExpr;
            FString FailureReason;
            if (!ResolvePinToCppExpr(TargetPin, TargetExpr, FailureReason, MaxDepth))
            {
                Info->NodeKind          = TEXT("Unsupported");
                Info->UnsupportedReason = FString::Printf(TEXT("AddDelegate target cannot be resolved: %s"), *FailureReason);
                ++OutUnsupportedCount;
                return Info;
            }
            Info->TargetExpression = TargetExpr;

            UEdGraphPin* DelegatePin = AddDelegateNode->GetDelegatePin();
            if (!DelegatePin || DelegatePin->LinkedTo.Num() == 0 || !DelegatePin->LinkedTo[0])
            {
                Info->NodeKind          = TEXT("Unsupported");
                Info->UnsupportedReason = TEXT("AddDelegate has no bound delegate function (missing CreateDelegate connection)");
                ++OutUnsupportedCount;
                return Info;
            }

            const UEdGraphNode* BoundNode = DelegatePin->LinkedTo[0]->GetOwningNode();
            if (const UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(BoundNode))
            {
                const FName HandlerName = CreateDelegateNode->GetFunctionName();
                if (HandlerName.IsNone())
                {
                    Info->NodeKind          = TEXT("Unsupported");
                    Info->UnsupportedReason = TEXT("CreateDelegate has no selected function name");
                    ++OutUnsupportedCount;
                    return Info;
                }
                Info->DelegateHandlerFunctionName = HandlerName.ToString();

                UEdGraphPin* ObjPin = CreateDelegateNode->GetObjectInPin();
                if (ObjPin && ObjPin->LinkedTo.Num() > 0)
                {
                    FString ObjExpr;
                    FString ObjFailure;
                    if (!ResolvePinToCppExpr(ObjPin, ObjExpr, ObjFailure, MaxDepth))
                    {
                        Info->NodeKind          = TEXT("Unsupported");
                        Info->UnsupportedReason = FString::Printf(TEXT("CreateDelegate object cannot be resolved: %s"), *ObjFailure);
                        ++OutUnsupportedCount;
                        return Info;
                    }
                    if (ObjExpr != TEXT("this"))
                    {
                        Info->NodeKind          = TEXT("Unsupported");
                        Info->UnsupportedReason = TEXT("CreateDelegate object must be 'this'");
                        ++OutUnsupportedCount;
                        return Info;
                    }
                }
            }
            else if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(BoundNode))
            {
                const FName HandlerName = EventNode->GetFunctionName();
                FString HandlerStr = HandlerName.IsNone()
                    ? EventNode->GetNodeTitle(ENodeTitleType::ListView).ToString()
                    : HandlerName.ToString();
                HandlerStr.TrimStartAndEndInline();
                if (HandlerStr.IsEmpty())
                {
                    Info->NodeKind          = TEXT("Unsupported");
                    Info->UnsupportedReason = TEXT("Bound event has empty function name");
                    ++OutUnsupportedCount;
                    return Info;
                }
                Info->DelegateHandlerFunctionName = HandlerStr;
            }
            else
            {
                Info->NodeKind          = TEXT("Unsupported");
                Info->UnsupportedReason = FString::Printf(TEXT("AddDelegate bound node is not supported: %s"),
                                                          BoundNode ? *BoundNode->GetClass()->GetName() : TEXT("null"));
                ++OutUnsupportedCount;
                return Info;
            }

            return Info;
        }

        // Macro instance — translator cannot expand; mark for manual review.
        if (const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(InNode))
        {
            const UEdGraph* MacroGraph = MacroNode->GetMacroGraph();
            if (!IsValid(MacroGraph))
            {
                Info->NodeKind          = TEXT("Unsupported");
                Info->NodeLabel         = TEXT("UK2Node_MacroInstance");
                Info->UnsupportedReason = TEXT("Macro instance has no MacroGraph");
                ++OutUnsupportedCount;
                return Info;
            }

            if (MacroGraph->GetName() == TEXT("FlipFlop"))
            {
                Info->NodeKind  = TEXT("FlipFlop");
                Info->NodeLabel = TEXT("FlipFlop");
                Info->FlipFlopStateVarName = FString::Printf(
                    TEXT("bFlipFlop_%s"),
                    *MacroNode->NodeGuid.ToString(EGuidFormats::Digits));

                const UEdGraphPin* APin = MacroNode->FindPin(TEXT("A"));
                const UEdGraphPin* BPin = MacroNode->FindPin(TEXT("B"));
                if (!APin || !BPin)
                {
                    Info->NodeKind          = TEXT("Unsupported");
                    Info->UnsupportedReason = TEXT("FlipFlop macro pins A/B not found");
                    ++OutUnsupportedCount;
                    return Info;
                }

                if (const UEdGraphNode* ANext = GetLinkedExecNode(APin))
                {
                    TraceLinearChain(ANext, Depth + 1, MaxDepth, VisitedThisChain,
                                     Info->BranchTrue, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
                }
                if (const UEdGraphNode* BNext = GetLinkedExecNode(BPin))
                {
                    TraceLinearChain(BNext, Depth + 1, MaxDepth, VisitedThisChain,
                                     Info->BranchFalse, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
                }
                return Info;
            }

            if (MacroGraphStack.Contains(MacroGraph))
            {
                Info->NodeKind          = TEXT("Unsupported");
                Info->NodeLabel         = MacroGraph->GetName();
                Info->UnsupportedReason = TEXT("Macro recursion detected during dump");
                ++OutUnsupportedCount;
                return Info;
            }

            Info->NodeKind  = TEXT("FunctionEntry");
            Info->NodeLabel = MacroGraph->GetName();

            MacroGraphStack.Add(MacroGraph);
            const UEdGraphNode* MacroStart = FindMacroEntryExecNode(MacroGraph);
            if (MacroStart)
            {
                TSet<const UEdGraphNode*> MacroVisited;
                TraceLinearChain(MacroStart, Depth + 1, MaxDepth, MacroVisited,
                                 Info->Next, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
            }
            else
            {
                ++OutUnsupportedCount;
                Info->NodeKind = TEXT("Unsupported");
                Info->UnsupportedReason = TEXT("Macro entry tunnel has no linked exec output");
                MacroGraphStack.Pop();
                return Info;
            }
            MacroGraphStack.Pop();

            if (const UEdGraphNode* AfterMacro = GetFirstLinkedExecFromNode(MacroNode))
            {
                TraceLinearChain(AfterMacro, Depth + 1, MaxDepth, VisitedThisChain,
                                 Info->Next, MacroGraphStack, OutTotalCount, OutUnsupportedCount);
            }
            return Info;
        }

        // Anything else — preserve a stub so the translator can emit a clear failure.
        Info->NodeKind         = TEXT("Unsupported");
        Info->NodeLabel        = InNode->GetClass()->GetName();
        Info->UnsupportedReason = FString::Printf(
            TEXT("Node class %s is not supported by the BlueprintBuster translator"),
            *InNode->GetClass()->GetName());
        ++OutUnsupportedCount;
        return Info;
    }

    void ParseExecutionGraphs(UBlueprint* InBlueprint,
                              TArray<FBPEventTreeInfo>& OutEventTrees,
                              int32& OutUnsupportedCount,
                              int32& OutTotalNodeCount,
                              int32 MaxDepth)
    {
        OutEventTrees.Reset();
        OutUnsupportedCount = 0;
        OutTotalNodeCount   = 0;

        if (!IsValid(InBlueprint))
        {
            return;
        }

        // Gather every graph that can carry events: UbergraphPages + ConstructionScript.
        TArray<UEdGraph*> AllGraphs;
        AllGraphs.Append(InBlueprint->UbergraphPages);
        AllGraphs.Append(InBlueprint->FunctionGraphs);

        for (UEdGraph* Graph : AllGraphs)
        {
            if (!IsValid(Graph))
            {
                continue;
            }

            // Each Event node starts an independent execution tree.
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
                if (!EventNode)
                {
                    continue;
                }

                FBPEventTreeInfo Tree;
                Tree.GraphName = Graph->GetName();

                TSet<const UEdGraphNode*> Visited;
                TArray<const UEdGraph*> MacroGraphStack;
                Tree.EventRoot = TraceNode(EventNode, /*Depth=*/0, MaxDepth, Visited, MacroGraphStack,
                                            OutTotalNodeCount, OutUnsupportedCount);

                // Trace the rest of the linear chain from the event's Then pin.
                if (Tree.EventRoot.IsValid())
                {
                    for (UEdGraphPin* Pin : EventNode->Pins)
                    {
                        if (Pin && Pin->Direction == EGPD_Output &&
                            Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                        {
                            if (const UEdGraphNode* Next = GetLinkedExecNode(Pin))
                            {
                                TraceLinearChain(Next, /*Depth=*/1, MaxDepth, Visited,
                                                 Tree.EventRoot->Next,
                                                 MacroGraphStack,
                                                 OutTotalNodeCount, OutUnsupportedCount);
                            }
                            break;
                        }
                    }
                }

                OutEventTrees.Add(MoveTemp(Tree));
            }
        }

        UE_LOG(LogBlueprintBuster, Verbose,
               TEXT("Graph: %d event trees, %d nodes (%d unsupported) for %s"),
               OutEventTrees.Num(), OutTotalNodeCount,
               OutUnsupportedCount, *InBlueprint->GetName());
    }

    // ─── Custom functions ────────────────────────────────────────────────────

    // Convert UEdGraphPin type info into translator-friendly strings.
    static void DescribePinType(const FEdGraphPinType& InPinType,
                                FBPFunctionParameter& OutParam)
    {
        OutParam.bIsArray     = InPinType.IsArray();
        OutParam.bIsMap       = InPinType.IsMap();
        OutParam.bIsSet       = InPinType.IsSet();
        OutParam.bIsReference = InPinType.bIsReference;
        OutParam.bIsConst     = InPinType.bIsConst;

        auto ResolveObjectName = [&InPinType]() -> FString
        {
            if (const UObject* SubObj = InPinType.PinSubCategoryObject.Get())
            {
                FString Raw = SubObj->GetName();
                if (const UClass* AsClass = Cast<UClass>(SubObj))
                {
                    if (AsClass->IsChildOf(AActor::StaticClass()) &&
                        !Raw.StartsWith(TEXT("A")))
                    {
                        Raw = FString::Printf(TEXT("A%s"), *Raw);
                    }
                    else if (!Raw.StartsWith(TEXT("U")) &&
                             !Raw.StartsWith(TEXT("A")))
                    {
                        Raw = FString::Printf(TEXT("U%s"), *Raw);
                    }
                }
                return Raw;
            }
            return FString();
        };

        const FName Cat = InPinType.PinCategory;

        if (Cat == UEdGraphSchema_K2::PC_Boolean)
        {
            OutParam.TypeName = TEXT("bool");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Byte)
        {
            if (const UObject* SubObj = InPinType.PinSubCategoryObject.Get())
            {
                if (SubObj->IsA<UEnum>())
                {
                    OutParam.TypeName     = TEXT("enum");
                    OutParam.CppClassName = SubObj->GetName();
                    return;
                }
            }
            OutParam.TypeName = TEXT("byte");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Int)
        {
            OutParam.TypeName = TEXT("int");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Int64)
        {
            OutParam.TypeName = TEXT("int64");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Float)
        {
            OutParam.TypeName = TEXT("float");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Double)
        {
            OutParam.TypeName = TEXT("double");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Real)
        {
            // UE5: subcategory distinguishes float vs double for PC_Real.
            OutParam.TypeName = (InPinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
                ? TEXT("double") : TEXT("float");
        }
        else if (Cat == UEdGraphSchema_K2::PC_String)
        {
            OutParam.TypeName = TEXT("string");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Name)
        {
            OutParam.TypeName = TEXT("name");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Text)
        {
            OutParam.TypeName = TEXT("text");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Object ||
                 Cat == UEdGraphSchema_K2::PC_Interface)
        {
            OutParam.TypeName     = TEXT("object");
            OutParam.CppClassName = ResolveObjectName();
            if (OutParam.CppClassName.IsEmpty()) OutParam.CppClassName = TEXT("UObject");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Class)
        {
            OutParam.TypeName     = TEXT("class");
            OutParam.CppClassName = ResolveObjectName();
            if (OutParam.CppClassName.IsEmpty()) OutParam.CppClassName = TEXT("UObject");
        }
        else if (Cat == UEdGraphSchema_K2::PC_SoftObject)
        {
            OutParam.TypeName     = TEXT("softobject");
            OutParam.CppClassName = ResolveObjectName();
            if (OutParam.CppClassName.IsEmpty()) OutParam.CppClassName = TEXT("UObject");
        }
        else if (Cat == UEdGraphSchema_K2::PC_SoftClass)
        {
            OutParam.TypeName     = TEXT("softclass");
            OutParam.CppClassName = ResolveObjectName();
            if (OutParam.CppClassName.IsEmpty()) OutParam.CppClassName = TEXT("UObject");
        }
        else if (Cat == UEdGraphSchema_K2::PC_Struct)
        {
            OutParam.TypeName = TEXT("struct");
            if (const UScriptStruct* AsStruct =
                    Cast<UScriptStruct>(InPinType.PinSubCategoryObject.Get()))
            {
                FString Raw = AsStruct->GetName();
                OutParam.CppClassName = Raw.StartsWith(TEXT("F"))
                    ? Raw
                    : FString::Printf(TEXT("F%s"), *Raw);
            }
            else
            {
                OutParam.CppClassName = TEXT("FStruct");
            }
        }
        else
        {
            OutParam.TypeName     = Cat.ToString();
            OutParam.CppClassName = ResolveObjectName();
        }
    }

    // FunctionEntry: parameters are OUTPUT data pins.
    // FunctionResult: returns are INPUT data pins.
    static void ExtractParameters(const UEdGraphNode* InNode,
                                  EEdGraphPinDirection InDirection,
                                  TArray<FBPFunctionParameter>& OutParams)
    {
        if (!InNode)
        {
            return;
        }

        for (UEdGraphPin* Pin : InNode->Pins)
        {
            if (!Pin || Pin->Direction != InDirection)
            {
                continue;
            }
            if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                continue;
            }
            if (Pin->PinName == UEdGraphSchema_K2::PN_Self)
            {
                continue;
            }
            if (Pin->PinName == TEXT("OutputDelegate"))
            {
                continue;
            }

            FBPFunctionParameter Param;
            Param.ParameterName = Pin->PinName.ToString();
            DescribePinType(Pin->PinType, Param);
            OutParams.Add(MoveTemp(Param));
        }
    }

    void ParseFunctionGraphs(UBlueprint* InBlueprint,
                             TArray<FBPCustomFunctionInfo>& OutFunctions,
                             int32& OutUnsupportedCount,
                             int32& OutTotalNodeCount,
                             int32 MaxDepth)
    {
        OutFunctions.Reset();

        if (!IsValid(InBlueprint))
        {
            return;
        }

        // UCS lives in FunctionGraphs but is conceptually an event graph.
        const FName UCSName = UEdGraphSchema_K2::FN_UserConstructionScript;

        for (UEdGraph* Graph : InBlueprint->FunctionGraphs)
        {
            if (!IsValid(Graph))
            {
                continue;
            }
            if (Graph->GetFName() == UCSName)
            {
                continue;
            }

            UK2Node_FunctionEntry*  EntryNode  = nullptr;
            UK2Node_FunctionResult* ResultNode = nullptr;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!EntryNode)
                {
                    EntryNode = Cast<UK2Node_FunctionEntry>(Node);
                }
                if (!ResultNode)
                {
                    ResultNode = Cast<UK2Node_FunctionResult>(Node);
                }
                if (EntryNode && ResultNode)
                {
                    break;
                }
            }

            if (!EntryNode)
            {
                continue;
            }

            FBPCustomFunctionInfo Func;
            Func.GraphName    = Graph->GetName();
            Func.FunctionName = EntryNode->CustomGeneratedFunctionName.IsNone()
                ? Graph->GetName()
                : EntryNode->CustomGeneratedFunctionName.ToString();

            const uint32 EntryFlags = EntryNode->GetFunctionFlags();
            Func.bIsPure              = (EntryFlags & FUNC_BlueprintPure) != 0;
            Func.bIsConst             = (EntryFlags & FUNC_Const) != 0;
            Func.bIsBlueprintCallable = (EntryFlags & FUNC_BlueprintCallable) != 0;

            ExtractParameters(EntryNode, EGPD_Output, Func.InputParameters);
            if (ResultNode)
            {
                ExtractParameters(ResultNode, EGPD_Input, Func.ReturnParameters);
            }

            // Locate the Then exec output of FunctionEntry.
            const UEdGraphPin* ThenPin = nullptr;
            for (UEdGraphPin* Pin : EntryNode->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Output &&
                    Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    ThenPin = Pin;
                    break;
                }
            }

            // FunctionRoot mirrors EventRoot shape — children carry the body chain.
            Func.FunctionRoot = MakeShared<FBPGraphNodeInfo>();
            Func.FunctionRoot->NodeKind  = TEXT("FunctionEntry");
            Func.FunctionRoot->NodeLabel = Func.FunctionName;

            TArray<const UEdGraph*> MacroGraphStack;
            if (const UEdGraphNode* FirstBodyNode = GetLinkedExecNode(ThenPin))
            {
                TSet<const UEdGraphNode*> Visited;
                Visited.Add(EntryNode);
                TraceLinearChain(FirstBodyNode, /*Depth=*/1, MaxDepth, Visited,
                                 Func.FunctionRoot->Next,
                                 MacroGraphStack,
                                 OutTotalNodeCount, OutUnsupportedCount);
            }

            OutFunctions.Add(MoveTemp(Func));
        }

        UE_LOG(LogBlueprintBuster, Verbose,
               TEXT("Functions: %d custom function(s) extracted from %s"),
               OutFunctions.Num(), *InBlueprint->GetName());
    }
}
