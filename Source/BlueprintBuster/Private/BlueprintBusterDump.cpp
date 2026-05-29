#include "BlueprintBusterDump.h"

#include "BlueprintBusterParsers.h"

#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace
{
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
		if (!InNode->UnsupportedReason.IsEmpty())
		{
			Obj->SetStringField(TEXT("unsupported"), InNode->UnsupportedReason);
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

bool BlueprintBusterDump::DumpBlueprintToJsonFile(UBlueprint* Blueprint, const FString& OutputFilePath, int32 MaxDepth, FBPDumpData* OutDump)
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
	return true;
}

