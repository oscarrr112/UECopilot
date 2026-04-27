// Copyright ProjectRPG. All Rights Reserved.

#include "AssetFactorySubsystem.h"
#include "AssetFactoryModule.h"
#include "AssetGeneratorRegistry.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#if WITH_EDITORONLY_DATA
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "EdGraph/EdGraphPin.h"
#endif
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString NormalizeAssetObjectPathForDiagnostics(const FString& AssetPath)
	{
		FString NormalizedPath = AssetPath;
		if (!NormalizedPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPaths::GetBaseFilename(NormalizedPath);
			if (!AssetName.IsEmpty())
			{
				NormalizedPath += TEXT(".") + AssetName;
			}
		}

		return NormalizedPath;
	}

	FString ObjectPathForDiagnostics(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	FString SerializeJsonObjectForDiagnostics(const TSharedPtr<FJsonObject>& Object)
	{
		FString Result;
		if (Object.IsValid())
		{
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}
		return Result;
	}

	constexpr float DiagnosticNodePadding = 32.0f;
	constexpr float DiagnosticBaseNodeWidth = 220.0f;
	constexpr float DiagnosticBaseNodeHeight = 90.0f;
	constexpr float DiagnosticSubNodeHeight = 42.0f;

	struct FDiagnosticNodeBounds
	{
		const UBehaviorTreeGraphNode* Node = nullptr;
		float Left = 0.0f;
		float Top = 0.0f;
		float Right = 0.0f;
		float Bottom = 0.0f;
	};

	FString GetBTNodeDisplayNameForDiagnostics(const UBTNode* Node)
	{
		if (!Node)
		{
			return FString();
		}

		if (!Node->NodeName.IsEmpty())
		{
			return Node->NodeName;
		}

		UClass* NodeClass = Node->GetClass();
		return NodeClass ? NodeClass->GetName() : FString();
	}

	FDiagnosticNodeBounds EstimatePrimaryGraphNodeBoundsForDiagnostics(const UBehaviorTreeGraphNode* GraphNode)
	{
		FDiagnosticNodeBounds Bounds;
		Bounds.Node = GraphNode;
		if (!GraphNode)
		{
			return Bounds;
		}

		const UBTNode* NodeInstance = Cast<UBTNode>(GraphNode->NodeInstance);
		const FString DisplayName = GetBTNodeDisplayNameForDiagnostics(NodeInstance);
		const float TitleWidth = static_cast<float>(DisplayName.Len() * 8 + 80);
		const int32 SubNodeCount = GraphNode->Decorators.Num() + GraphNode->Services.Num();
		const float Width = FMath::Clamp(FMath::Max(DiagnosticBaseNodeWidth, TitleWidth), DiagnosticBaseNodeWidth, 520.0f);
		const float Height = FMath::Clamp(DiagnosticBaseNodeHeight + SubNodeCount * DiagnosticSubNodeHeight, DiagnosticBaseNodeHeight, 640.0f);

		Bounds.Left = static_cast<float>(GraphNode->NodePosX) - DiagnosticNodePadding * 0.5f;
		Bounds.Top = static_cast<float>(GraphNode->NodePosY) - DiagnosticNodePadding * 0.5f;
		Bounds.Right = static_cast<float>(GraphNode->NodePosX) + Width + DiagnosticNodePadding * 0.5f;
		Bounds.Bottom = static_cast<float>(GraphNode->NodePosY) + Height + DiagnosticNodePadding * 0.5f;
		return Bounds;
	}

	TSharedPtr<FJsonObject> MakeBoundsJson(const FDiagnosticNodeBounds& Bounds)
	{
		TSharedPtr<FJsonObject> BoundsJson = MakeShared<FJsonObject>();
		BoundsJson->SetNumberField(TEXT("left"), Bounds.Left);
		BoundsJson->SetNumberField(TEXT("top"), Bounds.Top);
		BoundsJson->SetNumberField(TEXT("right"), Bounds.Right);
		BoundsJson->SetNumberField(TEXT("bottom"), Bounds.Bottom);
		BoundsJson->SetNumberField(TEXT("width"), Bounds.Right - Bounds.Left);
		BoundsJson->SetNumberField(TEXT("height"), Bounds.Bottom - Bounds.Top);
		return BoundsJson;
	}

	bool DiagnosticBoundsOverlap(const FDiagnosticNodeBounds& A, const FDiagnosticNodeBounds& B)
	{
		if (!A.Node || !B.Node || A.Node == B.Node)
		{
			return false;
		}

		return !(A.Right <= B.Left ||
			B.Right <= A.Left ||
			A.Bottom <= B.Top ||
			B.Bottom <= A.Top);
	}

	TSharedPtr<FJsonObject> MakeSubGraphNodeJson(const UBehaviorTreeGraphNode* Node)
	{
		TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		const UObject* NodeInstance = Node ? Node->NodeInstance : nullptr;
		NodeJson->SetStringField(TEXT("name"), Node ? Node->GetName() : FString());
		NodeJson->SetStringField(TEXT("class"), Node && Node->GetClass() ? Node->GetClass()->GetName() : FString());
		NodeJson->SetStringField(TEXT("instance"), ObjectPathForDiagnostics(NodeInstance));
		NodeJson->SetStringField(TEXT("instanceClass"), NodeInstance && NodeInstance->GetClass() ? NodeInstance->GetClass()->GetName() : FString());
		return NodeJson;
	}

	TArray<TSharedPtr<FJsonValue>> MakeSubGraphNodeArrayJson(const TArray<TObjectPtr<UBehaviorTreeGraphNode>>& Nodes)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const UBehaviorTreeGraphNode* Node : Nodes)
		{
			Result.Add(MakeShared<FJsonValueObject>(MakeSubGraphNodeJson(Node)));
		}
		return Result;
	}

	TArray<FString> GetSubNodeInstancePaths(const TArray<TObjectPtr<UBehaviorTreeGraphNode>>& Nodes)
	{
		TArray<FString> Result;
		for (const UBehaviorTreeGraphNode* Node : Nodes)
		{
			Result.Add(ObjectPathForDiagnostics(Node ? Node->NodeInstance : nullptr));
		}
		return Result;
	}

	TArray<FString> GetObjectInstancePaths(const TArray<TObjectPtr<UBTService>>& Nodes)
	{
		TArray<FString> Result;
		for (const UBTService* Node : Nodes)
		{
			Result.Add(ObjectPathForDiagnostics(Node));
		}
		return Result;
	}

	TArray<FString> GetObjectInstancePaths(const TArray<TObjectPtr<UBTDecorator>>& Nodes)
	{
		TArray<FString> Result;
		for (const UBTDecorator* Node : Nodes)
		{
			Result.Add(ObjectPathForDiagnostics(Node));
		}
		return Result;
	}

	bool StringArraysMatch(const TArray<FString>& A, const TArray<FString>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index] != B[Index])
			{
				return false;
			}
		}

		return true;
	}

	TSharedPtr<FJsonObject> MakeStringArrayComparisonJson(const TArray<FString>& Expected, const TArray<FString>& Actual)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> ExpectedJson;
		for (const FString& Value : Expected)
		{
			ExpectedJson.Add(MakeShared<FJsonValueString>(Value));
		}
		Result->SetArrayField(TEXT("expected"), ExpectedJson);

		TArray<TSharedPtr<FJsonValue>> ActualJson;
		for (const FString& Value : Actual)
		{
			ActualJson.Add(MakeShared<FJsonValueString>(Value));
		}
		Result->SetArrayField(TEXT("actual"), ActualJson);
		return Result;
	}

	TArray<UBehaviorTreeGraphNode*> CollectOutputLinkedGraphNodes(const UEdGraphNode* Node)
	{
		TArray<UBehaviorTreeGraphNode*> Result;
		if (!Node)
		{
			return Result;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}

			TArray<UBehaviorTreeGraphNode*> LinkedNodes;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				UBehaviorTreeGraphNode* LinkedNode = LinkedPin ? Cast<UBehaviorTreeGraphNode>(LinkedPin->GetOwningNode()) : nullptr;
				if (LinkedNode)
				{
					LinkedNodes.Add(LinkedNode);
				}
			}

			LinkedNodes.Sort([](const UBehaviorTreeGraphNode& A, const UBehaviorTreeGraphNode& B)
			{
				if (A.NodePosX == B.NodePosX)
				{
					return A.NodePosY < B.NodePosY;
				}
				return A.NodePosX < B.NodePosX;
			});

			Result.Append(LinkedNodes);
		}

		return Result;
	}

	TArray<FString> GetGraphNodeInstancePaths(const TArray<UBehaviorTreeGraphNode*>& Nodes)
	{
		TArray<FString> Result;
		for (const UBehaviorTreeGraphNode* Node : Nodes)
		{
			Result.Add(ObjectPathForDiagnostics(Node ? Node->NodeInstance : nullptr));
		}
		return Result;
	}

	UBTNode* GetRuntimeChildNode(const FBTCompositeChild& ChildInfo)
	{
		return ChildInfo.ChildComposite ? static_cast<UBTNode*>(ChildInfo.ChildComposite.Get()) : static_cast<UBTNode*>(ChildInfo.ChildTask.Get());
	}

	void AddDiagnosticError(TArray<TSharedPtr<FJsonValue>>& Errors, const FString& Code, const FString& Message)
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);
		Errors.Add(MakeShared<FJsonValueObject>(Error));
	}

	void VerifyRuntimeNodeGraphLayout(
		UBTNode* RuntimeNode,
		const TMap<UBTNode*, UBehaviorTreeGraphNode*>& GraphNodesByInstance,
		TSet<UBehaviorTreeGraphNode*>& ReachablePrimaryGraphNodes,
		TArray<TSharedPtr<FJsonValue>>& Errors)
	{
		if (!RuntimeNode)
		{
			AddDiagnosticError(Errors, TEXT("NullRuntimeNode"), TEXT("Encountered a null runtime BehaviorTree node"));
			return;
		}

		UBehaviorTreeGraphNode* const* GraphNodePtr = GraphNodesByInstance.Find(RuntimeNode);
		UBehaviorTreeGraphNode* GraphNode = GraphNodePtr ? *GraphNodePtr : nullptr;
		if (!GraphNode)
		{
			AddDiagnosticError(
				Errors,
				TEXT("MissingGraphNode"),
				FString::Printf(TEXT("Runtime BT node '%s' does not have a graph node"), *RuntimeNode->GetPathName()));
			return;
		}

		ReachablePrimaryGraphNodes.Add(GraphNode);

		UBTCompositeNode* CompositeNode = Cast<UBTCompositeNode>(RuntimeNode);
		if (!CompositeNode)
		{
			return;
		}

		const TArray<FString> ExpectedServices = GetObjectInstancePaths(CompositeNode->Services);
		const TArray<FString> ActualServices = GetSubNodeInstancePaths(GraphNode->Services);
		if (!StringArraysMatch(ExpectedServices, ActualServices))
		{
			TSharedPtr<FJsonObject> Comparison = MakeStringArrayComparisonJson(ExpectedServices, ActualServices);
			AddDiagnosticError(
				Errors,
				TEXT("CompositeServicesMismatch"),
				FString::Printf(TEXT("Composite '%s' graph services do not match runtime services: %s"), *CompositeNode->GetPathName(), *SerializeJsonObjectForDiagnostics(Comparison)));
		}

		TArray<UBehaviorTreeGraphNode*> ExpectedChildGraphNodes;
		for (const FBTCompositeChild& ChildInfo : CompositeNode->Children)
		{
			UBTNode* ChildNode = GetRuntimeChildNode(ChildInfo);
			if (!ChildNode)
			{
				continue;
			}

			if (UBehaviorTreeGraphNode* const* ChildGraphNodePtr = GraphNodesByInstance.Find(ChildNode))
			{
				ExpectedChildGraphNodes.Add(*ChildGraphNodePtr);
			}
		}

		const TArray<UBehaviorTreeGraphNode*> ActualLinkedChildGraphNodes = CollectOutputLinkedGraphNodes(GraphNode);
		if (GetGraphNodeInstancePaths(ExpectedChildGraphNodes) != GetGraphNodeInstancePaths(ActualLinkedChildGraphNodes))
		{
			TSharedPtr<FJsonObject> Comparison = MakeStringArrayComparisonJson(
				GetGraphNodeInstancePaths(ExpectedChildGraphNodes),
				GetGraphNodeInstancePaths(ActualLinkedChildGraphNodes));
			AddDiagnosticError(
				Errors,
				TEXT("ChildEdgeMismatch"),
				FString::Printf(TEXT("Composite '%s' output graph links do not match runtime children: %s"), *CompositeNode->GetPathName(), *SerializeJsonObjectForDiagnostics(Comparison)));
		}

		int32 PreviousChildX = TNumericLimits<int32>::Min();
		for (int32 ChildIndex = 0; ChildIndex < CompositeNode->Children.Num(); ++ChildIndex)
		{
			const FBTCompositeChild& ChildInfo = CompositeNode->Children[ChildIndex];
			UBTNode* ChildNode = GetRuntimeChildNode(ChildInfo);
			if (!ChildNode)
			{
				AddDiagnosticError(
					Errors,
					TEXT("NullRuntimeChild"),
					FString::Printf(TEXT("Composite '%s' has a null child at index %d"), *CompositeNode->GetPathName(), ChildIndex));
				continue;
			}

			UBehaviorTreeGraphNode* const* ChildGraphNodePtr = GraphNodesByInstance.Find(ChildNode);
			UBehaviorTreeGraphNode* ChildGraphNode = ChildGraphNodePtr ? *ChildGraphNodePtr : nullptr;
			if (!ChildGraphNode)
			{
				AddDiagnosticError(
					Errors,
					TEXT("MissingGraphNode"),
					FString::Printf(TEXT("Runtime child node '%s' does not have a graph node"), *ChildNode->GetPathName()));
				continue;
			}

			if (ChildGraphNode->NodePosY <= GraphNode->NodePosY)
			{
				AddDiagnosticError(
					Errors,
					TEXT("ChildNotBelowParent"),
					FString::Printf(
						TEXT("Child '%s' Y=%d is not below parent '%s' Y=%d"),
						*ChildNode->GetPathName(),
						ChildGraphNode->NodePosY,
						*CompositeNode->GetPathName(),
						GraphNode->NodePosY));
			}

			if (ChildIndex > 0 && ChildGraphNode->NodePosX <= PreviousChildX)
			{
				AddDiagnosticError(
					Errors,
					TEXT("SiblingOrderMismatch"),
					FString::Printf(
						TEXT("Child '%s' X=%d is not to the right of previous sibling X=%d under '%s'"),
						*ChildNode->GetPathName(),
						ChildGraphNode->NodePosX,
						PreviousChildX,
						*CompositeNode->GetPathName()));
			}
			PreviousChildX = ChildGraphNode->NodePosX;

			const TArray<FString> ExpectedDecorators = GetObjectInstancePaths(ChildInfo.Decorators);
			const TArray<FString> ActualDecorators = GetSubNodeInstancePaths(ChildGraphNode->Decorators);
			if (!StringArraysMatch(ExpectedDecorators, ActualDecorators))
			{
				TSharedPtr<FJsonObject> Comparison = MakeStringArrayComparisonJson(ExpectedDecorators, ActualDecorators);
				AddDiagnosticError(
					Errors,
				TEXT("ChildDecoratorsMismatch"),
					FString::Printf(TEXT("Child '%s' graph decorators do not match runtime decorators: %s"), *ChildNode->GetPathName(), *SerializeJsonObjectForDiagnostics(Comparison)));
			}

			VerifyRuntimeNodeGraphLayout(ChildNode, GraphNodesByInstance, ReachablePrimaryGraphNodes, Errors);
		}
	}
}

void UAssetFactorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogAssetFactory, Log, TEXT("AssetFactorySubsystem initialized"));
}

void UAssetFactorySubsystem::Deinitialize()
{
	UE_LOG(LogAssetFactory, Log, TEXT("AssetFactorySubsystem deinitialized"));
	Super::Deinitialize();
}

FGenerationReport UAssetFactorySubsystem::GenerateFromFile(const FString& JsonFilePath)
{
	FGenerationReport Report;

	// Read file
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Failed to read file: %s"), *JsonFilePath);
		return Report;
	}

	UE_LOG(LogAssetFactory, Log, TEXT("Loaded JSON file: %s (%d bytes)"), *JsonFilePath, JsonString.Len());
	return GenerateFromString(JsonString);
}

FGenerationReport UAssetFactorySubsystem::GenerateFromString(const FString& JsonString)
{
	FGenerationReport Report;

	// Parse JSON
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Failed to parse JSON: %s"), *Reader->GetErrorMessage());
		return Report;
	}

	return GenerateFromJson(RootObject);
}

FGenerationReport UAssetFactorySubsystem::ValidateAllConfigs(TSharedPtr<FJsonObject> RootObject) const
{
	FGenerationReport Report;

	// Validate root structure
	const TArray<TSharedPtr<FJsonValue>>* AssetsArray;
	if (!RootObject.IsValid() || !RootObject->TryGetArrayField(TEXT("Assets"), AssetsArray))
	{
		Report.AddResult(FGenerationResult::MakeFailed(TEXT(""), TEXT(""), TEXT(""), TEXT("JSON missing 'Assets' array")));
		return Report;
	}

	for (int32 i = 0; i < AssetsArray->Num(); ++i)
	{
		TSharedPtr<FJsonObject> AssetObj = (*AssetsArray)[i]->AsObject();
		if (!AssetObj.IsValid())
		{
			Report.AddResult(FGenerationResult::MakeFailed(TEXT(""), TEXT(""), TEXT(""),
				FString::Printf(TEXT("Assets[%d]: not a valid JSON object"), i)));
			continue;
		}

		// Validate common fields
		FString AssetType;
		if (!AssetObj->TryGetStringField(TEXT("AssetType"), AssetType) || AssetType.IsEmpty())
		{
			Report.AddResult(FGenerationResult::MakeFailed(TEXT("Unknown"), TEXT(""), TEXT(""),
				FString::Printf(TEXT("Assets[%d]: missing 'AssetType'"), i)));
			continue;
		}

		FString Name;
		if (!AssetObj->TryGetStringField(TEXT("Name"), Name) || Name.IsEmpty())
		{
			Report.AddResult(FGenerationResult::MakeFailed(AssetType, TEXT(""), TEXT(""),
				FString::Printf(TEXT("Assets[%d]: missing 'Name'"), i)));
			continue;
		}

		FString Path;
		if (!AssetObj->TryGetStringField(TEXT("Path"), Path) || Path.IsEmpty())
		{
			Report.AddResult(FGenerationResult::MakeFailed(AssetType, Name, TEXT(""),
				FString::Printf(TEXT("Assets[%d]: missing 'Path'"), i)));
			continue;
		}

		// Find generator
		IAssetGenerator* Generator = FAssetGeneratorRegistry::Get().FindGenerator(AssetType);
		if (!Generator)
		{
			Report.AddResult(FGenerationResult::MakeFailed(AssetType, Name, Path,
				FString::Printf(TEXT("Assets[%d]: no generator for type '%s'"), i, *AssetType)));
			continue;
		}

		// Parse action for validation
		FString ActionStr;
		AssetObj->TryGetStringField(TEXT("Action"), ActionStr);
		EGenerationAction AssetAction = ParseAction(ActionStr);

		// Per-generator validation
		TOptional<FString> GenError = Generator->ValidateConfig(AssetObj, AssetAction);
		if (GenError.IsSet())
		{
			Report.AddResult(FGenerationResult::MakeFailed(AssetType, Name, Path,
				FString::Printf(TEXT("Assets[%d]: %s"), i, *GenError.GetValue())));
			continue;
		}

		// Passed validation — add a Skipped placeholder
		Report.AddResult(FGenerationResult::MakeSkipped(AssetType, Name, Path, TEXT("Validation passed")));
	}

	return Report;
}

FGenerationReport UAssetFactorySubsystem::GenerateFromJson(TSharedPtr<FJsonObject> RootObject)
{
	FGenerationReport Report;

	if (!RootObject.IsValid())
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Invalid JSON object"));
		return Report;
	}

	// Phase 1: Validate all configs upfront
	FGenerationReport ValidationReport = ValidateAllConfigs(RootObject);
	if (ValidationReport.HasFailures())
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Validation failed for %d of %d assets. No assets were generated."),
			ValidationReport.FailedCount, ValidationReport.TotalCount);

		for (const FGenerationResult& Result : ValidationReport.Results)
		{
			const TCHAR* StatusStr = (Result.Status == EGenerationStatus::Failed) ? TEXT("FAILED") : TEXT("SKIPPED");
			UE_LOG(LogAssetFactory, Log, TEXT("[%s] %s: %s - %s"),
				StatusStr, *Result.AssetType, *Result.GetFullPath(), *Result.Message);
		}

		return ValidationReport;
	}

	// Phase 2: Generate (only if all validation passed)
	// Get Assets array (already validated in Phase 1)
	const TArray<TSharedPtr<FJsonValue>>* AssetsArray;
	RootObject->TryGetArrayField(TEXT("Assets"), AssetsArray);

	// Convert to array of objects
	TArray<TSharedPtr<FJsonObject>> AssetConfigs;
	for (const TSharedPtr<FJsonValue>& Value : *AssetsArray)
	{
		TSharedPtr<FJsonObject> AssetObj = Value->AsObject();
		if (AssetObj.IsValid())
		{
			AssetConfigs.Add(AssetObj);
		}
	}

	UE_LOG(LogAssetFactory, Log, TEXT("Validation passed for %d assets, proceeding with generation"), AssetConfigs.Num());

	// Sort by priority
	SortByPriority(AssetConfigs);

	// Process each asset
	for (const TSharedPtr<FJsonObject>& Config : AssetConfigs)
	{
		FGenerationResult Result = ProcessAssetConfig(Config);
		Report.AddResult(Result);

		// Log result
		const TCHAR* StatusStr = TEXT("Unknown");
		switch (Result.Status)
		{
		case EGenerationStatus::Success: StatusStr = TEXT("SUCCESS"); break;
		case EGenerationStatus::Updated: StatusStr = TEXT("UPDATED"); break;
		case EGenerationStatus::Skipped: StatusStr = TEXT("SKIPPED"); break;
		case EGenerationStatus::Failed: StatusStr = TEXT("FAILED"); break;
		}

		UE_LOG(LogAssetFactory, Log, TEXT("[%s] %s: %s - %s"),
			StatusStr, *Result.AssetType, *Result.GetFullPath(), *Result.Message);
	}

	UE_LOG(LogAssetFactory, Log, TEXT("Generation complete: %s"), *Report.GetSummary());
	return Report;
}

FGenerationResult UAssetFactorySubsystem::ProcessAssetConfig(TSharedPtr<FJsonObject> AssetConfig)
{
	// Get common fields
	FString AssetType;
	if (!AssetConfig->TryGetStringField(TEXT("AssetType"), AssetType))
	{
		return FGenerationResult::MakeFailed(TEXT("Unknown"), TEXT(""), TEXT(""), TEXT("Missing 'AssetType' field"));
	}

	FString Name;
	if (!AssetConfig->TryGetStringField(TEXT("Name"), Name))
	{
		return FGenerationResult::MakeFailed(AssetType, TEXT(""), TEXT(""), TEXT("Missing 'Name' field"));
	}

	FString Path;
	if (!AssetConfig->TryGetStringField(TEXT("Path"), Path))
	{
		return FGenerationResult::MakeFailed(AssetType, Name, TEXT(""), TEXT("Missing 'Path' field"));
	}

	// Parse action
	FString ActionString;
	AssetConfig->TryGetStringField(TEXT("Action"), ActionString);
	EGenerationAction Action = ParseAction(ActionString);

	// Find generator
	IAssetGenerator* Generator = FAssetGeneratorRegistry::Get().FindGenerator(AssetType);
	if (!Generator)
	{
		return FGenerationResult::MakeFailed(AssetType, Name, Path,
			FString::Printf(TEXT("No generator found for asset type '%s'"), *AssetType));
	}

	// Check if asset exists and prompt user when creating
	if (Action == EGenerationAction::Create)
	{
		FString FullPath = Path / Name;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/") + FullPath;
		}

		if (FPackageName::DoesPackageExist(FullPath))
		{
			// Asset exists - ask user what to do
			const FText Title = FText::FromString(TEXT("Asset Already Exists"));
			const FText Message = FText::Format(
				FText::FromString(TEXT("{0} '{1}' already exists at '{2}'.\n\nDo you want to overwrite it?\n\n[Yes] = Overwrite\n[No] = Skip this asset")),
				FText::FromString(AssetType),
				FText::FromString(Name),
				FText::FromString(Path)
			);

			EAppReturnType::Type UserChoice = FMessageDialog::Open(EAppMsgType::YesNo, Message, Title);

			if (UserChoice != EAppReturnType::Yes)
			{
				// User chose to skip
				return FGenerationResult::MakeSkipped(AssetType, Name, Path, TEXT("User chose to skip existing asset"));
			}

			// User chose to overwrite - change action to CreateOrUpdate so generator will update existing asset
			Action = EGenerationAction::CreateOrUpdate;
		}
	}

	// Validate configuration before generation
	TOptional<FString> ValidationError = Generator->ValidateConfig(AssetConfig, Action);
	if (ValidationError.IsSet())
	{
		return FGenerationResult::MakeFailed(AssetType, Name, Path, ValidationError.GetValue());
	}

	// Generate asset
	return Generator->Generate(Name, Path, Action, AssetConfig);
}

EGenerationAction UAssetFactorySubsystem::ParseAction(const FString& ActionString) const
{
	if (ActionString.Equals(TEXT("Update"), ESearchCase::IgnoreCase))
	{
		return EGenerationAction::Update;
	}
	else if (ActionString.Equals(TEXT("CreateOrUpdate"), ESearchCase::IgnoreCase))
	{
		return EGenerationAction::CreateOrUpdate;
	}
	return EGenerationAction::Create;
}

void UAssetFactorySubsystem::SortByPriority(TArray<TSharedPtr<FJsonObject>>& AssetConfigs)
{
	AssetConfigs.Sort([this](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
	{
		FString TypeA, TypeB;
		A->TryGetStringField(TEXT("AssetType"), TypeA);
		B->TryGetStringField(TEXT("AssetType"), TypeB);

		return GetPriorityForAssetType(TypeA) < GetPriorityForAssetType(TypeB);
	});
}

int32 UAssetFactorySubsystem::GetPriorityForAssetType(const FString& AssetType) const
{
	IAssetGenerator* Generator = FAssetGeneratorRegistry::Get().FindGenerator(AssetType);
	return Generator ? Generator->GetPriority() : 999;
}

TArray<FString> UAssetFactorySubsystem::GetSupportedAssetTypes() const
{
	return FAssetGeneratorRegistry::Get().GetRegisteredTypes();
}

FString UAssetFactorySubsystem::GetBehaviorTreeGraphLayoutDiagnostics(const FString& AssetPath) const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Errors;
	TArray<TSharedPtr<FJsonValue>> NodesJson;

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("taskServiceScope"), TEXT("Task-level Services are not checked because the current BehaviorTree generator JSON contract disallows them."));

#if WITH_EDITORONLY_DATA
	const FString NormalizedAssetPath = NormalizeAssetObjectPathForDiagnostics(AssetPath);
	UBehaviorTree* BehaviorTree = LoadObject<UBehaviorTree>(nullptr, *NormalizedAssetPath);
	Result->SetBoolField(TEXT("assetLoaded"), BehaviorTree != nullptr);

	if (!BehaviorTree)
	{
		Result->SetStringField(TEXT("graphPath"), NormalizedAssetPath + TEXT(":Behavior Tree"));
		Result->SetBoolField(TEXT("hasGraph"), false);
		AddDiagnosticError(Errors, TEXT("AssetNotLoaded"), FString::Printf(TEXT("Failed to load BehaviorTree asset '%s'"), *NormalizedAssetPath));
		Result->SetArrayField(TEXT("nodes"), NodesJson);
		Result->SetArrayField(TEXT("errors"), Errors);
		Result->SetBoolField(TEXT("verified"), false);
		return SerializeJsonObjectForDiagnostics(Result);
	}

	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph);
	Result->SetStringField(TEXT("graphPath"), Graph ? Graph->GetPathName() : NormalizedAssetPath + TEXT(":Behavior Tree"));
	Result->SetBoolField(TEXT("hasGraph"), Graph != nullptr);
	Result->SetStringField(TEXT("blackboardPath"), ObjectPathForDiagnostics(BehaviorTree->BlackboardAsset.Get()));

	if (!Graph)
	{
		AddDiagnosticError(Errors, TEXT("GraphMissing"), FString::Printf(TEXT("BehaviorTree '%s' does not have a BTGraph"), *BehaviorTree->GetPathName()));
		Result->SetArrayField(TEXT("nodes"), NodesJson);
		Result->SetArrayField(TEXT("errors"), Errors);
		Result->SetBoolField(TEXT("verified"), false);
		return SerializeJsonObjectForDiagnostics(Result);
	}

	TArray<UBehaviorTreeGraphNode_Root*> RootGraphNodes;
	TArray<UBehaviorTreeGraphNode*> PrimaryGraphNodes;
	TArray<FDiagnosticNodeBounds> PrimaryGraphNodeBounds;
	TMap<UBTNode*, UBehaviorTreeGraphNode*> GraphNodesByInstance;
	TMap<FIntPoint, FString> PrimaryPositions;
	bool bAllPrimaryPositionsZero = true;

	for (UEdGraphNode* GraphNodeBase : Graph->Nodes)
	{
		UBehaviorTreeGraphNode* GraphNode = Cast<UBehaviorTreeGraphNode>(GraphNodeBase);
		UObject* NodeInstance = GraphNode ? GraphNode->NodeInstance : nullptr;

		TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		NodeJson->SetStringField(TEXT("name"), GraphNodeBase ? GraphNodeBase->GetName() : FString());
		NodeJson->SetStringField(TEXT("class"), GraphNodeBase && GraphNodeBase->GetClass() ? GraphNodeBase->GetClass()->GetName() : FString());
		NodeJson->SetStringField(TEXT("instance"), ObjectPathForDiagnostics(NodeInstance));
		NodeJson->SetStringField(TEXT("instanceClass"), NodeInstance && NodeInstance->GetClass() ? NodeInstance->GetClass()->GetName() : FString());
		NodeJson->SetNumberField(TEXT("x"), GraphNodeBase ? GraphNodeBase->NodePosX : 0);
		NodeJson->SetNumberField(TEXT("y"), GraphNodeBase ? GraphNodeBase->NodePosY : 0);
		NodeJson->SetArrayField(TEXT("decorators"), GraphNode ? MakeSubGraphNodeArrayJson(GraphNode->Decorators) : TArray<TSharedPtr<FJsonValue>>());
		NodeJson->SetArrayField(TEXT("services"), GraphNode ? MakeSubGraphNodeArrayJson(GraphNode->Services) : TArray<TSharedPtr<FJsonValue>>());

		if (!GraphNode)
		{
			NodesJson.Add(MakeShared<FJsonValueObject>(NodeJson));
			continue;
		}

		if (UBehaviorTreeGraphNode_Root* RootGraphNode = Cast<UBehaviorTreeGraphNode_Root>(GraphNode))
		{
			RootGraphNodes.Add(RootGraphNode);
			NodesJson.Add(MakeShared<FJsonValueObject>(NodeJson));
			continue;
		}

		UBTNode* BTNodeInstance = Cast<UBTNode>(NodeInstance);
		if (BTNodeInstance)
		{
			GraphNodesByInstance.Add(BTNodeInstance, GraphNode);
		}

		if (Cast<UBTCompositeNode>(NodeInstance) || Cast<UBTTaskNode>(NodeInstance))
		{
			PrimaryGraphNodes.Add(GraphNode);
			const FDiagnosticNodeBounds Bounds = EstimatePrimaryGraphNodeBoundsForDiagnostics(GraphNode);
			PrimaryGraphNodeBounds.Add(Bounds);
			NodeJson->SetObjectField(TEXT("estimatedBounds"), MakeBoundsJson(Bounds));
			bAllPrimaryPositionsZero = bAllPrimaryPositionsZero && GraphNode->NodePosX == 0 && GraphNode->NodePosY == 0;

			const FIntPoint Position(GraphNode->NodePosX, GraphNode->NodePosY);
			if (const FString* ExistingNodeName = PrimaryPositions.Find(Position))
			{
				AddDiagnosticError(
					Errors,
					TEXT("PrimaryNodePositionOverlap"),
					FString::Printf(TEXT("Primary nodes '%s' and '%s' share position (%d, %d)"), **ExistingNodeName, *GraphNode->GetName(), Position.X, Position.Y));
			}
			else
			{
				PrimaryPositions.Add(Position, GraphNode->GetName());
			}
		}

		NodesJson.Add(MakeShared<FJsonValueObject>(NodeJson));
	}

	Result->SetArrayField(TEXT("nodes"), NodesJson);

	for (int32 Index = 0; Index < PrimaryGraphNodeBounds.Num(); ++Index)
	{
		for (int32 OtherIndex = Index + 1; OtherIndex < PrimaryGraphNodeBounds.Num(); ++OtherIndex)
		{
			const FDiagnosticNodeBounds& A = PrimaryGraphNodeBounds[Index];
			const FDiagnosticNodeBounds& B = PrimaryGraphNodeBounds[OtherIndex];
			if (!DiagnosticBoundsOverlap(A, B))
			{
				continue;
			}

			TSharedPtr<FJsonObject> OverlapJson = MakeShared<FJsonObject>();
			OverlapJson->SetStringField(TEXT("a"), A.Node ? A.Node->GetName() : FString());
			OverlapJson->SetStringField(TEXT("b"), B.Node ? B.Node->GetName() : FString());
			OverlapJson->SetObjectField(TEXT("aBounds"), MakeBoundsJson(A));
			OverlapJson->SetObjectField(TEXT("bBounds"), MakeBoundsJson(B));
			AddDiagnosticError(
				Errors,
				TEXT("PrimaryNodeBoundsOverlap"),
				FString::Printf(TEXT("Primary graph node estimated bounds overlap: %s"), *SerializeJsonObjectForDiagnostics(OverlapJson)));
		}
	}

	if (RootGraphNodes.Num() != 1)
	{
		AddDiagnosticError(Errors, TEXT("RootGraphNodeCount"), FString::Printf(TEXT("Expected exactly one root graph node, found %d"), RootGraphNodes.Num()));
	}
	else
	{
		UBehaviorTreeGraphNode_Root* RootGraphNode = RootGraphNodes[0];
		Result->SetStringField(TEXT("rootGraphBlackboardPath"), ObjectPathForDiagnostics(RootGraphNode->BlackboardAsset.Get()));
		Result->SetBoolField(TEXT("blackboardPreserved"), RootGraphNode->BlackboardAsset == BehaviorTree->BlackboardAsset);

		if (RootGraphNode->BlackboardAsset != BehaviorTree->BlackboardAsset)
		{
			AddDiagnosticError(
				Errors,
				TEXT("BlackboardMismatch"),
				FString::Printf(
					TEXT("Root graph blackboard '%s' does not match BehaviorTree blackboard '%s'"),
					*ObjectPathForDiagnostics(RootGraphNode->BlackboardAsset.Get()),
					*ObjectPathForDiagnostics(BehaviorTree->BlackboardAsset.Get())));
		}
	}

	if (PrimaryGraphNodes.Num() < 2)
	{
		AddDiagnosticError(Errors, TEXT("PrimaryGraphNodeCount"), FString::Printf(TEXT("Expected at least two primary behavior graph nodes, found %d"), PrimaryGraphNodes.Num()));
	}

	if (PrimaryGraphNodes.Num() > 0 && bAllPrimaryPositionsZero)
	{
		AddDiagnosticError(Errors, TEXT("PrimaryPositionsAllZero"), TEXT("All primary behavior graph nodes are at the default (0, 0) position"));
	}

	if (RootGraphNodes.Num() == 1)
	{
		UBehaviorTreeGraphNode_Root* RootGraphNode = RootGraphNodes[0];
		for (const UBehaviorTreeGraphNode* PrimaryGraphNode : PrimaryGraphNodes)
		{
			if (PrimaryGraphNode && PrimaryGraphNode->NodePosY <= RootGraphNode->NodePosY)
			{
				AddDiagnosticError(
					Errors,
					TEXT("PrimaryNotBelowRoot"),
					FString::Printf(TEXT("Primary graph node '%s' Y=%d is not below root graph node Y=%d"), *PrimaryGraphNode->GetName(), PrimaryGraphNode->NodePosY, RootGraphNode->NodePosY));
			}
		}
	}

	if (BehaviorTree->RootNode)
	{
		TSet<UBehaviorTreeGraphNode*> ReachablePrimaryGraphNodes;
		if (RootGraphNodes.Num() == 1)
		{
			UBehaviorTreeGraphNode* const* RuntimeRootGraphNodePtr = GraphNodesByInstance.Find(BehaviorTree->RootNode);
			UBehaviorTreeGraphNode* RuntimeRootGraphNode = RuntimeRootGraphNodePtr ? *RuntimeRootGraphNodePtr : nullptr;
			const TArray<UBehaviorTreeGraphNode*> RootLinkedNodes = CollectOutputLinkedGraphNodes(RootGraphNodes[0]);
			if (RootLinkedNodes.Num() != 1 || RootLinkedNodes[0] != RuntimeRootGraphNode)
			{
				TArray<FString> ExpectedRootLinks;
				if (RuntimeRootGraphNode)
				{
					ExpectedRootLinks.Add(ObjectPathForDiagnostics(RuntimeRootGraphNode->NodeInstance));
				}

				TSharedPtr<FJsonObject> Comparison = MakeStringArrayComparisonJson(
					ExpectedRootLinks,
					GetGraphNodeInstancePaths(RootLinkedNodes));
				AddDiagnosticError(
					Errors,
					TEXT("RootGraphEdgeMissing"),
					FString::Printf(TEXT("Root graph output link does not target runtime root node: %s"), *SerializeJsonObjectForDiagnostics(Comparison)));
			}
		}

		VerifyRuntimeNodeGraphLayout(BehaviorTree->RootNode, GraphNodesByInstance, ReachablePrimaryGraphNodes, Errors);
		for (UBehaviorTreeGraphNode* PrimaryGraphNode : PrimaryGraphNodes)
		{
			if (!PrimaryGraphNode || ReachablePrimaryGraphNodes.Contains(PrimaryGraphNode))
			{
				continue;
			}

			AddDiagnosticError(
				Errors,
				TEXT("PrimaryGraphNodeUnreachable"),
				FString::Printf(
					TEXT("Primary graph node '%s' with runtime instance '%s' is not reachable from the BehaviorTree root runtime node"),
					*PrimaryGraphNode->GetName(),
					*ObjectPathForDiagnostics(PrimaryGraphNode->NodeInstance)));
		}
	}
	else
	{
		AddDiagnosticError(Errors, TEXT("RootNodeMissing"), FString::Printf(TEXT("BehaviorTree '%s' has no runtime root node"), *BehaviorTree->GetPathName()));
	}
#else
	Result->SetBoolField(TEXT("assetLoaded"), false);
	Result->SetStringField(TEXT("graphPath"), FString());
	Result->SetBoolField(TEXT("hasGraph"), false);
	Result->SetArrayField(TEXT("nodes"), NodesJson);
	AddDiagnosticError(Errors, TEXT("EditorOnlyDataUnavailable"), TEXT("BehaviorTree graph diagnostics require editor-only data"));
#endif

	Result->SetArrayField(TEXT("errors"), Errors);
	Result->SetBoolField(TEXT("verified"), Errors.Num() == 0);
	return SerializeJsonObjectForDiagnostics(Result);
}

TSharedPtr<FJsonObject> UAssetFactorySubsystem::ExtractAsset(const FString& AssetPath, bool bDiffOnly)
{
	// Try multiple ways to find/load the asset
	UObject* Asset = nullptr;
	FString AssetName = FPaths::GetBaseFilename(AssetPath);
	FString FullPathWithSuffix = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);

	// First, try with the full object path (e.g., /Game/Test/MyAsset.MyAsset)
	// This is the correct way to reference the actual asset object, not the package
	Asset = StaticFindObject(UObject::StaticClass(), nullptr, *FullPathWithSuffix);

	// If not found in memory, try LoadObject with suffix
	if (!Asset)
	{
		Asset = LoadObject<UObject>(nullptr, *FullPathWithSuffix);
	}

	// Fallback: try without suffix (might work for some asset types)
	if (!Asset)
	{
		Asset = StaticFindObject(UObject::StaticClass(), nullptr, *AssetPath);
		// If we got a package, try to find the actual asset inside it
		if (Asset && Asset->IsA<UPackage>())
		{
			UPackage* Package = Cast<UPackage>(Asset);
			Asset = StaticFindObject(UObject::StaticClass(), Package, *AssetName);
		}
	}

	// Last resort: LoadObject without suffix
	if (!Asset)
	{
		Asset = LoadObject<UObject>(nullptr, *AssetPath);
	}

	if (!Asset)
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Failed to load asset: %s"), *AssetPath);
		return nullptr;
	}

	// Find a generator that can extract this asset
	TArray<TSharedRef<IAssetGenerator>> Generators = FAssetGeneratorRegistry::Get().GetGeneratorsSortedByPriority();
	for (const TSharedRef<IAssetGenerator>& Generator : Generators)
	{
		if (Generator->CanExtract(Asset))
		{
			TSharedPtr<FJsonObject> Config = Generator->Extract(Asset, bDiffOnly);
			if (Config.IsValid())
			{
				// Add asset metadata
				Config->SetStringField(TEXT("AssetType"), Generator->GetAssetType());

				// Extract Name and Path from the asset path (AssetName already computed above)
				FString AssetDir = FPaths::GetPath(AssetPath);
				Config->SetStringField(TEXT("Name"), AssetName);
				Config->SetStringField(TEXT("Path"), AssetDir);

				UE_LOG(LogAssetFactory, Log, TEXT("Extracted asset: %s (type: %s)"), *AssetPath, *Generator->GetAssetType());
				return Config;
			}
		}
	}

	UE_LOG(LogAssetFactory, Warning, TEXT("No generator can extract asset: %s (class: %s)"),
		*AssetPath, *Asset->GetClass()->GetName());
	return nullptr;
}
