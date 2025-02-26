﻿#include "CSReinstancer.h"

#include "BlueprintCompilationManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FunctionTerminator.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_StructOperation.h"
#include "UnrealSharpCore/TypeGenerator/Register/CSTypeRegistry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UnrealSharpBlueprint/K2Node_CSAsyncAction.h"

FCSReinstancer& FCSReinstancer::Get()
{
	static FCSReinstancer Instance;
	return Instance;
}

void FCSReinstancer::Initialize()
{
	FCSTypeRegistry::Get().GetOnNewClassEvent().AddRaw(this, &FCSReinstancer::AddPendingClass);
	FCSTypeRegistry::Get().GetOnNewStructEvent().AddRaw(this, &FCSReinstancer::AddPendingStruct);
	FCSTypeRegistry::Get().GetOnNewInterfaceEvent().AddRaw(this, &FCSReinstancer::AddPendingInterface);
}

void FCSReinstancer::AddPendingClass(UClass* NewClass)
{
	ClassesToReinstance.Add(NewClass);
}

void FCSReinstancer::AddPendingStruct(UScriptStruct* NewStruct)
{
	StructsToReinstance.Add(NewStruct);
}

void FCSReinstancer::AddPendingInterface(UClass* NewInterface)
{
	InterfacesToReinstance.Add(NewInterface);
}

bool FCSReinstancer::TryUpdatePin(FEdGraphPinType& PinType) const
{
	UObject* PinSubCategoryObject = PinType.PinSubCategoryObject.Get();
	
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		UScriptStruct* Struct = Cast<UScriptStruct>(PinSubCategoryObject);
		if (UScriptStruct* const * FoundStruct = StructsToReinstance.Find(Struct))
		{
			PinType.PinSubCategoryObject = *FoundStruct;
			return true;
		}
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		UEnum* Enum = Cast<UEnum>(PinSubCategoryObject);
		
		if (!Enum || Enum->GetOutermost() != UCSManager::Get().GetUnrealSharpPackage())
		{
			return false;
		}

		// Enums are not reinstanced, so we need to check if the enum is still valid
		if (FCSTypeRegistry::GetEnumFromName(Enum->GetFName()))
		{
			PinType.PinSubCategoryObject = Enum;
			return true;
		}
	}
	else if (PinType.IsMap())
	{
		bool bChanged = false;
		{
			UScriptStruct* Struct = Cast<UScriptStruct>(PinSubCategoryObject);
			if (UScriptStruct* const * FoundStruct = StructsToReinstance.Find(Struct))
			{
				PinType.PinSubCategoryObject = *FoundStruct;
				bChanged = true;
			}
		}
		
		UObject* MapValueType = PinType.PinValueType.TerminalSubCategoryObject.Get();
		if (UScriptStruct* Struct = Cast<UScriptStruct>(MapValueType))
		{
			if (UScriptStruct* const * FoundStruct = StructsToReinstance.Find(Struct))
			{
				PinType.PinValueType.TerminalSubCategoryObject = *FoundStruct;
				bChanged = true;
			}
		}

		return bChanged;
	}
	else if (PinType.IsSet() || PinType.IsArray())
	{
		UScriptStruct* Struct = Cast<UScriptStruct>(PinSubCategoryObject);
		if (UScriptStruct* const * FoundStruct = StructsToReinstance.Find(Struct))
		{
			PinType.PinSubCategoryObject = *FoundStruct;
			return  true;
		}
	}
	else if (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Class
		|| PinType.PinSubCategory == UEdGraphSchema_K2::PC_Object
		|| PinType.PinSubCategory == UEdGraphSchema_K2::PC_SoftObject
		|| PinType.PinSubCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		UClass* Interface = Cast<UClass>(PinSubCategoryObject);
		if (UClass* const * FoundInterface = InterfacesToReinstance.Find(Interface))
		{
			PinType.PinSubCategoryObject = *FoundInterface;
			return true;
		}
	}
	return false;
}

void FCSReinstancer::FinishHotReload()
{
	UpdateBlueprints();
	FixDataTables();
	
	if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyModule->NotifyCustomizationModuleChanged();
	}
	
	StructsToReinstance.Empty();
	InterfacesToReinstance.Empty();
	ClassesToReinstance.Empty();
	
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
}

void FCSReinstancer::FixDataTables()
{
	for (UScriptStruct* Struct : StructsToReinstance)
	{
		TArray<UDataTable*> Tables;
		GetTablesDependentOnStruct(Struct, Tables);
		
		for (UDataTable*& Table : Tables)
		{
			FString Data = Table->GetTableAsJSON();
			Struct->StructFlags = static_cast<EStructFlags>(STRUCT_NoDestructor | Struct->StructFlags);
			Table->CleanBeforeStructChange();
			Table->RowStruct = Struct;
			Table->CreateTableFromJSONString(Data);
		}
	}
}

UFunction* FCSReinstancer::FindMatchingMember(const FMemberReference& FunctionReference) const
{
	const UClass* CurrentClassType = FunctionReference.GetMemberParentClass();
	if (!CurrentClassType)
	{
		return nullptr;
	}

	if (UClass* const * FoundNewClassType = ClassesToReinstance.Find(CurrentClassType))
	{
		if (UFunction* Function = (*FoundNewClassType)->FindFunctionByName(FunctionReference.GetMemberName()))
		{
			return Function;
		}
	}
	return nullptr;
}

bool FCSReinstancer::UpdateMemberCall(UK2Node_CallFunction* Node) const
{
	if (UFunction* NewMember = FindMatchingMember(Node->FunctionReference))
	{
		Node->SetFromFunction(NewMember);
		return true;
	}
	return false;
}

bool FCSReinstancer::UpdateMemberCall(UK2Node_CSAsyncAction * Node) const
{
	const TObjectPtr<UClass> CurrentProxyClass = Node->GetProxyClass();
	if (!CurrentProxyClass)
	{
		return false;
	}
	
	if (UClass* const * FoundNewClassType = ClassesToReinstance.Find(CurrentProxyClass))
	{
		if (UFunction* Function = (*FoundNewClassType)->FindFunctionByName(Node->GetFactoryFunctionName()))
		{
			UK2Node_CSAsyncAction::SetNodeFunc(Node, false, Function);
			return true;
		}
	}
	return false;
}

void FCSReinstancer::UpdateInheritance(UBlueprint* Blueprint, bool& RefNeedsNodeReconstruction) const
{
	//we do not loop through the parent classes, as every blueprint will get an update on its own.
	if(UClass* ParentClass = Blueprint->ParentClass)
	{
		if (UClass* const* NewClass = ClassesToReinstance.Find(ParentClass))
		{
			Blueprint->ParentClass = *NewClass;
			RefNeedsNodeReconstruction = true;
		}
	}

	for (FBPInterfaceDescription& InterfaceDescription : Blueprint->ImplementedInterfaces)
	{
		if (UClass* const* NewInterface = InterfacesToReinstance.Find(InterfaceDescription.Interface))
		{
			InterfaceDescription.Interface = *NewInterface;
			RefNeedsNodeReconstruction = true;
		}
		
		if (UClass* const* NewInterface = ClassesToReinstance.Find(InterfaceDescription.Interface))
		{
			InterfaceDescription.Interface = *NewInterface;
			RefNeedsNodeReconstruction = true;
		}
	}
}

void FCSReinstancer::UpdateNodePinTypes(UEdGraphNode* Node, bool& RefNeedsNodeReconstruction) const
{
	if (UK2Node_EditablePinBase* EditableNode = Cast<UK2Node_EditablePinBase>(Node))
	{
		for (const TSharedPtr<FUserPinInfo>& Pin : EditableNode->UserDefinedPins)
		{
			if (TryUpdatePin(Pin->PinType))
			{
				RefNeedsNodeReconstruction = true;
			}
		}
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (TryUpdatePin(Pin->PinType))
		{
			RefNeedsNodeReconstruction = true;
		}
	}
}

void FCSReinstancer::UpdateBlueprints()
{
	TSet<UBlueprint*> ToUpdate;
	
	for (TObjectIterator<UBlueprint> BlueprintIt; BlueprintIt; ++BlueprintIt)
	{
		bool bNeedsNodeReconstruction = false;
		UBlueprint* Blueprint = *BlueprintIt;

		if (FCSGeneratedClassBuilder::IsManagedType(Blueprint->GeneratedClass))
		{
			continue;
		}

		UpdateInheritance(Blueprint, bNeedsNodeReconstruction);
		
		for (FBPVariableDescription& NewVariable : Blueprint->NewVariables)
		{
			TryUpdatePin(NewVariable.VarType);
		}

		TArray<UK2Node*> AllNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, AllNodes);
		
		for (UK2Node* Node : AllNodes)
		{
			if (UK2Node_CallFunction* CallFunction = Cast<UK2Node_CallFunction>(Node))
			{
				if(UpdateMemberCall(CallFunction))
				{
					bNeedsNodeReconstruction = true;
				}
			}
			else if (UK2Node_CSAsyncAction* CustomEvent = Cast<UK2Node_CSAsyncAction>(Node))
			{
				if(UpdateMemberCall(CustomEvent))
				{
					bNeedsNodeReconstruction = true;
				}
			}
			else if (UK2Node_StructOperation* StructOperation = Cast<UK2Node_StructOperation>(Node))
			{
				if (UScriptStruct* const * FoundNewStructType = StructsToReinstance.Find(StructOperation->StructType))
				{
					StructOperation->StructType = *FoundNewStructType;
					bNeedsNodeReconstruction = true;
				}
			}
			else if (UK2Node_DynamicCast* Node_DynamicCast = Cast<UK2Node_DynamicCast>(Node))
			{
				if (UClass* const * FoundNewStructType = ClassesToReinstance.Find(Node_DynamicCast->TargetType))
				{
					Node_DynamicCast->TargetType = *FoundNewStructType;	
					bNeedsNodeReconstruction = true;
				}
			}
			else if (UK2Node_MacroInstance* Node_MacroInstance = Cast<UK2Node_MacroInstance>(Node))
			{
				if (TryUpdatePin(Node_MacroInstance->ResolvedWildcardType))
				{
					bNeedsNodeReconstruction = true;
				}
			}
			else if (UK2Node_FunctionTerminator* FunctionTerminator = Cast<UK2Node_FunctionTerminator>(Node))
			{
				if (const UClass* CurrentClassType = FunctionTerminator->FunctionReference.GetMemberParentClass())
				{
					if (UClass* const * FoundNewClassType = ClassesToReinstance.Find(CurrentClassType))
					{
						FGuid MemberGuid;
						UBlueprint::GetFunctionGuidFromClassByFieldName(*FoundNewClassType, FunctionTerminator->FunctionReference.GetMemberName(), MemberGuid); // there is not necessarily a guid
						FMemberReference MemberReference;
						MemberReference.SetDirect(FunctionTerminator->FunctionReference.GetMemberName(), MemberGuid, *FoundNewClassType, false);
						FunctionTerminator->FunctionReference = MemberReference;
						bNeedsNodeReconstruction = true;
					}
				}
			}
			
			UpdateNodePinTypes(Node, bNeedsNodeReconstruction);
		}

		if (bNeedsNodeReconstruction)
		{
			ToUpdate.Add(Blueprint);
		}
	}

	//in case blueprints will use u# types like in functions they will need to regenerate their BP skeleton to pick up the changed u# type versions.
	//otherwise they will revert all our node pin updates in BP functions back to the old types after a call to Schema::ReconstructNode since the function parameter infos still refer to the old types in the class/skeleton.
	//in other words the call to FBlueprintEditorUtils::RefreshAllNodes is important, as it will make sure that the BP skeleton is updated with the new u# types and not just the node pins.
	//and since we don't want to modify the class skeleton (which would potentially dangerous and its also closed api), we need to refresh the nodes with unreal api to make sure we do not miss important operations. and this is also more robust against future epic changes.
	//possibly we can improve performance by only refreshing single nodes that have been changed, but for now we will refresh just all nodes in a BP which seems performing well enough.
	//if performance will become an issue, we should look into parallelizing the update and moving the whole compilation and refreshing task onto threads and a background running task like epic does it for c++ reloads too.
	for (UBlueprint* Blueprint : ToUpdate)
	{
		FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	}

	for (UBlueprint* Blueprint : ToUpdate)
	{
		FBlueprintCompilationManager::QueueForCompilation(Blueprint);
	}
		
	FBlueprintCompilationManager::FlushCompilationQueueAndReinstance();
}

void FCSReinstancer::GetTablesDependentOnStruct(UScriptStruct* Struct, TArray<UDataTable*>& DataTables)
{
	TArray<UObject*> FoundDataTables;
	GetObjectsOfClass(UDataTable::StaticClass(), FoundDataTables);
	
	for (UObject* DataTableObj : FoundDataTables)
	{
		UDataTable* DataTable = static_cast<UDataTable*>(DataTableObj);
		if (DataTable->RowStruct != Struct)
		{
			continue;
		}
		DataTables.Add(DataTable);
	}
}
