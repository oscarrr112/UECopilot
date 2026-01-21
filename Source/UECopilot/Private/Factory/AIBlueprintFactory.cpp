// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/AIBlueprintFactory.h"
#include "UECopilot.h"
#include "Factory/NodeSpawner.h"
#include "Factory/LayoutEngine.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "EditorAssetLibrary.h"

DEFINE_LOG_CATEGORY(LogBlueprintFactory);

FBlueprintGenerationResult UAIBlueprintFactory::CreateBlueprint(
	const FBlueprintData& Data,
	const FString& PackagePath,
	bool bAutoLayout)
{
	FBlueprintGenerationResult Result;

	// Validate input
	if (Data.Name.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Blueprint name is required");
		return Result;
	}

	// Sanitize blueprint name (remove invalid characters)
	FString SanitizedName = Data.Name;
	SanitizedName = FPaths::MakeValidFileName(SanitizedName);
	SanitizedName.ReplaceInline(TEXT(" "), TEXT("_"));

	// Resolve parent class
	UClass* ParentClass = ResolveParentClass(Data.ParentClass);
	if (!ParentClass)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Could not resolve parent class: %s"), *Data.ParentClass);
		return Result;
	}

	// Check if blueprint already exists
	FString PackageName = PackagePath / SanitizedName;
	FString AssetPath = PackageName + TEXT(".") + SanitizedName;
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Blueprint already exists at: %s. Use /modify to modify existing blueprints."), *AssetPath);
		return Result;
	}

	// Create package
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to create package: %s"), *PackageName);
		return Result;
	}

	// Mark package as fully loaded
	Package->FullyLoad();

	// Create blueprint
	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		FName(*SanitizedName),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass()
	);

	if (!NewBlueprint)
	{
		Result.ErrorMessage = TEXT("Failed to create blueprint");
		return Result;
	}

	// Create variables
	CreateVariables(NewBlueprint, Data.Variables, Result.Warnings);

	// Create event graphs
	for (const FBlueprintGraphData& GraphData : Data.EventGraphs)
	{
		TMap<FString, UK2Node*> NodeMap;
		if (CreateEventGraph(NewBlueprint, GraphData, NodeMap, Result.Warnings))
		{
			// Connect nodes
			TArray<FString> ConnErrors;
			ConnectNodes(NodeMap, GraphData.Nodes, ConnErrors);
			Result.Warnings.Append(ConnErrors);

			// Merge node maps
			for (const auto& Pair : NodeMap)
			{
				Result.CreatedNodes.Add(Pair.Key, Pair.Value);
			}
		}
	}

	// Create function graphs
	for (const FBlueprintGraphData& GraphData : Data.Functions)
	{
		TMap<FString, UK2Node*> NodeMap;
		if (CreateFunctionGraph(NewBlueprint, GraphData, NodeMap, Result.Warnings))
		{
			// Connect nodes
			TArray<FString> ConnErrors;
			ConnectNodes(NodeMap, GraphData.Nodes, ConnErrors);
			Result.Warnings.Append(ConnErrors);

			// Merge node maps
			for (const auto& Pair : NodeMap)
			{
				Result.CreatedNodes.Add(Pair.Key, Pair.Value);
			}
		}
	}

	// Apply auto layout
	if (bAutoLayout)
	{
		FLayoutSettings LayoutSettings;
		LayoutSettings.HorizontalSpacing = 350.0f;  // Wider spacing for readability
		LayoutSettings.VerticalSpacing = 120.0f;   // Tighter vertical spacing
		LayoutSettings.bPrioritizeExecFlow = false; // Also consider data flow for pure nodes

		for (UEdGraph* Graph : NewBlueprint->UbergraphPages)
		{
			ULayoutEngine::AutoLayoutGraph(Graph, LayoutSettings);
		}
		for (UEdGraph* Graph : NewBlueprint->FunctionGraphs)
		{
			ULayoutEngine::AutoLayoutGraph(Graph, LayoutSettings);
		}
	}

	// Compile blueprint
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(NewBlueprint);
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	// Save package
	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

	// Ensure the directory exists
	FString PackageDir = FPaths::GetPath(PackageFileName);
	if (!FPaths::DirectoryExists(PackageDir))
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*PackageDir);
	}

	// Mark package as dirty and fully loaded before saving
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;

	FSavePackageResultStruct SaveResult = UPackage::Save(Package, NewBlueprint, *PackageFileName, SaveArgs);

	if (SaveResult.Result != ESavePackageResult::Success)
	{
		// Even if save fails, the blueprint is created in memory
		// User can save it manually from editor
		Result.Warnings.Add(FString::Printf(TEXT("Blueprint created but save failed. Save manually from editor. Path: %s"), *PackageFileName));
		UE_LOG(LogBlueprintFactory, Warning, TEXT("Failed to save package: %s"), *PackageFileName);
	}

	// Register with asset registry
	FAssetRegistryModule::AssetCreated(NewBlueprint);

	Result.bSuccess = true;
	Result.Blueprint = NewBlueprint;

	UE_LOG(LogBlueprintFactory, Log, TEXT("Successfully created blueprint: %s"), *SanitizedName);

	return Result;
}

FBlueprintGenerationResult UAIBlueprintFactory::CreatePreviewBlueprint(const FBlueprintData& Data)
{
	FBlueprintGenerationResult Result;

	// Resolve parent class
	UClass* ParentClass = ResolveParentClass(Data.ParentClass);
	if (!ParentClass)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Could not resolve parent class: %s"), *Data.ParentClass);
		return Result;
	}

	// Create transient package
	UPackage* TransientPackage = GetTransientPackage();

	// Create transient blueprint
	FString UniqueName = FString::Printf(TEXT("PREVIEW_%s_%d"), *Data.Name, FMath::Rand());

	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		TransientPackage,
		FName(*UniqueName),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass()
	);

	if (!NewBlueprint)
	{
		Result.ErrorMessage = TEXT("Failed to create preview blueprint");
		return Result;
	}

	// Mark as transient
	NewBlueprint->SetFlags(RF_Transient);

	// Create variables
	CreateVariables(NewBlueprint, Data.Variables, Result.Warnings);

	// Create event graphs
	for (const FBlueprintGraphData& GraphData : Data.EventGraphs)
	{
		TMap<FString, UK2Node*> NodeMap;
		if (CreateEventGraph(NewBlueprint, GraphData, NodeMap, Result.Warnings))
		{
			TArray<FString> ConnErrors;
			ConnectNodes(NodeMap, GraphData.Nodes, ConnErrors);
			Result.Warnings.Append(ConnErrors);

			for (const auto& Pair : NodeMap)
			{
				Result.CreatedNodes.Add(Pair.Key, Pair.Value);
			}
		}
	}

	// Create function graphs
	for (const FBlueprintGraphData& GraphData : Data.Functions)
	{
		TMap<FString, UK2Node*> NodeMap;
		if (CreateFunctionGraph(NewBlueprint, GraphData, NodeMap, Result.Warnings))
		{
			TArray<FString> ConnErrors;
			ConnectNodes(NodeMap, GraphData.Nodes, ConnErrors);
			Result.Warnings.Append(ConnErrors);

			for (const auto& Pair : NodeMap)
			{
				Result.CreatedNodes.Add(Pair.Key, Pair.Value);
			}
		}
	}

	// Auto layout
	for (UEdGraph* Graph : NewBlueprint->UbergraphPages)
	{
		ULayoutEngine::AutoLayoutGraph(Graph);
	}
	for (UEdGraph* Graph : NewBlueprint->FunctionGraphs)
	{
		ULayoutEngine::AutoLayoutGraph(Graph);
	}

	Result.bSuccess = true;
	Result.Blueprint = NewBlueprint;

	return Result;
}

FBlueprintGenerationResult UAIBlueprintFactory::ModifyBlueprint(
	UBlueprint* Blueprint,
	const FBlueprintData& Data,
	bool bMerge)
{
	FBlueprintGenerationResult Result;

	if (!Blueprint)
	{
		Result.ErrorMessage = TEXT("Blueprint is null");
		return Result;
	}

	// Add new variables
	for (const FBlueprintVariableData& VarData : Data.Variables)
	{
		AddVariable(Blueprint, VarData);
	}

	// Add/modify graphs
	for (const FBlueprintGraphData& GraphData : Data.EventGraphs)
	{
		FBlueprintGenerationResult GraphResult = AddGraph(Blueprint, GraphData);
		Result.Warnings.Append(GraphResult.Warnings);

		for (const auto& Pair : GraphResult.CreatedNodes)
		{
			Result.CreatedNodes.Add(Pair.Key, Pair.Value);
		}
	}

	for (const FBlueprintGraphData& GraphData : Data.Functions)
	{
		FBlueprintGraphData FuncData = GraphData;
		FuncData.bIsFunction = true;
		FBlueprintGenerationResult GraphResult = AddGraph(Blueprint, FuncData);
		Result.Warnings.Append(GraphResult.Warnings);

		for (const auto& Pair : GraphResult.CreatedNodes)
		{
			Result.CreatedNodes.Add(Pair.Key, Pair.Value);
		}
	}

	// Compile
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	Result.bSuccess = true;
	Result.Blueprint = Blueprint;

	return Result;
}

FBlueprintGenerationResult UAIBlueprintFactory::AddGraph(
	UBlueprint* Blueprint,
	const FBlueprintGraphData& GraphData)
{
	FBlueprintGenerationResult Result;

	if (!Blueprint)
	{
		Result.ErrorMessage = TEXT("Blueprint is null");
		return Result;
	}

	TMap<FString, UK2Node*> NodeMap;

	if (GraphData.bIsFunction)
	{
		CreateFunctionGraph(Blueprint, GraphData, NodeMap, Result.Warnings);
	}
	else
	{
		CreateEventGraph(Blueprint, GraphData, NodeMap, Result.Warnings);
	}

	// Connect nodes
	TArray<FString> ConnErrors;
	ConnectNodes(NodeMap, GraphData.Nodes, ConnErrors);
	Result.Warnings.Append(ConnErrors);

	Result.CreatedNodes = NodeMap;
	Result.bSuccess = true;
	Result.Blueprint = Blueprint;

	return Result;
}

bool UAIBlueprintFactory::AddVariable(UBlueprint* Blueprint, const FBlueprintVariableData& VarData)
{
	if (!Blueprint)
	{
		return false;
	}

	FName VarName(*VarData.Name);

	// Check if already exists
	for (const FBPVariableDescription& Existing : Blueprint->NewVariables)
	{
		if (Existing.VarName == VarName)
		{
			return true; // Already exists
		}
	}

	// Create new variable
	FEdGraphPinType PinType = VarTypeToPinType(VarData);

	FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, PinType);

	// Find and configure the variable
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarName)
		{
			Var.PropertyFlags |= VarData.bInstanceEditable ? CPF_Edit : 0;
			Var.PropertyFlags |= VarData.bExposeOnSpawn ? CPF_ExposeOnSpawn : 0;

			if (!VarData.Category.IsEmpty())
			{
				Var.Category = FText::FromString(VarData.Category);
			}

			if (!VarData.Tooltip.IsEmpty())
			{
				Var.SetMetaData(FBlueprintMetadata::MD_Tooltip, VarData.Tooltip);
			}

			if (!VarData.DefaultValue.IsEmpty())
			{
				Var.DefaultValue = VarData.DefaultValue;
			}

			break;
		}
	}

	return true;
}

int32 UAIBlueprintFactory::ConnectNodes(
	const TMap<FString, UK2Node*>& NodeMap,
	const TArray<FBlueprintNodeData>& NodeData,
	TArray<FString>& OutErrors)
{
	int32 SuccessfulConnections = 0;

	for (const FBlueprintNodeData& Data : NodeData)
	{
		UK2Node* const* TargetNodePtr = NodeMap.Find(Data.NodeId);
		if (!TargetNodePtr || !*TargetNodePtr)
		{
			continue;
		}

		UK2Node* TargetNode = *TargetNodePtr;

		for (const FBlueprintPinData& PinData : Data.Pins)
		{
			// Handle default values for pins without connections
			if (!PinData.DefaultValue.IsEmpty() && PinData.Connections.Num() == 0)
			{
				UEdGraphPin* Pin = FindPinByName(TargetNode, PinData.Name, EGPD_Input);
				if (Pin)
				{
					Pin->DefaultValue = PinData.DefaultValue;
					UE_LOG(LogUECopilot, Log, TEXT("Set default value for pin %s.%s = %s"),
						*Data.NodeId, *PinData.Name, *PinData.DefaultValue);
				}
			}

			for (const FBlueprintPinConnection& Conn : PinData.Connections)
			{
				UK2Node* const* SourceNodePtr = NodeMap.Find(Conn.SourceNodeId);
				if (!SourceNodePtr || !*SourceNodePtr)
				{
					OutErrors.Add(FString::Printf(TEXT("Source node not found: %s"), *Conn.SourceNodeId));
					continue;
				}

				UK2Node* SourceNode = *SourceNodePtr;

				// Find pins
				UEdGraphPin* TargetPin = FindPinByName(TargetNode, PinData.Name, EGPD_Input);
				UEdGraphPin* SourcePin = FindPinByName(SourceNode, Conn.SourcePinName, EGPD_Output);

				if (!TargetPin)
				{
					// Try output direction (for bidirectional search)
					TargetPin = FindPinByName(TargetNode, PinData.Name, EGPD_Output);
				}

				if (!SourcePin)
				{
					// Try input direction
					SourcePin = FindPinByName(SourceNode, Conn.SourcePinName, EGPD_Input);
				}

				if (!TargetPin)
				{
					OutErrors.Add(FString::Printf(TEXT("Target pin not found: %s.%s"), *Data.NodeId, *PinData.Name));
					continue;
				}

				if (!SourcePin)
				{
					OutErrors.Add(FString::Printf(TEXT("Source pin not found: %s.%s"), *Conn.SourceNodeId, *Conn.SourcePinName));
					continue;
				}

				// Make connection
				if (SourcePin->Direction == EGPD_Output && TargetPin->Direction == EGPD_Input)
				{
					SourcePin->MakeLinkTo(TargetPin);
					SuccessfulConnections++;
				}
				else if (SourcePin->Direction == EGPD_Input && TargetPin->Direction == EGPD_Output)
				{
					TargetPin->MakeLinkTo(SourcePin);
					SuccessfulConnections++;
				}
			}
		}
	}

	return SuccessfulConnections;
}

UClass* UAIBlueprintFactory::ResolveParentClass(const FString& ParentClassPath)
{
	if (ParentClassPath.IsEmpty())
	{
		return AActor::StaticClass();
	}

	// Try common shortcuts
	static TMap<FString, UClass*> CommonClasses = {
		{TEXT("Actor"), AActor::StaticClass()},
		{TEXT("Pawn"), APawn::StaticClass()},
		{TEXT("Character"), ACharacter::StaticClass()},
		{TEXT("PlayerController"), APlayerController::StaticClass()},
		{TEXT("GameModeBase"), AGameModeBase::StaticClass()},
		{TEXT("ActorComponent"), UActorComponent::StaticClass()},
		{TEXT("SceneComponent"), USceneComponent::StaticClass()},
	};

	// Check common classes first
	if (UClass** Found = CommonClasses.Find(ParentClassPath))
	{
		return *Found;
	}

	// Try to load by path
	return UNodeSpawner::FindClassByPath(ParentClassPath);
}

void UAIBlueprintFactory::CreateVariables(UBlueprint* Blueprint, const TArray<FBlueprintVariableData>& Variables, TArray<FString>& OutWarnings)
{
	for (const FBlueprintVariableData& VarData : Variables)
	{
		if (!AddVariable(Blueprint, VarData))
		{
			OutWarnings.Add(FString::Printf(TEXT("Failed to create variable: %s"), *VarData.Name));
		}
	}
}

bool UAIBlueprintFactory::CreateEventGraph(
	UBlueprint* Blueprint,
	const FBlueprintGraphData& GraphData,
	TMap<FString, UK2Node*>& OutNodeMap,
	TArray<FString>& OutWarnings)
{
	if (!Blueprint)
	{
		return false;
	}

	// Use existing event graph or create new one
	UEdGraph* EventGraph = nullptr;

	if (GraphData.Name.IsEmpty() || GraphData.Name == TEXT("EventGraph"))
	{
		// Use default event graph
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			EventGraph = Blueprint->UbergraphPages[0];
		}
	}
	else
	{
		// Find or create named graph
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph->GetName() == GraphData.Name)
			{
				EventGraph = Graph;
				break;
			}
		}
	}

	if (!EventGraph)
	{
		EventGraph = FBlueprintEditorUtils::CreateNewGraph(
			Blueprint,
			FName(*GraphData.Name),
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass()
		);

		FBlueprintEditorUtils::AddUbergraphPage(Blueprint, EventGraph);
	}

	// Spawn nodes
	for (const FBlueprintNodeData& NodeData : GraphData.Nodes)
	{
		FNodeSpawnResult SpawnResult = UNodeSpawner::SpawnNode(EventGraph, NodeData, Blueprint);

		if (SpawnResult.bSuccess && SpawnResult.Node)
		{
			OutNodeMap.Add(NodeData.NodeId, SpawnResult.Node);
		}
		else
		{
			OutWarnings.Add(FString::Printf(TEXT("Failed to spawn node %s: %s"), *NodeData.NodeId, *SpawnResult.ErrorMessage));
		}
	}

	return true;
}

bool UAIBlueprintFactory::CreateFunctionGraph(
	UBlueprint* Blueprint,
	const FBlueprintGraphData& GraphData,
	TMap<FString, UK2Node*>& OutNodeMap,
	TArray<FString>& OutWarnings)
{
	if (!Blueprint || GraphData.Name.IsEmpty())
	{
		return false;
	}

	// Check if function already exists
	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetName() == GraphData.Name)
		{
			FunctionGraph = Graph;
			break;
		}
	}

	if (!FunctionGraph)
	{
		// Create new function
		FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
			Blueprint,
			FName(*GraphData.Name),
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass()
		);

		FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, FunctionGraph, false, nullptr);

		// Initialize function with entry node
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		Schema->CreateDefaultNodesForGraph(*FunctionGraph);
	}

	// Find entry and result nodes
	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : FunctionGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
		{
			EntryNode = Entry;
		}
		if (UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(Node))
		{
			ResultNode = Result;
		}
	}

	// Add function input parameters to Entry node
	if (EntryNode)
	{
		for (const FBlueprintPinData& Input : GraphData.Inputs)
		{
			// Parse type string to get pin type
			FEdGraphPinType PinType;
			if (Input.Type == TEXT("float") || Input.Type == TEXT("Float"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
				PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			}
			else if (Input.Type == TEXT("int") || Input.Type == TEXT("Int"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			}
			else if (Input.Type == TEXT("bool") || Input.Type == TEXT("Bool"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			}
			else if (Input.Type == TEXT("string") || Input.Type == TEXT("String"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_String;
			}
			else
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
				PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			}

			// Create user defined pin on entry node
			TSharedPtr<FUserPinInfo> PinInfo = MakeShareable(new FUserPinInfo());
			PinInfo->PinName = FName(*Input.Name);
			PinInfo->PinType = PinType;
			PinInfo->DesiredPinDirection = EGPD_Output;

			EntryNode->UserDefinedPins.Add(PinInfo);
		}

		// Reconstruct node to apply pin changes
		EntryNode->ReconstructNode();

		// Add Entry node to node map with special ID
		OutNodeMap.Add(TEXT("fn_entry"), EntryNode);
	}

	// Create Result node if we have outputs and it doesn't exist
	if (GraphData.Outputs.Num() > 0)
	{
		if (!ResultNode)
		{
			// Create result node
			FGraphNodeCreator<UK2Node_FunctionResult> NodeCreator(*FunctionGraph);
			ResultNode = NodeCreator.CreateNode();
			ResultNode->NodePosX = 800;
			ResultNode->NodePosY = 0;
			NodeCreator.Finalize();
		}

		// Add output parameters to Result node
		for (const FBlueprintPinData& Output : GraphData.Outputs)
		{
			FEdGraphPinType PinType;
			if (Output.Type == TEXT("float") || Output.Type == TEXT("Float"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
				PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			}
			else if (Output.Type == TEXT("int") || Output.Type == TEXT("Int"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			}
			else if (Output.Type == TEXT("bool") || Output.Type == TEXT("Bool"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			}
			else if (Output.Type == TEXT("string") || Output.Type == TEXT("String"))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_String;
			}
			else
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
				PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			}

			TSharedPtr<FUserPinInfo> PinInfo = MakeShareable(new FUserPinInfo());
			PinInfo->PinName = FName(*Output.Name);
			PinInfo->PinType = PinType;
			PinInfo->DesiredPinDirection = EGPD_Input;

			ResultNode->UserDefinedPins.Add(PinInfo);
		}

		ResultNode->ReconstructNode();

		// Add Result node to node map with special ID
		OutNodeMap.Add(TEXT("fn_result"), ResultNode);
	}

	// Spawn additional nodes (skip fn_entry and fn_result as they're already created)
	for (const FBlueprintNodeData& NodeData : GraphData.Nodes)
	{
		// Skip special nodes that are already in the NodeMap
		if (NodeData.NodeId == TEXT("fn_entry") || NodeData.NodeId == TEXT("fn_result"))
		{
			continue;
		}

		FNodeSpawnResult SpawnResult = UNodeSpawner::SpawnNode(FunctionGraph, NodeData, Blueprint);

		if (SpawnResult.bSuccess && SpawnResult.Node)
		{
			OutNodeMap.Add(NodeData.NodeId, SpawnResult.Node);
		}
		else
		{
			OutWarnings.Add(FString::Printf(TEXT("Failed to spawn node %s: %s"), *NodeData.NodeId, *SpawnResult.ErrorMessage));
		}
	}

	return true;
}

FEdGraphPinType UAIBlueprintFactory::GetPinType(EBlueprintVarType VarType, const FString& TypeClass)
{
	FEdGraphPinType PinType;

	switch (VarType)
	{
	case EBlueprintVarType::Boolean:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		break;
	case EBlueprintVarType::Integer:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		break;
	case EBlueprintVarType::Float:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		break;
	case EBlueprintVarType::String:
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		break;
	case EBlueprintVarType::Name:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		break;
	case EBlueprintVarType::Text:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		break;
	case EBlueprintVarType::Vector:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		break;
	case EBlueprintVarType::Rotator:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		break;
	case EBlueprintVarType::Transform:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		break;
	case EBlueprintVarType::Object:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		if (!TypeClass.IsEmpty())
		{
			PinType.PinSubCategoryObject = UNodeSpawner::FindClassByPath(TypeClass);
		}
		break;
	default:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		break;
	}

	return PinType;
}

FEdGraphPinType UAIBlueprintFactory::VarTypeToPinType(const FBlueprintVariableData& VarData)
{
	FEdGraphPinType PinType = GetPinType(VarData.Type, VarData.TypeClass);

	// Handle container types
	if (VarData.Type == EBlueprintVarType::Array)
	{
		PinType.ContainerType = EPinContainerType::Array;
		// Set element type based on ContainerElementType
		FEdGraphPinType ElementType = GetPinType(VarData.ContainerElementType, TEXT(""));
		PinType.PinCategory = ElementType.PinCategory;
		PinType.PinSubCategory = ElementType.PinSubCategory;
		PinType.PinSubCategoryObject = ElementType.PinSubCategoryObject;
	}
	else if (VarData.Type == EBlueprintVarType::Set)
	{
		PinType.ContainerType = EPinContainerType::Set;
	}
	else if (VarData.Type == EBlueprintVarType::Map)
	{
		PinType.ContainerType = EPinContainerType::Map;
	}

	return PinType;
}

UEdGraphPin* UAIBlueprintFactory::FindPinByName(UK2Node* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	// Exact match first
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinName)
		{
			if (Direction == EGPD_MAX || Pin->Direction == Direction)
			{
				return Pin;
			}
		}
	}

	// Handle positional argument names like "Arg0", "Arg1", etc.
	if (PinName.StartsWith(TEXT("Arg")))
	{
		FString IndexStr = PinName.Mid(3);
		if (IndexStr.IsNumeric())
		{
			int32 ArgIndex = FCString::Atoi(*IndexStr);
			int32 CurrentIndex = 0;

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input)
				{
					// Skip exec pins
					if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						continue;
					}
					// Skip self/world context pins
					if (Pin->PinName == TEXT("self") || Pin->PinName == TEXT("WorldContextObject"))
					{
						continue;
					}

					if (CurrentIndex == ArgIndex)
					{
						return Pin;
					}
					CurrentIndex++;
				}
			}
		}
	}

	// Try partial match (sometimes pins have prefixes/suffixes)
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin)
		{
			FString CurrentPinName = Pin->PinName.ToString();
			if (CurrentPinName.Contains(PinName) || PinName.Contains(CurrentPinName))
			{
				if (Direction == EGPD_MAX || Pin->Direction == Direction)
				{
					return Pin;
				}
			}
		}
	}

	return nullptr;
}
