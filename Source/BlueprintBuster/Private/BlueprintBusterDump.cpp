#include "BlueprintBusterDump.h"

#include "BlueprintBusterParsers.h"
#include "BlueprintBuster.h"

#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Algo/Sort.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool TryConvertBPGCPathToBlueprintPath(const FString& InClassPath, FString& OutBlueprintPath)
	{
		int32 DotIdx = INDEX_NONE;
		if (!InClassPath.FindLastChar(TEXT('.'), DotIdx))
		{
			return false;
		}

		const FString ObjName = InClassPath.Mid(DotIdx + 1);
		if (!ObjName.EndsWith(TEXT("_C")))
		{
			return false;
		}

		OutBlueprintPath = InClassPath.Left(DotIdx + 1) + ObjName.LeftChop(2);
		return true;
	}

	void CollectBlueprintDepsFromNode(const TSharedPtr<FBPGraphNodeInfo>& InNode, TSet<FString>& InOutDeps)
	{
		if (!InNode.IsValid())
		{
			return;
		}

		if (!InNode->TargetClassPath.IsEmpty())
		{
			FString BPPath;
			if (TryConvertBPGCPathToBlueprintPath(InNode->TargetClassPath, BPPath))
			{
				InOutDeps.Add(BPPath);
			}
		}

		for (const TSharedPtr<FBPGraphNodeInfo>& N : InNode->Next)
		{
			CollectBlueprintDepsFromNode(N, InOutDeps);
		}
		for (const TSharedPtr<FBPGraphNodeInfo>& N : InNode->BranchTrue)
		{
			CollectBlueprintDepsFromNode(N, InOutDeps);
		}
		for (const TSharedPtr<FBPGraphNodeInfo>& N : InNode->BranchFalse)
		{
			CollectBlueprintDepsFromNode(N, InOutDeps);
		}
	}

	void CollectBlueprintDependencies(FBPDumpData& InOutDump)
	{
		TSet<FString> Deps;

		if (!InOutDump.ParentClassPath.IsEmpty())
		{
			FString BPPath;
			if (TryConvertBPGCPathToBlueprintPath(InOutDump.ParentClassPath, BPPath))
			{
				Deps.Add(BPPath);
			}
		}

		for (const FBPEventTreeInfo& Tree : InOutDump.EventTrees)
		{
			CollectBlueprintDepsFromNode(Tree.EventRoot, Deps);
		}
		for (const FBPCustomFunctionInfo& Func : InOutDump.CustomFunctions)
		{
			CollectBlueprintDepsFromNode(Func.FunctionRoot, Deps);
		}

		Deps.Remove(InOutDump.BlueprintPath);

		InOutDump.DependencyBlueprintPaths = Deps.Array();
		Algo::Sort(InOutDump.DependencyBlueprintPaths);
	}

	TSharedPtr<FJsonObject> NodeToJson(const TSharedPtr<FBPGraphNodeInfo>& InNode)
	{
		if (!InNode.IsValid())
		{
			return nullptr;
		}

		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("kind"), InNode->NodeKind);
		Obj->SetStringField(TEXT("label"), InNode->NodeLabel);

		if (!InNode->FunctionName.IsEmpty())
		{
			Obj->SetStringField(TEXT("function"), InNode->FunctionName);
		}
		if (!InNode->TargetClassPath.IsEmpty())
		{
			Obj->SetStringField(TEXT("targetClass"), InNode->TargetClassPath);
		}
		if (!InNode->TargetClassName.IsEmpty())
		{
			Obj->SetStringField(TEXT("targetClassName"), InNode->TargetClassName);
		}
		if (!InNode->TargetExpression.IsEmpty())
		{
			Obj->SetStringField(TEXT("targetExpr"), InNode->TargetExpression);
		}
		if (!InNode->ConditionExpression.IsEmpty())
		{
			Obj->SetStringField(TEXT("condition"), InNode->ConditionExpression);
		}
		if (!InNode->ValueExpression.IsEmpty())
		{
			Obj->SetStringField(TEXT("valueExpr"), InNode->ValueExpression);
		}
		if (!InNode->DelegatePropertyName.IsEmpty())
		{
			Obj->SetStringField(TEXT("delegate"), InNode->DelegatePropertyName);
		}
		if (!InNode->DelegateHandlerFunctionName.IsEmpty())
		{
			Obj->SetStringField(TEXT("handler"), InNode->DelegateHandlerFunctionName);
		}
		if (!InNode->FlipFlopStateVarName.IsEmpty())
		{
			Obj->SetStringField(TEXT("stateVar"), InNode->FlipFlopStateVarName);
		}
		if (!InNode->UnsupportedReason.IsEmpty())
		{
			Obj->SetStringField(TEXT("unsupported"), InNode->UnsupportedReason);
		}

		if (InNode->CallArguments.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Args;
			Args.Reserve(InNode->CallArguments.Num());
			for (const FBPCallArgumentInfo& Arg : InNode->CallArguments)
			{
				TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
				A->SetStringField(TEXT("name"), Arg.Name);
				A->SetStringField(TEXT("expr"), Arg.Expr);
				Args.Add(MakeShared<FJsonValueObject>(A));
			}
			Obj->SetArrayField(TEXT("args"), Args);
		}
		if (!InNode->AssignToVariable.IsEmpty())
		{
			Obj->SetStringField(TEXT("assignTo"), InNode->AssignToVariable);
		}

		auto SerialiseList = [&](const TArray<TSharedPtr<FBPGraphNodeInfo>>& InList, const FString& Field)
		{
			if (InList.Num() == 0)
			{
				return;
			}
			TArray<TSharedPtr<FJsonValue>> JsonList;
			for (const TSharedPtr<FBPGraphNodeInfo>& Child : InList)
			{
				TSharedPtr<FJsonObject> ChildObj = NodeToJson(Child);
				if (ChildObj.IsValid())
				{
					JsonList.Add(MakeShared<FJsonValueObject>(ChildObj.ToSharedRef()));
				}
			}
			Obj->SetArrayField(Field, JsonList);
		};

		SerialiseList(InNode->Next, TEXT("next"));
		SerialiseList(InNode->BranchTrue, TEXT("true"));
		SerialiseList(InNode->BranchFalse, TEXT("false"));

		return Obj;
	}

	TSharedRef<FJsonObject> DumpToJson(const FBPDumpData& InDump)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("blueprintName"), InDump.BlueprintName);
		Root->SetStringField(TEXT("blueprintPath"), InDump.BlueprintPath);
		Root->SetStringField(TEXT("parentClassPath"), InDump.ParentClassPath);
		Root->SetStringField(TEXT("parentClassName"), InDump.ParentClassName);
		Root->SetBoolField(TEXT("isActorDerived"), InDump.bIsActorDerived);
		Root->SetNumberField(TEXT("totalNodeCount"), InDump.TotalNodeCount);
		Root->SetNumberField(TEXT("unsupportedNodeCount"), InDump.UnsupportedNodeCount);
		if (InDump.DependencyBlueprintPaths.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> DepArr;
			DepArr.Reserve(InDump.DependencyBlueprintPaths.Num());
			for (const FString& Dep : InDump.DependencyBlueprintPaths)
			{
				DepArr.Add(MakeShared<FJsonValueString>(Dep));
			}
			Root->SetArrayField(TEXT("dependencyBlueprints"), DepArr);
		}

		TArray<TSharedPtr<FJsonValue>> CompArr;
		for (const FBPComponentInfo& Comp : InDump.Components)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("variableName"), Comp.VariableName);
			O->SetStringField(TEXT("classPath"), Comp.ClassPath);
			O->SetStringField(TEXT("className"), Comp.ClassName);
			O->SetStringField(TEXT("attachParentVarName"), Comp.AttachParentVarName);
			O->SetStringField(TEXT("attachSocketName"), Comp.AttachSocketName);
			O->SetBoolField(TEXT("isRoot"), Comp.bIsRoot);

			TArray<TSharedPtr<FJsonValue>> Children;
			for (const FString& Child : Comp.ChildVariableNames)
			{
				Children.Add(MakeShared<FJsonValueString>(Child));
			}
			O->SetArrayField(TEXT("children"), Children);
			CompArr.Add(MakeShared<FJsonValueObject>(O));
		}
		Root->SetArrayField(TEXT("components"), CompArr);

		TArray<TSharedPtr<FJsonValue>> DefArr;
		for (const FBPPropertyInfo& Def : InDump.Defaults)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("propertyName"), Def.PropertyName);
			O->SetStringField(TEXT("propertyType"), Def.PropertyTypeName);
			O->SetStringField(TEXT("innerTypeName"), Def.InnerTypeName);
			O->SetStringField(TEXT("value"), Def.ValueString);
			O->SetStringField(TEXT("category"), Def.Category);
			O->SetStringField(TEXT("pointerStorageHint"), Def.PointerStorageHint);
			O->SetBoolField(TEXT("isInstanceEditable"), Def.bIsInstanceEditable);
			O->SetBoolField(TEXT("isBlueprintVisible"), Def.bIsBlueprintVisible);
			DefArr.Add(MakeShared<FJsonValueObject>(O));
		}
		Root->SetArrayField(TEXT("defaults"), DefArr);

		TArray<TSharedPtr<FJsonValue>> TreeArr;
		for (const FBPEventTreeInfo& Tree : InDump.EventTrees)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("graphName"), Tree.GraphName);
			if (Tree.EventRoot.IsValid())
			{
				TSharedPtr<FJsonObject> RootObj = NodeToJson(Tree.EventRoot);
				if (RootObj.IsValid())
				{
					O->SetObjectField(TEXT("event"), RootObj);
				}
			}
			TreeArr.Add(MakeShared<FJsonValueObject>(O));
		}
		Root->SetArrayField(TEXT("eventTrees"), TreeArr);

		auto ParameterToJson = [](const FBPFunctionParameter& InParam)
		{
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), InParam.ParameterName);
			P->SetStringField(TEXT("type"), InParam.TypeName);
			P->SetStringField(TEXT("cppClassName"), InParam.CppClassName);
			P->SetBoolField(TEXT("isArray"), InParam.bIsArray);
			P->SetBoolField(TEXT("isMap"), InParam.bIsMap);
			P->SetBoolField(TEXT("isSet"), InParam.bIsSet);
			P->SetBoolField(TEXT("isReference"), InParam.bIsReference);
			P->SetBoolField(TEXT("isConst"), InParam.bIsConst);
			return P;
		};

		TArray<TSharedPtr<FJsonValue>> FuncArr;
		for (const FBPCustomFunctionInfo& Func : InDump.CustomFunctions)
		{
			TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetStringField(TEXT("functionName"), Func.FunctionName);
			F->SetStringField(TEXT("graphName"), Func.GraphName);
			F->SetBoolField(TEXT("isPure"), Func.bIsPure);
			F->SetBoolField(TEXT("isConst"), Func.bIsConst);
			F->SetBoolField(TEXT("isBlueprintCallable"), Func.bIsBlueprintCallable);

			TArray<TSharedPtr<FJsonValue>> InArr;
			for (const FBPFunctionParameter& P : Func.InputParameters)
			{
				InArr.Add(MakeShared<FJsonValueObject>(ParameterToJson(P)));
			}
			F->SetArrayField(TEXT("inputs"), InArr);

			TArray<TSharedPtr<FJsonValue>> RetArr;
			for (const FBPFunctionParameter& P : Func.ReturnParameters)
			{
				RetArr.Add(MakeShared<FJsonValueObject>(ParameterToJson(P)));
			}
			F->SetArrayField(TEXT("returns"), RetArr);

			if (Func.FunctionRoot.IsValid())
			{
				TSharedPtr<FJsonObject> RootObj = NodeToJson(Func.FunctionRoot);
				if (RootObj.IsValid())
				{
					F->SetObjectField(TEXT("functionRoot"), RootObj);
				}
			}

			FuncArr.Add(MakeShared<FJsonValueObject>(F));
		}
		Root->SetArrayField(TEXT("customFunctions"), FuncArr);

		return Root;
	}

	bool WriteJsonToFile(const TSharedRef<FJsonObject>& InRoot, const FString& InFilePath)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);

		if (!FJsonSerializer::Serialize(InRoot, Writer))
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(Out, *InFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

FString BlueprintBusterDump::GetDefaultDumpDirectory()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BlueprintBuster"), TEXT("Dumps"));
}

FString BlueprintBusterDump::MakeDumpFilePath(const FString& OutputDir, const UBlueprint* Blueprint)
{
	const FString BaseDir = OutputDir.IsEmpty() ? GetDefaultDumpDirectory() : OutputDir;
	const FString FileName = FString::Printf(TEXT("%s_dump.json"), Blueprint ? *Blueprint->GetName() : TEXT("Blueprint"));
	return FPaths::Combine(BaseDir, FileName);
}

bool BlueprintBusterDump::DumpBlueprintToJsonFile(UBlueprint* Blueprint, const FString& OutputFilePath, int32 MaxDepth, FBPDumpData* OutDump, bool bFailOnUnsupported)
{
	if (!IsValid(Blueprint))
	{
		return false;
	}

	FBPDumpData Dump;
	Dump.BlueprintName = Blueprint->GetName();
	Dump.BlueprintPath = Blueprint->GetPathName();

	if (UClass* GenClass = Blueprint->GeneratedClass)
	{
		if (UClass* SuperCls = GenClass->GetSuperClass())
		{
			Dump.ParentClassPath = SuperCls->GetPathName();
			Dump.ParentClassName = SuperCls->GetName();
			if (!Dump.ParentClassName.StartsWith(TEXT("A")) && !Dump.ParentClassName.StartsWith(TEXT("U")))
			{
				Dump.ParentClassName = FString::Printf(TEXT("U%s"), *Dump.ParentClassName);
			}
			Dump.bIsActorDerived = SuperCls->IsChildOf(AActor::StaticClass());
		}
	}

	BlueprintBusterParsers::ParseSimpleConstructionScript(Blueprint, Dump.Components);
	BlueprintBusterParsers::ParseClassDefaultObject(Blueprint, Dump.Defaults);
	BlueprintBusterParsers::ParseExecutionGraphs(Blueprint, Dump.EventTrees, Dump.UnsupportedNodeCount, Dump.TotalNodeCount, MaxDepth);
	BlueprintBusterParsers::ParseFunctionGraphs(Blueprint, Dump.CustomFunctions, Dump.UnsupportedNodeCount, Dump.TotalNodeCount, MaxDepth);
	CollectBlueprintDependencies(Dump);

	const FString FinalOutPath = OutputFilePath.IsEmpty() ? MakeDumpFilePath(FString(), Blueprint) : OutputFilePath;
	const FString FinalOutDir = FPaths::GetPath(FinalOutPath);
	IFileManager::Get().MakeDirectory(*FinalOutDir, true);

	if (!WriteJsonToFile(DumpToJson(Dump), FinalOutPath))
	{
		return false;
	}

	if (OutDump)
	{
		*OutDump = MoveTemp(Dump);
	}
	if (bFailOnUnsupported)
	{
		return Dump.UnsupportedNodeCount == 0;
	}
	return true;
}

bool BlueprintBusterDump::DumpBlueprintToJsonFilesRecursive(UBlueprint* Blueprint,
                                                           const FString& OutputDir,
                                                           int32 MaxDepth,
                                                           int32 MaxDependencyDepth,
                                                           FBPDumpData* OutRootDump,
                                                           bool bFailOnUnsupported,
                                                           TSet<FString>* InOutVisitedBlueprintPaths)
{
	if (!IsValid(Blueprint))
	{
		return false;
	}

	const FString BaseDir = OutputDir.IsEmpty() ? GetDefaultDumpDirectory() : OutputDir;

	TSet<FString> LocalVisited;
	TSet<FString>& Visited = InOutVisitedBlueprintPaths ? *InOutVisitedBlueprintPaths : LocalVisited;

	TArray<TPair<FString, int32>> Queue;
	Queue.Reserve(32);

	const FString RootPath = Blueprint->GetPathName();
	if (!Visited.Contains(RootPath))
	{
		Visited.Add(RootPath);
		Queue.Add({ RootPath, 0 });
	}

	bool bOk = true;
	bool bRootDumpCaptured = false;

	for (int32 Index = 0; Index < Queue.Num(); ++Index)
	{
		const FString CurrentPath = Queue[Index].Key;
		const int32 Depth = Queue[Index].Value;

		UBlueprint* Current = (CurrentPath == RootPath) ? Blueprint : LoadObject<UBlueprint>(nullptr, *CurrentPath);
		if (!IsValid(Current))
		{
			UE_LOG(LogBlueprintBuster, Error, TEXT("Could not load dependent blueprint '%s'"), *CurrentPath);
			return false;
		}

		const FString OutPath = MakeDumpFilePath(BaseDir, Current);
		FBPDumpData Dump;
		const bool bDumpOk = DumpBlueprintToJsonFile(Current, OutPath, MaxDepth, &Dump, bFailOnUnsupported);
		if (!bDumpOk)
		{
			bOk = false;
			if (bFailOnUnsupported)
			{
				UE_LOG(LogBlueprintBuster, Error, TEXT("FullDump failed for '%s' (unsupported nodes remain). JSON still written: %s"), *Current->GetName(), *OutPath);
				return false;
			}
		}

		if (!bRootDumpCaptured && CurrentPath == RootPath)
		{
			if (OutRootDump)
			{
				*OutRootDump = Dump;
			}
			bRootDumpCaptured = true;
		}

		if (Depth >= MaxDependencyDepth)
		{
			continue;
		}

		for (const FString& Dep : Dump.DependencyBlueprintPaths)
		{
			if (!Visited.Contains(Dep))
			{
				Visited.Add(Dep);
				Queue.Add({ Dep, Depth + 1 });
			}
		}
	}

	return bOk;
}

