// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/AIBlueprintFactory.h"
#include "AssetFactoryAI.h"
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
#include "Math/UnrealMathUtility.h"
#include "Math/Vector2D.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "EditorAssetLibrary.h"
#include "DynamicConfigUtils.h"

namespace DC = AssetFactoryAI::DynamicConfig;

namespace
{
	TArray<FString> ParseDelimitedList(const FString& Input, const TCHAR Delimiter)
	{
		TArray<FString> Values;
		const FString Delim(1, &Delimiter);
		Input.ParseIntoArray(Values, *Delim, true);
		for (FString& Value : Values)
		{
			Value.TrimStartAndEndInline();
		}
		Values.RemoveAll([](const FString& Value) { return Value.IsEmpty(); });
		return Values;
	}

	FString FormatDynamicMessage(const FString& Pattern, const FString& Arg)
	{
		FString Result = Pattern;
		Result.ReplaceInline(TEXT("%s"), *Arg);
		return Result;
	}

	const FString& GetFunctionEntryNodeId()
	{
		static const FString Value = DC::GetString(TEXT("FunctionEntryNodeId"), TEXT("fn_entry"));
		return Value;
	}

	const FString& GetFunctionResultNodeId()
	{
		static const FString Value = DC::GetString(TEXT("FunctionResultNodeId"), TEXT("fn_result"));
		return Value;
	}

	const FString& GetReceiveTickEventName()
	{
		static const FString Value = DC::GetString(TEXT("ReceiveTickEventName"), TEXT("ReceiveTick"));
		return Value;
	}

	const FString& GetEventTickNodeIdPrefix()
	{
		static const FString Value = DC::GetString(TEXT("EventTickNodeIdPrefix"), TEXT("event_tick"));
		return Value;
	}

	TArray<FString> CollectEventTickConsumers(const FBlueprintGraphData& GraphData);
	void EnsureTickNodesAppended(UBlueprint* Blueprint, const FBlueprintGraphData& GraphData, const TArray<FString>& TickConsumers, const TMap<FString, UK2Node*>& NodeMap);
	UEdGraphPin* FindNodePin(UK2Node* Node, const FString& PinName, EEdGraphPinDirection Direction = EGPD_MAX);
	UEdGraphPin* FindExecPin(UK2Node* Node, const FString& PinName, EEdGraphPinDirection Direction = EGPD_MAX);
	UEdGraphPin* FindExecPinByIndex(UK2Node* Node, EEdGraphPinDirection Direction, int32 Index, const FString& PreferredName = TEXT(""));
	void RemoveUnlinkedFunctionResultNodes(TMap<FString, UK2Node*>& NodeMap);
	void LayoutFunctionNodesInDeclaredOrder(const FBlueprintGraphData& GraphData, const TMap<FString, UK2Node*>& NodeMap);
	UEdGraph* ResolveGraphFromNodeMap(const TMap<FString, UK2Node*>& NodeMap);
	void PlaceNodesInRightwardFlow(const FBlueprintGraphData& GraphData, UEdGraph* Graph, const TMap<FString, UK2Node*>& NodeMap);
	void RunPostLayoutPass(UEdGraph* Graph);

	FString NormalizePinToken(const FString& Name)
	{
		FString Result = Name.ToLower();
		Result.ReplaceInline(TEXT(" "), TEXT(""));
		Result.ReplaceInline(TEXT("_"), TEXT(""));
		Result.ReplaceInline(TEXT("."), TEXT(""));
		return Result;
	}

	const TArray<TArray<FString>>& GetPinAliasGroups()
	{
		static const TArray<TArray<FString>> Groups = []()
		{
			const FString RawGroups = DC::GetString(
				TEXT("PinAliasGroups"),
				TEXT("execute,exec,then,in,input|then,true,out,output|else,false|condition,cond|returnvalue,return,result"));

			TArray<TArray<FString>> ParsedGroups;
			for (const FString& Group : ParseDelimitedList(RawGroups, '|'))
			{
				TArray<FString> Items;
				for (const FString& Item : ParseDelimitedList(Group, ','))
				{
					Items.Add(Item.ToLower());
				}
				if (Items.Num() > 0)
				{
					ParsedGroups.Add(Items);
				}
			}
			return ParsedGroups;
		}();
		return Groups;
	}

	bool ValueInAliasGroup(const FString& Value, const TArray<FString>& Group)
	{
		for (const FString& Item : Group)
		{
			if (Value == Item)
			{
				return true;
			}
		}
		return false;
	}

	bool PinNamesEquivalent(const FString& CandidateName, const FString& RequestedName)
	{
		const FString Candidate = NormalizePinToken(CandidateName);
		const FString Requested = NormalizePinToken(RequestedName);

		if (Candidate == Requested)
		{
			return true;
		}

		for (const TArray<FString>& Group : GetPinAliasGroups())
		{
			if (ValueInAliasGroup(Candidate, Group) && ValueInAliasGroup(Requested, Group))
			{
				return true;
			}
		}

		return false;
	}

	bool IsExecLikePinName(const FString& PinName)
	{
		const FString Name = NormalizePinToken(PinName);
		const TArray<TArray<FString>>& Groups = GetPinAliasGroups();
		return Groups.Num() > 0 ? ValueInAliasGroup(Name, Groups[0]) : false;
	}

	struct FLayoutRect
	{
		float X = 0.0f;
		float Y = 0.0f;
		float W = 300.0f;
		float H = 180.0f;
	};

	struct FRightwardFlowLayoutConfig
	{
		float NodeWidth = 320.0f;
		float NodeHeight = 180.0f;
		float HorizontalSpacing = 380.0f;
		float VerticalSpacing = 220.0f;
		float CollisionPadding = 40.0f;
	};

	struct FDeclaredOrderLayoutConfig
	{
		int32 StartX = 160;
		int32 StartY = 120;
		int32 StepX = 320;
		int32 StepY = 180;
	};

	FRightwardFlowLayoutConfig GetRightwardFlowLayoutConfig()
	{
		FRightwardFlowLayoutConfig Config;
		Config.NodeWidth = DC::GetFloat(TEXT("Rightward.NodeWidth"), Config.NodeWidth);
		Config.NodeHeight = DC::GetFloat(TEXT("Rightward.NodeHeight"), Config.NodeHeight);
		Config.HorizontalSpacing = DC::GetFloat(TEXT("Rightward.HorizontalSpacing"), Config.HorizontalSpacing);
		Config.VerticalSpacing = DC::GetFloat(TEXT("Rightward.VerticalSpacing"), Config.VerticalSpacing);
		Config.CollisionPadding = DC::GetFloat(TEXT("Rightward.CollisionPadding"), Config.CollisionPadding);
		return Config;
	}

	FDeclaredOrderLayoutConfig GetDeclaredOrderLayoutConfig()
	{
		FDeclaredOrderLayoutConfig Config;
		Config.StartX = DC::GetInt(TEXT("Declared.StartX"), Config.StartX);
		Config.StartY = DC::GetInt(TEXT("Declared.StartY"), Config.StartY);
		Config.StepX = DC::GetInt(TEXT("Declared.StepX"), Config.StepX);
		Config.StepY = DC::GetInt(TEXT("Declared.StepY"), Config.StepY);
		return Config;
	}

	FLayoutRect MakeRectFromNode(const UK2Node* Node)
	{
		FLayoutRect Rect;
		if (Node)
		{
			Rect.X = static_cast<float>(Node->NodePosX);
			Rect.Y = static_cast<float>(Node->NodePosY);
		}
		return Rect;
	}

	bool RectsOverlap(const FLayoutRect& A, const FLayoutRect& B, float Padding)
	{
		return (A.X < B.X + B.W + Padding) &&
			(A.X + A.W + Padding > B.X) &&
			(A.Y < B.Y + B.H + Padding) &&
			(A.Y + A.H + Padding > B.Y);
	}

	float FindAvailableY(const TArray<FLayoutRect>& Occupied, float X, float PreferredY, float Width, float Height, float Padding, float VerticalStep)
	{
		float CandidateY = PreferredY;
		for (int32 Attempts = 0; Attempts < 128; ++Attempts)
		{
			FLayoutRect Candidate{ X, CandidateY, Width, Height };
			bool bOverlaps = false;
			for (const FLayoutRect& Existing : Occupied)
			{
				if (RectsOverlap(Candidate, Existing, Padding))
				{
					bOverlaps = true;
					break;
				}
			}

			if (!bOverlaps)
			{
				return CandidateY;
			}

			CandidateY += VerticalStep;
		}

		return CandidateY;
	}

	FEdGraphPinType ParseFunctionPinTypeFromText(const FString& InType)
	{
		const FString Normalized = InType.ToLower();

		struct FSimplePinType
		{
			FName Category;
			FName SubCategory;
		};

		static const TMap<FString, FSimplePinType> SimpleTypes = {
			{ TEXT("float"), { UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double } },
			{ TEXT("double"), { UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double } },
			{ TEXT("int"), { UEdGraphSchema_K2::PC_Int, NAME_None } },
			{ TEXT("bool"), { UEdGraphSchema_K2::PC_Boolean, NAME_None } },
			{ TEXT("string"), { UEdGraphSchema_K2::PC_String, NAME_None } },
		};

		FEdGraphPinType PinType;
		if (const FSimplePinType* Type = SimpleTypes.Find(Normalized))
		{
			PinType.PinCategory = Type->Category;
			PinType.PinSubCategory = Type->SubCategory;
			return PinType;
		}

		// Keep previous fallback behavior.
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		return PinType;
	}

	FString NormalizeClassAlias(const FString& InPath)
	{
		FString Out = InPath;
		Out.TrimStartAndEndInline();
		return Out.ToLower();
	}

	const TMap<FString, UClass*>& GetParentClassAliasMap()
	{
		static const TMap<FString, UClass*> Aliases = []()
		{
			TMap<FString, UClass*> Map = {
				{ TEXT("actor"), AActor::StaticClass() },
				{ TEXT("pawn"), APawn::StaticClass() },
				{ TEXT("character"), ACharacter::StaticClass() },
				{ TEXT("playercontroller"), APlayerController::StaticClass() },
				{ TEXT("gamemodebase"), AGameModeBase::StaticClass() },
				{ TEXT("actorcomponent"), UActorComponent::StaticClass() },
				{ TEXT("scenecomponent"), USceneComponent::StaticClass() },
				{ TEXT("widget"), nullptr },
				{ TEXT("userwidget"), nullptr },
			};

			// Optional overrides/additions:
			// ParentClassAliases="ability=/Script/MyGame.BP_Ability_C;npc=/Script/MyGame.BP_NPC_C"
			for (const FString& PairText : ParseDelimitedList(DC::GetString(TEXT("ParentClassAliases"), TEXT("")), ';'))
			{
				FString Alias;
				FString ClassPath;
				if (!PairText.Split(TEXT("="), &Alias, &ClassPath))
				{
					continue;
				}

				Alias = NormalizeClassAlias(Alias);
				ClassPath.TrimStartAndEndInline();
				if (Alias.IsEmpty() || ClassPath.IsEmpty())
				{
					continue;
				}

				if (UClass* ResolvedClass = UNodeSpawner::FindClassByPath(ClassPath))
				{
					Map.Add(Alias, ResolvedClass);
				}
			}

			return Map;
		}();
		return Aliases;
	}

	UClass* ResolveWidgetAliasClass()
	{
		return UNodeSpawner::FindClassByPath(TEXT("/Script/UMG.UserWidget"));
	}

	const TMap<EBlueprintVarType, EPinContainerType>& GetContainerTypeMap()
	{
		static const TMap<EBlueprintVarType, EPinContainerType> Map = {
			{ EBlueprintVarType::Array, EPinContainerType::Array },
			{ EBlueprintVarType::Set, EPinContainerType::Set },
			{ EBlueprintVarType::Map, EPinContainerType::Map },
		};
		return Map;
	}
}

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
		Result.ErrorMessage = DC::GetString(TEXT("Factory.Error.BlueprintNameRequired"), TEXT("Blueprint name is required"));
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
		const FString Pattern = DC::GetString(TEXT("Factory.Error.ResolveParentClass"), TEXT("Could not resolve parent class: %s"));
		Result.ErrorMessage = FormatDynamicMessage(Pattern, Data.ParentClass);
		return Result;
	}

	// Check if blueprint already exists
	FString PackageName = PackagePath / SanitizedName;
	FString AssetPath = PackageName + TEXT(".") + SanitizedName;
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		const FString Pattern = DC::GetString(TEXT("Factory.Error.BlueprintExists"), TEXT("Blueprint already exists at: %s. Use /modify to modify existing blueprints."));
		Result.ErrorMessage = FormatDynamicMessage(Pattern, AssetPath);
		return Result;
	}

	// Create package
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		const FString Pattern = DC::GetString(TEXT("Factory.Error.CreatePackageFailed"), TEXT("Failed to create package: %s"));
		Result.ErrorMessage = FormatDynamicMessage(Pattern, PackageName);
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
		Result.ErrorMessage = DC::GetString(TEXT("Factory.Error.CreateBlueprintFailed"), TEXT("Failed to create blueprint"));
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
		LayoutSettings.HorizontalSpacing = DC::GetFloat(TEXT("AutoLayout.HorizontalSpacing"), 350.0f);
		LayoutSettings.VerticalSpacing = DC::GetFloat(TEXT("AutoLayout.VerticalSpacing"), 120.0f);
		LayoutSettings.bPrioritizeExecFlow = DC::GetBool(TEXT("AutoLayout.PrioritizeExecFlow"), false);

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
		const FString Pattern = DC::GetString(TEXT("Factory.Error.ResolveParentClass"), TEXT("Could not resolve parent class: %s"));
		Result.ErrorMessage = FormatDynamicMessage(Pattern, Data.ParentClass);
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
		Result.ErrorMessage = DC::GetString(TEXT("Factory.Error.CreatePreviewBlueprintFailed"), TEXT("Failed to create preview blueprint"));
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
		Result.ErrorMessage = DC::GetString(TEXT("Factory.Error.BlueprintNull"), TEXT("Blueprint is null"));
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
		FBlueprintGenerationResult GraphResult = AddGraph(Blueprint, GraphData, bMerge);
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
		FBlueprintGenerationResult GraphResult = AddGraph(Blueprint, FuncData, bMerge);
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
	const FBlueprintGraphData& GraphData,
	bool bMerge)
{
	FBlueprintGenerationResult Result;

	if (!Blueprint)
	{
		Result.ErrorMessage = DC::GetString(TEXT("Factory.Error.BlueprintNull"), TEXT("Blueprint is null"));
		return Result;
	}

	TMap<FString, UK2Node*> NodeMap;

	if (GraphData.bIsFunction)
	{
		CreateFunctionGraph(Blueprint, GraphData, NodeMap, Result.Warnings);
	}
	else
	{
		CreateEventGraph(Blueprint, GraphData, NodeMap, Result.Warnings, bMerge);
	}

	// Connect nodes
	TArray<FString> ConnErrors;
	ConnectNodes(NodeMap, GraphData.Nodes, ConnErrors);
	Result.Warnings.Append(ConnErrors);

	if (GraphData.bIsFunction)
	{
		RemoveUnlinkedFunctionResultNodes(NodeMap);
		LayoutFunctionNodesInDeclaredOrder(GraphData, NodeMap);
	}

	if (!GraphData.bIsFunction)
	{
		TArray<FString> TickConsumers = CollectEventTickConsumers(GraphData);
		if (TickConsumers.Num() > 0)
		{
			EnsureTickNodesAppended(Blueprint, GraphData, TickConsumers, NodeMap);
		}
	}

	if (UEdGraph* Graph = ResolveGraphFromNodeMap(NodeMap))
	{
		// Phase 1: deterministic rightward placement for newly generated nodes.
		PlaceNodesInRightwardFlow(GraphData, Graph, NodeMap);
		// Phase 2: whole-graph cleanup pass to reduce overlaps and tangled wires.
		RunPostLayoutPass(Graph);
	}

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
					UE_LOG(LogAssetFactoryAI, Log, TEXT("Set default value for pin %s.%s = %s"),
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
					// Ignore bogus exec links on pure nodes (e.g. Variable_Get.execute).
					if (IsExecLikePinName(PinData.Name) && FindExecPin(TargetNode, TEXT(""), EGPD_Input) == nullptr)
					{
						continue;
					}

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

	const FString Alias = NormalizeClassAlias(ParentClassPath);
	if (UClass* const* AliasClass = GetParentClassAliasMap().Find(Alias))
	{
		if (*AliasClass)
		{
			return *AliasClass;
		}

		if (Alias == TEXT("widget") || Alias == TEXT("userwidget"))
		{
			if (UClass* WidgetClass = ResolveWidgetAliasClass())
			{
				return WidgetClass;
			}
		}
	}

	// Try to load by path
	return UNodeSpawner::FindClassByPath(ParentClassPath);
}

namespace
{
	UEdGraph* ResolveGraphFromNodeMap(const TMap<FString, UK2Node*>& NodeMap)
	{
		for (const TPair<FString, UK2Node*>& Pair : NodeMap)
		{
			if (Pair.Value)
			{
				return Pair.Value->GetGraph();
			}
		}
		return nullptr;
	}

	void PlaceNodesInRightwardFlow(const FBlueprintGraphData& GraphData, UEdGraph* Graph, const TMap<FString, UK2Node*>& NodeMap)
	{
		if (!Graph || GraphData.Nodes.Num() == 0 || NodeMap.Num() == 0)
		{
			return;
		}

		const FRightwardFlowLayoutConfig LayoutConfig = GetRightwardFlowLayoutConfig();

		TSet<UK2Node*> NewNodes;
		for (const TPair<FString, UK2Node*>& Pair : NodeMap)
		{
			if (Pair.Value)
			{
				NewNodes.Add(Pair.Value);
			}
		}

		float MaxExistingRight = 0.0f;
		float MinExistingY = 0.0f;
		bool bHasExisting = false;
		TArray<FLayoutRect> OccupiedRects;

		for (UEdGraphNode* RawNode : Graph->Nodes)
		{
			UK2Node* ExistingNode = Cast<UK2Node>(RawNode);
			if (!ExistingNode)
			{
				continue;
			}

			if (!NewNodes.Contains(ExistingNode))
			{
				bHasExisting = true;
				MaxExistingRight = FMath::Max(MaxExistingRight, static_cast<float>(ExistingNode->NodePosX) + LayoutConfig.NodeWidth);
				MinExistingY = bHasExisting ? FMath::Min(MinExistingY, static_cast<float>(ExistingNode->NodePosY)) : static_cast<float>(ExistingNode->NodePosY);
				OccupiedRects.Add(MakeRectFromNode(ExistingNode));
			}
		}

		const float BaseX = bHasExisting ? (MaxExistingRight + LayoutConfig.HorizontalSpacing) : 0.0f;
		const float BaseY = bHasExisting ? MinExistingY : 0.0f;

		TMap<FString, int32> DeclOrder;
		TSet<FString> NodeIds;
		for (int32 Index = 0; Index < GraphData.Nodes.Num(); ++Index)
		{
			DeclOrder.Add(GraphData.Nodes[Index].NodeId, Index);
			NodeIds.Add(GraphData.Nodes[Index].NodeId);
		}

		TMap<FString, const FBlueprintNodeData*> NodeDataById;
		TMap<FString, int32> InDegree;
		TMap<FString, int32> LayerById;
		TMap<FString, TArray<FString>> SuccById;
		TMap<FString, TArray<FString>> PredById;
		for (const FBlueprintNodeData& NodeData : GraphData.Nodes)
		{
			NodeDataById.Add(NodeData.NodeId, &NodeData);
			InDegree.Add(NodeData.NodeId, 0);
			LayerById.Add(NodeData.NodeId, 0);
		}

		for (const FBlueprintNodeData& TargetNode : GraphData.Nodes)
		{
			for (const FBlueprintPinData& Pin : TargetNode.Pins)
			{
				for (const FBlueprintPinConnection& Conn : Pin.Connections)
				{
					if (!NodeIds.Contains(Conn.SourceNodeId))
					{
						continue;
					}

					SuccById.FindOrAdd(Conn.SourceNodeId).Add(TargetNode.NodeId);
					PredById.FindOrAdd(TargetNode.NodeId).Add(Conn.SourceNodeId);
					InDegree.FindOrAdd(TargetNode.NodeId) += 1;
				}
			}
		}

		TArray<FString> Queue;
		for (const FBlueprintNodeData& NodeData : GraphData.Nodes)
		{
			if (InDegree.FindRef(NodeData.NodeId) == 0)
			{
				Queue.Add(NodeData.NodeId);
			}
		}
		Queue.Sort([&DeclOrder](const FString& A, const FString& B)
		{
			return DeclOrder.FindRef(A) < DeclOrder.FindRef(B);
		});

		TArray<FString> TopoOrder;
		while (Queue.Num() > 0)
		{
			const FString Current = Queue[0];
			Queue.RemoveAt(0);
			TopoOrder.Add(Current);

			for (const FString& Succ : SuccById.FindOrAdd(Current))
			{
				LayerById.FindOrAdd(Succ) = FMath::Max(LayerById.FindRef(Succ), LayerById.FindRef(Current) + 1);
				int32& Degree = InDegree.FindOrAdd(Succ);
				Degree = FMath::Max(0, Degree - 1);
				if (Degree == 0)
				{
					Queue.Add(Succ);
				}
			}

			Queue.Sort([&DeclOrder](const FString& A, const FString& B)
			{
				return DeclOrder.FindRef(A) < DeclOrder.FindRef(B);
			});
		}

		for (const FBlueprintNodeData& NodeData : GraphData.Nodes)
		{
			if (!TopoOrder.Contains(NodeData.NodeId))
			{
				TopoOrder.Add(NodeData.NodeId);
			}
		}

		TMap<int32, int32> LayerRowCounters;
		for (const FString& NodeId : TopoOrder)
		{
			const FBlueprintNodeData* const* NodeDataPtr = NodeDataById.Find(NodeId);
			UK2Node* const* NodePtr = NodeMap.Find(NodeId);
			if (!NodeDataPtr || !*NodeDataPtr || !NodePtr || !*NodePtr)
			{
				continue;
			}

			const FBlueprintNodeData& NodeData = **NodeDataPtr;
			UK2Node* Node = *NodePtr;

			if (FMath::Abs(NodeData.Position.X) > KINDA_SMALL_NUMBER || FMath::Abs(NodeData.Position.Y) > KINDA_SMALL_NUMBER)
			{
				Node->NodePosX = static_cast<int32>(NodeData.Position.X);
				Node->NodePosY = static_cast<int32>(NodeData.Position.Y);
				OccupiedRects.Add(MakeRectFromNode(Node));
				continue;
			}

			if (Node->NodePosX != 0 || Node->NodePosY != 0)
			{
				OccupiedRects.Add(MakeRectFromNode(Node));
				continue;
			}

			const int32 Layer = LayerById.FindRef(NodeId);
			const float TargetX = BaseX + Layer * LayoutConfig.HorizontalSpacing;

			float PreferredY = BaseY + LayerRowCounters.FindOrAdd(Layer) * LayoutConfig.VerticalSpacing;
			TArray<float> SourceYs;
			for (const FString& PredId : PredById.FindOrAdd(NodeId))
			{
				if (UK2Node* const* PredNodePtr = NodeMap.Find(PredId))
				{
					if (*PredNodePtr)
					{
						SourceYs.Add(static_cast<float>((*PredNodePtr)->NodePosY));
					}
				}
			}

			for (const FBlueprintPinData& Pin : NodeData.Pins)
			{
				for (const FBlueprintPinConnection& Conn : Pin.Connections)
				{
					if (NodeIds.Contains(Conn.SourceNodeId))
					{
						continue;
					}
					if (UK2Node* ExternalNode = Cast<UK2Node>(FindObject<UEdGraphNode>(Graph, *Conn.SourceNodeId)))
					{
						SourceYs.Add(static_cast<float>(ExternalNode->NodePosY));
					}
				}
			}

			if (SourceYs.Num() > 0)
			{
				float SumY = 0.0f;
				for (float Y : SourceYs)
				{
					SumY += Y;
				}
				PreferredY = SumY / SourceYs.Num();
			}

			const float FinalY = FindAvailableY(
				OccupiedRects,
				TargetX,
				PreferredY,
				LayoutConfig.NodeWidth,
				LayoutConfig.NodeHeight,
				LayoutConfig.CollisionPadding,
				LayoutConfig.VerticalSpacing);
			Node->NodePosX = static_cast<int32>(TargetX);
			Node->NodePosY = static_cast<int32>(FinalY);

			LayerRowCounters.FindOrAdd(Layer) = LayerRowCounters.FindRef(Layer) + 1;
			OccupiedRects.Add(MakeRectFromNode(Node));
		}
	}

	void RunPostLayoutPass(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return;
		}

		FLayoutSettings LayoutSettings;
		LayoutSettings.HorizontalSpacing = DC::GetFloat(TEXT("PostLayout.HorizontalSpacing"), 380.0f);
		LayoutSettings.VerticalSpacing = DC::GetFloat(TEXT("PostLayout.VerticalSpacing"), 180.0f);
		LayoutSettings.bPrioritizeExecFlow = DC::GetBool(TEXT("PostLayout.PrioritizeExecFlow"), true);
		ULayoutEngine::AutoLayoutGraph(Graph, LayoutSettings);
	}

	void RemoveUnlinkedFunctionResultNodes(TMap<FString, UK2Node*>& NodeMap)
	{
		TArray<TPair<FString, UK2Node*>> ResultNodes;
		for (const TPair<FString, UK2Node*>& Pair : NodeMap)
		{
			if (Pair.Value && Pair.Value->IsA(UK2Node_FunctionResult::StaticClass()))
			{
				ResultNodes.Add(Pair);
			}
		}

		if (ResultNodes.Num() <= 1)
		{
			return;
		}

		bool bHasAnyLinkedResultNode = false;
		for (const TPair<FString, UK2Node*>& Pair : ResultNodes)
		{
			UK2Node* Node = Pair.Value;
			if (!Node)
			{
				continue;
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->LinkedTo.Num() > 0)
				{
					bHasAnyLinkedResultNode = true;
					break;
				}
			}
			if (bHasAnyLinkedResultNode)
			{
				break;
			}
		}

		for (const TPair<FString, UK2Node*>& Pair : ResultNodes)
		{
			UK2Node* Node = Pair.Value;
			if (!Node)
			{
				continue;
			}

			bool bHasLinks = false;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->LinkedTo.Num() > 0)
				{
					bHasLinks = true;
					break;
				}
			}

			if (bHasLinks)
			{
				continue;
			}

			if (Pair.Key.Equals(GetFunctionResultNodeId(), ESearchCase::IgnoreCase) && !bHasAnyLinkedResultNode)
			{
				continue;
			}

			NodeMap.Remove(Pair.Key);
			Node->DestroyNode();
		}
	}

	void LayoutFunctionNodesInDeclaredOrder(const FBlueprintGraphData& GraphData, const TMap<FString, UK2Node*>& NodeMap)
	{
		const FDeclaredOrderLayoutConfig LayoutConfig = GetDeclaredOrderLayoutConfig();

		int32 NextX = LayoutConfig.StartX;

		if (UK2Node* const* EntryPtr = NodeMap.Find(GetFunctionEntryNodeId()))
		{
			if (UK2Node* EntryNode = *EntryPtr)
			{
				EntryNode->NodePosX = LayoutConfig.StartX;
				EntryNode->NodePosY = LayoutConfig.StartY;
				NextX += LayoutConfig.StepX;
			}
		}

		int32 ReturnRow = 0;
		for (const FBlueprintNodeData& NodeData : GraphData.Nodes)
		{
			UK2Node* const* NodePtr = NodeMap.Find(NodeData.NodeId);
			if (!NodePtr || !*NodePtr)
			{
				continue;
			}

			UK2Node* Node = *NodePtr;
			if (NodeData.NodeId.Equals(GetFunctionEntryNodeId(), ESearchCase::IgnoreCase))
			{
				continue;
			}

			if (FMath::Abs(NodeData.Position.X) > KINDA_SMALL_NUMBER || FMath::Abs(NodeData.Position.Y) > KINDA_SMALL_NUMBER)
			{
				Node->NodePosX = static_cast<int32>(NodeData.Position.X);
				Node->NodePosY = static_cast<int32>(NodeData.Position.Y);
				continue;
			}

			Node->NodePosX = NextX;
			Node->NodePosY = LayoutConfig.StartY;
			NextX += LayoutConfig.StepX;

			if (NodeData.NodeType == EBlueprintNodeType::Return)
			{
				Node->NodePosY = LayoutConfig.StartY + LayoutConfig.StepY * ReturnRow;
				ReturnRow++;
			}
		}
	}

	UEdGraph* FindEventGraph(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		if (GraphName.IsEmpty())
		{
			return Blueprint->UbergraphPages.Num() > 0 ? Blueprint->UbergraphPages[0] : nullptr;
		}

		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				return Graph;
			}
		}

		return Blueprint->UbergraphPages.Num() > 0 ? Blueprint->UbergraphPages[0] : nullptr;
	}

	UK2Node_Event* FindTickEventNode(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				if (EventNode->EventReference.GetMemberName() == FName(*GetReceiveTickEventName()))
				{
					return EventNode;
				}
			}
		}

		return nullptr;
	}

	TArray<FString> CollectEventTickConsumers(const FBlueprintGraphData& GraphData)
	{
		TArray<FString> Result;
		for (const FBlueprintNodeData& Node : GraphData.Nodes)
		{
			for (const FBlueprintPinData& Pin : Node.Pins)
			{
				for (const FBlueprintPinConnection& Conn : Pin.Connections)
				{
					if (Conn.SourceNodeId.StartsWith(GetEventTickNodeIdPrefix(), ESearchCase::IgnoreCase))
					{
						if (!Result.Contains(Node.NodeId))
						{
							Result.Add(Node.NodeId);
						}
					}
				}
			}
		}

		return Result;
	}

	void EnsureTickNodesAppended(UBlueprint* Blueprint, const FBlueprintGraphData& GraphData, const TArray<FString>& TickConsumers, const TMap<FString, UK2Node*>& NodeMap)
	{
		if (!Blueprint || TickConsumers.Num() == 0)
		{
			return;
		}

		if (UEdGraph* EventGraph = FindEventGraph(Blueprint, GraphData.Name))
		{
			if (UK2Node_Event* TickNode = FindTickEventNode(EventGraph))
			{
				UEdGraphPin* TickExecPin = TickNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
				if (!TickExecPin)
				{
					TickExecPin = FindExecPin(TickNode, UEdGraphSchema_K2::PN_Then.ToString(), EGPD_Output);
				}
				if (!TickExecPin)
				{
					TickExecPin = FindExecPin(TickNode, TEXT(""), EGPD_Output);
				}

				if (!TickExecPin)
				{
					return;
				}

				TSet<UK2Node*> NewNodeSet;
				for (const FString& NodeId : TickConsumers)
				{
					if (UK2Node* const* NodePtr = NodeMap.Find(NodeId))
					{
						NewNodeSet.Add(*NodePtr);
					}
				}

				TArray<UEdGraphPin*> ConnectionsCopy = TickExecPin->LinkedTo;

				TArray<UEdGraphPin*> ExistingConnections;
				TMap<UK2Node*, UEdGraphPin*> EventNodeInputs;
				for (UEdGraphPin* ConnectedPin : ConnectionsCopy)
				{
					UK2Node* ConnectedNode = Cast<UK2Node>(ConnectedPin->GetOwningNode());
					TickExecPin->BreakLinkTo(ConnectedPin);

					if (ConnectedNode && NewNodeSet.Contains(ConnectedNode))
					{
						EventNodeInputs.Add(ConnectedNode, ConnectedPin);
					}
					else
					{
						ExistingConnections.Add(ConnectedPin);
					}
				}

				FGraphNodeCreator<UK2Node_ExecutionSequence> SequenceCreator(*EventGraph);
				UK2Node_ExecutionSequence* SequenceNode = SequenceCreator.CreateNode();
				SequenceNode->NodePosX = TickNode->NodePosX + 250;
				SequenceNode->NodePosY = TickNode->NodePosY;
				SequenceNode->AllocateDefaultPins();
				SequenceCreator.Finalize();

				UEdGraphPin* SequenceInput = FindExecPin(SequenceNode, TEXT(""), EGPD_Input);
				if (SequenceInput && TickExecPin)
				{
					TickExecPin->MakeLinkTo(SequenceInput);
				}

				UEdGraphPin* SequenceThen0 = FindExecPinByIndex(SequenceNode, EGPD_Output, 0, UEdGraphSchema_K2::PN_Then.ToString());
				UEdGraphPin* SequenceThen1 = FindExecPinByIndex(SequenceNode, EGPD_Output, 1);

				if (!SequenceThen0 || !SequenceThen1)
				{
					return;
				}

				for (UEdGraphPin* ExistingPin : ExistingConnections)
				{
					SequenceThen0->MakeLinkTo(ExistingPin);
				}

				TMap<FString, const FBlueprintNodeData*> NodeDataLookup;
				NodeDataLookup.Reserve(GraphData.Nodes.Num());
				for (const FBlueprintNodeData& Node : GraphData.Nodes)
				{
					if (!Node.NodeId.IsEmpty())
					{
						NodeDataLookup.Add(Node.NodeId, &Node);
					}
				}

				TSet<FString> PositionedDependencies;
				auto PositionDependenciesFrom = [&](const FString& RootId, const FVector2D& RootPos)
				{
					TArray<TPair<FString, FVector2D>> WorkQueue;
					WorkQueue.Emplace(RootId, RootPos);

					while (WorkQueue.Num() > 0)
					{
						TPair<FString, FVector2D> Pair = WorkQueue[0];
						WorkQueue.RemoveAt(0);

						if (const FBlueprintNodeData* const* NodeDataPtr = NodeDataLookup.Find(Pair.Key))
						{
							const FBlueprintNodeData* NodeData = *NodeDataPtr;
							int32 DependencyIndex = 0;

							for (const FBlueprintPinData& Pin : NodeData->Pins)
							{
								for (const FBlueprintPinConnection& Conn : Pin.Connections)
								{
									if (Conn.SourceNodeId.IsEmpty() || PositionedDependencies.Contains(Conn.SourceNodeId))
									{
										continue;
									}

									UK2Node* const* SourceNodePtr = NodeMap.Find(Conn.SourceNodeId);
									if (!SourceNodePtr || !*SourceNodePtr)
									{
										continue;
									}

									UK2Node* SourceNode = *SourceNodePtr;
									if (FindExecPin(SourceNode, TEXT(""), EGPD_MAX))
									{
										continue;
									}

									if (SourceNode->NodePosX == 0 && SourceNode->NodePosY == 0)
									{
										constexpr float DependencyOffsetX = -220.0f;
										constexpr float DependencyStepY = 80.0f;
										float PositionX = Pair.Value.X + DependencyOffsetX - DependencyIndex * 40.0f;
										float PositionY = Pair.Value.Y + ((DependencyIndex % 2 == 0) ? -DependencyStepY : DependencyStepY);
										SourceNode->NodePosX = static_cast<int32>(PositionX);
										SourceNode->NodePosY = static_cast<int32>(PositionY);
									}

									PositionedDependencies.Add(Conn.SourceNodeId);
									WorkQueue.Emplace(Conn.SourceNodeId, FVector2D(SourceNode->NodePosX, SourceNode->NodePosY));
									DependencyIndex++;
								}
							}
						}
					}
				};

				UEdGraphPin* PrevPin = SequenceThen1;
				int32 ConsumerIndex = 0;
				constexpr float ConsumerOffsetX = 240.0f;
				constexpr float ConsumerOffsetY = 120.0f;
				const float BaseX = SequenceNode->NodePosX + 200.0f;
				const float BaseY = SequenceNode->NodePosY;

				for (const FString& NodeId : TickConsumers)
				{
					if (UK2Node* const* NodePtr = NodeMap.Find(NodeId))
					{
						UK2Node* Node = *NodePtr;
						UEdGraphPin* ExecInput = nullptr;

						if (UEdGraphPin** ExecPinPtr = EventNodeInputs.Find(Node))
						{
							ExecInput = *ExecPinPtr;
						}

						if (!ExecInput)
						{
							ExecInput = FindExecPin(Node, TEXT(""), EGPD_Input);
						}

						if (PrevPin && ExecInput)
						{
							PrevPin->MakeLinkTo(ExecInput);
						}

						UEdGraphPin* ExecOutput = FindExecPin(Node, TEXT(""), EGPD_Output);
						if (!ExecOutput)
						{
							ExecOutput = FindNodePin(Node, UEdGraphSchema_K2::PN_Then.ToString(), EGPD_Output);
						}

						if (ExecOutput)
						{
							PrevPin = ExecOutput;

							if (Node->NodePosX == 0 && Node->NodePosY == 0)
							{
								Node->NodePosX = static_cast<int32>(BaseX + ConsumerIndex * ConsumerOffsetX);
								Node->NodePosY = static_cast<int32>(BaseY + ConsumerIndex * ConsumerOffsetY);
							}

							PositionDependenciesFrom(NodeId, FVector2D(Node->NodePosX, Node->NodePosY));
							ConsumerIndex++;
						}
					}
				}
			}
		}
	}
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
	TArray<FString>& OutWarnings,
	bool bMerge)
{
	if (!Blueprint)
	{
		return false;
	}

	// Use existing event graph or create new one
	UEdGraph* EventGraph = nullptr;

	const FString NormalizedGraphName = GraphData.Name.ToLower();
	const bool bUseUbergraph =
		GraphData.Name.IsEmpty() ||
		GraphData.Name.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase) ||
		GraphData.Name.Equals(TEXT("Ubergraph"), ESearchCase::IgnoreCase) ||
		(!NormalizedGraphName.IsEmpty() && NormalizedGraphName.Contains(TEXT("tick")));

	if (bUseUbergraph)
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

	// When not merging, clear existing non-event nodes and break event node connections
	// so we get a clean slate for the new BSL-compiled nodes.
	if (!bMerge)
	{
		TArray<UEdGraphNode*> NodesToRemove;
		for (UEdGraphNode* Node : EventGraph->Nodes)
		{
			if (Cast<UK2Node_Event>(Node))
			{
				// Keep event nodes (SpawnEventNode will reuse them),
				// but break all their connections so new ones can be applied cleanly
				for (UEdGraphPin* Pin : Node->Pins)
				{
					Pin->BreakAllPinLinks();
				}
			}
			else
			{
				// Remove all non-event nodes (call functions, variables, flow control, etc.)
				NodesToRemove.Add(Node);
			}
		}

		for (UEdGraphNode* Node : NodesToRemove)
		{
			EventGraph->RemoveNode(Node);
		}
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
			FEdGraphPinType PinType = ParseFunctionPinTypeFromText(Input.Type);

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
		OutNodeMap.Add(GetFunctionEntryNodeId(), EntryNode);
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
			FEdGraphPinType PinType = ParseFunctionPinTypeFromText(Output.Type);

			TSharedPtr<FUserPinInfo> PinInfo = MakeShareable(new FUserPinInfo());
			PinInfo->PinName = FName(*Output.Name);
			PinInfo->PinType = PinType;
			PinInfo->DesiredPinDirection = EGPD_Input;

			ResultNode->UserDefinedPins.Add(PinInfo);
		}

		ResultNode->ReconstructNode();

		// Add Result node to node map with special ID
		OutNodeMap.Add(GetFunctionResultNodeId(), ResultNode);
	}

	// Spawn additional nodes (skip helper entry/result node IDs as they're already created)
	for (const FBlueprintNodeData& NodeData : GraphData.Nodes)
	{
		// Skip special nodes that are already in the NodeMap
		if (NodeData.NodeId == GetFunctionEntryNodeId() || NodeData.NodeId == GetFunctionResultNodeId())
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
	struct FPinTypeDescriptor
	{
		FName Category;
		FName SubCategory;
		UObject* SubCategoryObject;
	};

	static const TMap<EBlueprintVarType, FPinTypeDescriptor> TypeDescriptors = {
		{ EBlueprintVarType::Boolean, { UEdGraphSchema_K2::PC_Boolean, NAME_None, nullptr } },
		{ EBlueprintVarType::Integer, { UEdGraphSchema_K2::PC_Int, NAME_None, nullptr } },
		{ EBlueprintVarType::Float, { UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Float, nullptr } },
		{ EBlueprintVarType::String, { UEdGraphSchema_K2::PC_String, NAME_None, nullptr } },
		{ EBlueprintVarType::Name, { UEdGraphSchema_K2::PC_Name, NAME_None, nullptr } },
		{ EBlueprintVarType::Text, { UEdGraphSchema_K2::PC_Text, NAME_None, nullptr } },
		{ EBlueprintVarType::Vector, { UEdGraphSchema_K2::PC_Struct, NAME_None, TBaseStructure<FVector>::Get() } },
		{ EBlueprintVarType::Rotator, { UEdGraphSchema_K2::PC_Struct, NAME_None, TBaseStructure<FRotator>::Get() } },
		{ EBlueprintVarType::Transform, { UEdGraphSchema_K2::PC_Struct, NAME_None, TBaseStructure<FTransform>::Get() } },
	};

	FEdGraphPinType PinType;
	if (const FPinTypeDescriptor* Descriptor = TypeDescriptors.Find(VarType))
	{
		PinType.PinCategory = Descriptor->Category;
		PinType.PinSubCategory = Descriptor->SubCategory;
		PinType.PinSubCategoryObject = Descriptor->SubCategoryObject;
		return PinType;
	}

	// Object/default fallback keeps previous behavior.
	PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	if (VarType == EBlueprintVarType::Object && !TypeClass.IsEmpty())
	{
		PinType.PinSubCategoryObject = UNodeSpawner::FindClassByPath(TypeClass);
	}
	return PinType;
}

FEdGraphPinType UAIBlueprintFactory::VarTypeToPinType(const FBlueprintVariableData& VarData)
{
	FEdGraphPinType PinType = GetPinType(VarData.Type, VarData.TypeClass);

	if (const EPinContainerType* ContainerType = GetContainerTypeMap().Find(VarData.Type))
	{
		PinType.ContainerType = *ContainerType;
		if (VarData.Type == EBlueprintVarType::Array)
		{
			FEdGraphPinType ElementType = GetPinType(VarData.ContainerElementType, TEXT(""));
			PinType.PinCategory = ElementType.PinCategory;
			PinType.PinSubCategory = ElementType.PinSubCategory;
			PinType.PinSubCategoryObject = ElementType.PinSubCategoryObject;
		}
	}

	return PinType;
}

UEdGraphPin* UAIBlueprintFactory::FindPinByName(UK2Node* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
	return FindNodePin(Node, PinName, Direction);
}

namespace
{
	int32 TryParseArgIndex(const FString& PinName)
	{
		if (!PinName.StartsWith(TEXT("Arg")))
		{
			return INDEX_NONE;
		}

		const FString IndexStr = PinName.Mid(3);
		return IndexStr.IsNumeric() ? FCString::Atoi(*IndexStr) : INDEX_NONE;
	}

	bool IsSkippablePositionalInputPin(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return true;
		}
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			return true;
		}
		return Pin->PinName == TEXT("self") || Pin->PinName == TEXT("WorldContextObject");
	}

	bool DirectionMatchesPin(const UEdGraphPin* Pin, EEdGraphPinDirection Direction)
	{
		return Pin && (Direction == EGPD_MAX || Pin->Direction == Direction);
	}

	bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	TArray<UEdGraphPin*> CollectExecPins(UK2Node* Node, EEdGraphPinDirection Direction)
	{
		TArray<UEdGraphPin*> ExecPins;
		if (!Node)
		{
			return ExecPins;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (DirectionMatchesPin(Pin, Direction) && IsExecPin(Pin))
			{
				ExecPins.Add(Pin);
			}
		}
		return ExecPins;
	}

	UEdGraphPin* FindNodePin(UK2Node* Node, const FString& PinName, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		// Exact match first (case-insensitive with alias support).
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!DirectionMatchesPin(Pin, Direction))
			{
				continue;
			}
			if (PinNamesEquivalent(Pin->PinName.ToString(), PinName))
			{
				return Pin;
			}
		}

		// Handle positional argument names like "Arg0", "Arg1", etc.
		const int32 ArgIndex = TryParseArgIndex(PinName);
		if (ArgIndex != INDEX_NONE)
		{
			int32 CurrentIndex = 0;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || IsSkippablePositionalInputPin(Pin))
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

		// Try partial match (sometimes pins have prefixes/suffixes).
		const FString RequestedLower = PinName.ToLower();
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!DirectionMatchesPin(Pin, Direction))
			{
				continue;
			}

			const FString CurrentPinLower = Pin->PinName.ToString().ToLower();
			if (CurrentPinLower.Contains(RequestedLower) || RequestedLower.Contains(CurrentPinLower))
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UEdGraphPin* FindExecPin(UK2Node* Node, const FString& PinName, EEdGraphPinDirection Direction)
	{
		for (UEdGraphPin* Pin : CollectExecPins(Node, Direction))
		{
			if (PinName.IsEmpty() || PinNamesEquivalent(Pin->PinName.ToString(), PinName))
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UEdGraphPin* FindExecPinByIndex(UK2Node* Node, EEdGraphPinDirection Direction, int32 Index, const FString& PreferredName)
	{
		if (!Node || Index < 0)
		{
			return nullptr;
		}

		TArray<UEdGraphPin*> ExecPins = CollectExecPins(Node, Direction);

		if (ExecPins.Num() == 0)
		{
			return nullptr;
		}

		if (!PreferredName.IsEmpty())
		{
			for (UEdGraphPin* Pin : ExecPins)
			{
				if (PinNamesEquivalent(Pin->PinName.ToString(), PreferredName))
				{
					return Pin;
				}
			}
		}

		return ExecPins.IsValidIndex(Index) ? ExecPins[Index] : nullptr;
	}
}
