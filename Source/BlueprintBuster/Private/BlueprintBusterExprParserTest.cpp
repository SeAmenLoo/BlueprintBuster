#include "BlueprintBusterDump.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/EngineTypes.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Select.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintBusterExprParserTest, "BlueprintBuster.FullDump.ExprParser", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

static UEdGraphPin* BlueprintBuster_FindExecPin(UEdGraphNode* Node, EEdGraphPinDirection Dir, const FName& PreferredName = NAME_None)
{
    if (!IsValid(Node))
    {
        return nullptr;
    }

    if (PreferredName != NAME_None)
    {
        if (UEdGraphPin* P = Node->FindPin(PreferredName, Dir))
        {
            return P;
        }
    }

    for (UEdGraphPin* P : Node->Pins)
    {
        if (!P)
        {
            continue;
        }
        if (P->Direction == Dir && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
        {
            return P;
        }
    }
    return nullptr;
}

static void BlueprintBuster_AddNode(UEdGraph* Graph, UEdGraphNode* Node)
{
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    Node->AllocateDefaultPins();
    Graph->AddNode(Node, true, false);
}

static bool BlueprintBuster_CollectVariableSetExprs(const TSharedPtr<FBPGraphNodeInfo>& Root, TMap<FString, FString>& OutByVarName)
{
    if (!Root.IsValid())
    {
        return false;
    }

    TArray<TSharedPtr<FBPGraphNodeInfo>> Stack;
    Stack.Add(Root);

    while (Stack.Num() > 0)
    {
        TSharedPtr<FBPGraphNodeInfo> N = Stack.Pop(EAllowShrinking::No);
        if (!N.IsValid())
        {
            continue;
        }

        if (N->NodeKind == TEXT("VariableSet") && !N->NodeLabel.IsEmpty() && !N->ValueExpression.IsEmpty())
        {
            OutByVarName.Add(N->NodeLabel, N->ValueExpression);
        }

        for (const TSharedPtr<FBPGraphNodeInfo>& C : N->Next)
        {
            Stack.Add(C);
        }
        for (const TSharedPtr<FBPGraphNodeInfo>& C : N->BranchTrue)
        {
            Stack.Add(C);
        }
        for (const TSharedPtr<FBPGraphNodeInfo>& C : N->BranchFalse)
        {
            Stack.Add(C);
        }
    }

    return true;
}

bool FBlueprintBusterExprParserTest::RunTest(const FString& Parameters)
{
    UPackage* Pkg = GetTransientPackage();
    TestNotNull(TEXT("TransientPackage"), Pkg);

    UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Pkg, FName(TEXT("BP_BB_ExprParser_Test")), EBlueprintType::BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
    TestNotNull(TEXT("CreateBlueprint"), BP);

    const UEdGraphSchema* Schema = GetDefault<UEdGraphSchema_K2>();
    TestNotNull(TEXT("Schema"), Schema);

    {
        FEdGraphPinType BoolType;
        BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        TestTrue(TEXT("AddMemberVariable(bFlag)"), FBlueprintEditorUtils::AddMemberVariable(BP, FName(TEXT("bFlag")), BoolType));
    }
    {
        FEdGraphPinType IntType;
        IntType.PinCategory = UEdGraphSchema_K2::PC_Int;
        TestTrue(TEXT("AddMemberVariable(SomeInt)"), FBlueprintEditorUtils::AddMemberVariable(BP, FName(TEXT("SomeInt")), IntType));
        TestTrue(TEXT("AddMemberVariable(SomeInt2)"), FBlueprintEditorUtils::AddMemberVariable(BP, FName(TEXT("SomeInt2")), IntType));
    }
    {
        FEdGraphPinType ArrayType;
        ArrayType.PinCategory = UEdGraphSchema_K2::PC_Int;
        ArrayType.ContainerType = EPinContainerType::Array;
        TestTrue(TEXT("AddMemberVariable(IntArray)"), FBlueprintEditorUtils::AddMemberVariable(BP, FName(TEXT("IntArray")), ArrayType));
    }
    {
        FEdGraphPinType EnumType;
        EnumType.PinCategory = UEdGraphSchema_K2::PC_Byte;
        EnumType.PinSubCategoryObject = StaticEnum<ECollisionChannel>();
        TestTrue(TEXT("AddMemberVariable(CollisionChannel)"), FBlueprintEditorUtils::AddMemberVariable(BP, FName(TEXT("CollisionChannel")), EnumType));
    }

    UEdGraph* FuncGraph = FBlueprintEditorUtils::CreateNewGraph(BP, FName(TEXT("TestExprs")), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    TestNotNull(TEXT("CreateNewGraph"), FuncGraph);
    FBlueprintEditorUtils::AddFunctionGraph(BP, FuncGraph, false, static_cast<UFunction*>(nullptr));

    UK2Node_FunctionEntry* EntryNode = nullptr;
    UK2Node_FunctionResult* ResultNode = nullptr;
    for (UEdGraphNode* N : FuncGraph->Nodes)
    {
        if (!EntryNode)
        {
            EntryNode = Cast<UK2Node_FunctionEntry>(N);
        }
        if (!ResultNode)
        {
            ResultNode = Cast<UK2Node_FunctionResult>(N);
        }
    }
    TestNotNull(TEXT("EntryNode"), EntryNode);
    TestNotNull(TEXT("ResultNode"), ResultNode);

    UK2Node_VariableGet* GetFlag = NewObject<UK2Node_VariableGet>(FuncGraph);
    GetFlag->VariableReference.SetSelfMember(FName(TEXT("bFlag")));
    BlueprintBuster_AddNode(FuncGraph, GetFlag);

    UK2Node_EnumLiteral* EnumLit = NewObject<UK2Node_EnumLiteral>(FuncGraph);
    EnumLit->Enum = StaticEnum<ECollisionChannel>();
    BlueprintBuster_AddNode(FuncGraph, EnumLit);

    if (UEdGraphPin* EnumIn = EnumLit->FindPin(UK2Node_EnumLiteral::GetEnumInputPinName(), EGPD_Input))
    {
        EnumIn->DefaultValue = TEXT("ECC_WorldStatic");
    }

    UK2Node_VariableSet* SetEnum = NewObject<UK2Node_VariableSet>(FuncGraph);
    SetEnum->VariableReference.SetSelfMember(FName(TEXT("CollisionChannel")));
    BlueprintBuster_AddNode(FuncGraph, SetEnum);

    UK2Node_MakeArray* MakeArray = NewObject<UK2Node_MakeArray>(FuncGraph);
    BlueprintBuster_AddNode(FuncGraph, MakeArray);
    MakeArray->AddInputPin();

    UK2Node_VariableSet* SetArray = NewObject<UK2Node_VariableSet>(FuncGraph);
    SetArray->VariableReference.SetSelfMember(FName(TEXT("IntArray")));
    BlueprintBuster_AddNode(FuncGraph, SetArray);

    UK2Node_Select* SelectNode = NewObject<UK2Node_Select>(FuncGraph);
    BlueprintBuster_AddNode(FuncGraph, SelectNode);

    UK2Node_VariableSet* SetSelect = NewObject<UK2Node_VariableSet>(FuncGraph);
    SetSelect->VariableReference.SetSelfMember(FName(TEXT("SomeInt")));
    BlueprintBuster_AddNode(FuncGraph, SetSelect);

    UK2Node_CallFunction* PureAdd = NewObject<UK2Node_CallFunction>(FuncGraph);
    PureAdd->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(TEXT("Add_IntInt"))));
    BlueprintBuster_AddNode(FuncGraph, PureAdd);

    UK2Node_VariableSet* SetPure = NewObject<UK2Node_VariableSet>(FuncGraph);
    SetPure->VariableReference.SetSelfMember(FName(TEXT("SomeInt2")));
    BlueprintBuster_AddNode(FuncGraph, SetPure);

    UEdGraphPin* EntryThen = BlueprintBuster_FindExecPin(EntryNode, EGPD_Output, UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* SetEnumExec = BlueprintBuster_FindExecPin(SetEnum, EGPD_Input, UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* SetArrayExec = BlueprintBuster_FindExecPin(SetArray, EGPD_Input, UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* SetSelectExec = BlueprintBuster_FindExecPin(SetSelect, EGPD_Input, UEdGraphSchema_K2::PN_Execute);
    UEdGraphPin* SetPureExec = BlueprintBuster_FindExecPin(SetPure, EGPD_Input, UEdGraphSchema_K2::PN_Execute);

    TestNotNull(TEXT("EntryThen"), EntryThen);
    TestNotNull(TEXT("SetEnumExec"), SetEnumExec);
    TestNotNull(TEXT("SetArrayExec"), SetArrayExec);
    TestNotNull(TEXT("SetSelectExec"), SetSelectExec);
    TestNotNull(TEXT("SetPureExec"), SetPureExec);

    Schema->TryCreateConnection(EntryThen, SetEnumExec);

    UEdGraphPin* SetEnumThen = BlueprintBuster_FindExecPin(SetEnum, EGPD_Output, UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* SetArrayThen = BlueprintBuster_FindExecPin(SetArray, EGPD_Output, UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* SetSelectThen = BlueprintBuster_FindExecPin(SetSelect, EGPD_Output, UEdGraphSchema_K2::PN_Then);
    UEdGraphPin* SetPureThen = BlueprintBuster_FindExecPin(SetPure, EGPD_Output, UEdGraphSchema_K2::PN_Then);

    TestNotNull(TEXT("SetEnumThen"), SetEnumThen);
    TestNotNull(TEXT("SetArrayThen"), SetArrayThen);
    TestNotNull(TEXT("SetSelectThen"), SetSelectThen);
    TestNotNull(TEXT("SetPureThen"), SetPureThen);

    Schema->TryCreateConnection(SetEnumThen, SetArrayExec);
    Schema->TryCreateConnection(SetArrayThen, SetSelectExec);
    Schema->TryCreateConnection(SetSelectThen, SetPureExec);

    if (UEdGraphPin* EnumOut = EnumLit->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output))
    {
        Schema->TryCreateConnection(EnumOut, SetEnum->FindPin(SetEnum->GetVarName(), EGPD_Input));
    }

    if (UEdGraphPin* ArrayOut = MakeArray->GetOutputPin())
    {
        Schema->TryCreateConnection(ArrayOut, SetArray->FindPin(SetArray->GetVarName(), EGPD_Input));
    }

    for (UEdGraphPin* Pin : MakeArray->Pins)
    {
        if (!Pin || Pin->Direction != EGPD_Input)
        {
            continue;
        }
        const FString N = Pin->PinName.ToString();
        if (N == TEXT("0"))
        {
            Pin->DefaultValue = TEXT("1");
        }
        else if (N == TEXT("1"))
        {
            Pin->DefaultValue = TEXT("2");
        }
    }

    if (UEdGraphPin* IndexPin = SelectNode->GetIndexPin())
    {
        if (UEdGraphPin* FlagOut = GetFlag->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output))
        {
            Schema->TryCreateConnection(FlagOut, IndexPin);
        }
    }

    {
        TArray<UEdGraphPin*> OptionPins;
        SelectNode->GetOptionPins(OptionPins);
        for (UEdGraphPin* P : OptionPins)
        {
            if (!P || P->Direction != EGPD_Input)
            {
                continue;
            }
            if (P->PinName == TEXT("A"))
            {
                P->DefaultValue = TEXT("10");
            }
            else if (P->PinName == TEXT("B"))
            {
                P->DefaultValue = TEXT("20");
            }
        }
        if (UEdGraphPin* SelectOut = SelectNode->GetReturnValuePin())
        {
            Schema->TryCreateConnection(SelectOut, SetSelect->FindPin(SetSelect->GetVarName(), EGPD_Input));
        }
    }

    if (UEdGraphPin* A = PureAdd->FindPin(FName(TEXT("A")), EGPD_Input))
    {
        A->DefaultValue = TEXT("3");
    }
    if (UEdGraphPin* B = PureAdd->FindPin(FName(TEXT("B")), EGPD_Input))
    {
        B->DefaultValue = TEXT("4");
    }
    if (UEdGraphPin* AddOut = PureAdd->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output))
    {
        Schema->TryCreateConnection(AddOut, SetPure->FindPin(SetPure->GetVarName(), EGPD_Input));
    }

    FKismetEditorUtilities::CompileBlueprint(BP);

    const FString OutPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BlueprintBuster"), TEXT("Tests"), TEXT("BP_BB_ExprParser_Test_dump.json"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), true);

    FBPDumpData Dump;
    const bool bDumpOk = BlueprintBusterDump::DumpBlueprintToJsonFile(BP, OutPath, 64, &Dump, true);
    TestTrue(TEXT("DumpBlueprintToJsonFile"), bDumpOk);

    if (!bDumpOk)
    {
        return false;
    }

    TestEqual(TEXT("Unsupported count"), Dump.UnsupportedNodeCount, 0);
    TestTrue(TEXT("Has custom functions"), Dump.CustomFunctions.Num() > 0);

    TMap<FString, FString> ByVar;
    if (Dump.CustomFunctions.Num() > 0 && Dump.CustomFunctions[0].FunctionRoot.IsValid())
    {
        BlueprintBuster_CollectVariableSetExprs(Dump.CustomFunctions[0].FunctionRoot, ByVar);
    }

    const FString* EnumExpr = ByVar.Find(TEXT("CollisionChannel"));
    const FString* ArrayExpr = ByVar.Find(TEXT("IntArray"));
    const FString* SelectExpr = ByVar.Find(TEXT("SomeInt"));
    const FString* PureExpr = ByVar.Find(TEXT("SomeInt2"));

    TestNotNull(TEXT("EnumExpr"), EnumExpr);
    TestNotNull(TEXT("ArrayExpr"), ArrayExpr);
    TestNotNull(TEXT("SelectExpr"), SelectExpr);
    TestNotNull(TEXT("PureExpr"), PureExpr);

    if (EnumExpr)
    {
        TestTrue(TEXT("Enum literal lowered"), EnumExpr->Contains(TEXT("ECollisionChannel::ECC_WorldStatic")));
    }
    if (ArrayExpr)
    {
        TestTrue(TEXT("MakeArray lowered"), ArrayExpr->Contains(TEXT("TArray<int32>{")));
        TestTrue(TEXT("MakeArray item 1"), ArrayExpr->Contains(TEXT("1")));
        TestTrue(TEXT("MakeArray item 2"), ArrayExpr->Contains(TEXT("2")));
    }
    if (SelectExpr)
    {
        TestTrue(TEXT("Select lowered"), SelectExpr->Contains(TEXT("?")));
        TestTrue(TEXT("Select uses bFlag"), SelectExpr->Contains(TEXT("bFlag")));
    }
    if (PureExpr)
    {
        TestTrue(TEXT("Pure CallFunction lowered"), PureExpr->Contains(TEXT("UKismetMathLibrary::Add_IntInt")));
    }

    IFileManager::Get().Delete(*OutPath, false, true, true);

    return true;
}

#endif
