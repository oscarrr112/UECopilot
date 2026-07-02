// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentService.h"
#include "AssetDocumentJsonRegionUtils.h"
#include "Profiles/BehaviorTreeAssetDocumentProfile.h"
#include "Regions/AssetDocumentReflectedPropertyUtils.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Services/BTService_DefaultFocus.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "BehaviorTree/Tasks/BTTask_SetKeyValue.h"
#include "BehaviorTree/Tasks/BTTask_WaitBlackboardTime.h"
#include "Animation/NodeMappingContainer.h"
#include "BlueprintEditorSettings.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Sections/MovieSceneCVarSection.h"
#include "TestActorBase.h"
#include "TestDataAsset.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
const FAssetDocumentRegionPolicy* FindPolicyByRegionId(const TArray<FAssetDocumentRegionPolicy>& Policies, FName RegionId)
{
	return Policies.FindByPredicate([RegionId](const FAssetDocumentRegionPolicy& Policy)
	{
		return Policy.RegionId == RegionId;
	});
}

TSharedRef<FJsonObject> MakeObject()
{
	return MakeShared<FJsonObject>();
}

TSharedPtr<FJsonObject> MakeAssetRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
	Ref->SetStringField(TEXT("Kind"), TEXT("AssetRef"));
	Ref->SetStringField(TEXT("Path"), Path);
	return Ref;
}

TSharedPtr<FJsonValue> MakeObjectValue(TSharedPtr<FJsonObject> Object)
{
	return MakeShared<FJsonValueObject>(Object.ToSharedRef());
}

FString MakeObjectPathFromTarget(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

UBlackboardData* MakeExistingBlackboardAsset(const FString& Target)
{
	UPackage* Package = CreatePackage(*Target);
	return NewObject<UBlackboardData>(
		Package,
		*FPackageName::GetLongPackageAssetName(Target),
		RF_Public | RF_Standalone | RF_Transactional);
}

UBehaviorTree* MakeExistingBehaviorTreeAsset(const FString& Target)
{
	UPackage* Package = CreatePackage(*Target);
	return NewObject<UBehaviorTree>(
		Package,
		*FPackageName::GetLongPackageAssetName(Target),
		RF_Public | RF_Standalone | RF_Transactional);
}

UBehaviorTree* LoadBehaviorTreeForTarget(const FString& Target)
{
	return LoadObject<UBehaviorTree>(nullptr, *MakeObjectPathFromTarget(Target));
}

FAssetDocumentApplyRequest MakeApplyRequest(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentApplyRequest Request;
	Request.Document = Document;
	Request.bSaveAsset = false;
	return Request;
}

FAssetDocumentDiffRequest MakeDiffRequest(TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentDiffRequest Request;
	Request.Document = Document;
	return Request;
}

bool ResultHasDiagnostic(const FAssetDocumentResult& Result, const FString& ExpectedCode, const FString& ExpectedPath)
{
	return Result.Diagnostics.ContainsByPredicate([&ExpectedCode, &ExpectedPath](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == ExpectedCode && Diagnostic.Path == ExpectedPath;
	});
}

bool DiffPayloadHasEntry(const TSharedPtr<FJsonObject>& Payload, const FString& BucketName, const FString& ExpectedPath)
{
	if (!Payload.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Payload->TryGetArrayField(BucketName, Entries) || !Entries)
	{
		return false;
	}

	return Entries->ContainsByPredicate([&ExpectedPath](const TSharedPtr<FJsonValue>& EntryValue)
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
		FString Path;
		return Entry.IsValid() && Entry->TryGetStringField(TEXT("path"), Path) && Path == ExpectedPath;
	});
}

bool DiffPayloadHasAnyEntry(const TSharedPtr<FJsonObject>& Payload, const FString& ExpectedPath)
{
	return DiffPayloadHasEntry(Payload, TEXT("changed"), ExpectedPath)
		|| DiffPayloadHasEntry(Payload, TEXT("added"), ExpectedPath)
		|| DiffPayloadHasEntry(Payload, TEXT("removed"), ExpectedPath)
		|| DiffPayloadHasEntry(Payload, TEXT("unchanged"), ExpectedPath);
}

void SwapArrayEntries(TSharedPtr<FJsonObject> Object, const FString& FieldName, int32 FirstIndex, int32 SecondIndex)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values || !Values->IsValidIndex(FirstIndex) || !Values->IsValidIndex(SecondIndex))
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Swapped = *Values;
	Swapped.Swap(FirstIndex, SecondIndex);
	Object->SetArrayField(FieldName, Swapped);
}

TSharedPtr<FJsonObject> MakeBehaviorTreeBody(
	TSharedPtr<FJsonObject> Blackboard,
	TSharedPtr<FJsonObject> Tree = nullptr,
	TSharedPtr<FJsonObject> EditorLayout = nullptr)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	if (Blackboard.IsValid())
	{
		Body->SetObjectField(TEXT("Blackboard"), Blackboard);
	}
	else
	{
		Body->SetField(TEXT("Blackboard"), MakeShared<FJsonValueNull>());
	}

	if (Tree.IsValid())
	{
		Body->SetObjectField(TEXT("Tree"), Tree);
	}
	if (EditorLayout.IsValid())
	{
		Body->SetObjectField(TEXT("EditorLayout"), EditorLayout);
	}
	return Body;
}

TSharedPtr<FJsonObject> MakeEmptyBehaviorTree()
{
	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetObjectField(TEXT("Root"), MakeShared<FJsonObject>());
	return Tree;
}

TSharedPtr<FJsonObject> MakeMinimalSelectorBehaviorTree()
{
	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetArrayField(TEXT("RootDecorators"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("Id"), TEXT("Root"));
	Root->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTComposite_Selector"));
	Root->SetArrayField(TEXT("Services"), TArray<TSharedPtr<FJsonValue>>());
	Root->SetArrayField(TEXT("Children"), TArray<TSharedPtr<FJsonValue>>());
	Tree->SetObjectField(TEXT("Root"), Root);
	return Tree;
}

TSharedPtr<FJsonObject> MakeTreeWithKeySelectorLikeProperty()
{
	TSharedPtr<FJsonObject> Selector = MakeShared<FJsonObject>();
	Selector->SetStringField(TEXT("Key"), TEXT("TargetActor"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("BlackboardKey"), Selector);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("Id"), TEXT("RootWithKey"));
	Root->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTTask_WaitBlackboardTime"));
	Root->SetObjectField(TEXT("Properties"), Properties);

	TSharedPtr<FJsonObject> Tree = MakeMinimalSelectorBehaviorTree();
	Tree->SetObjectField(TEXT("Root"), Root);
	return Tree;
}

TSharedPtr<FJsonObject> MakeDecoratorLogicOp(const FString& Operation, int32 Number)
{
	TSharedPtr<FJsonObject> Logic = MakeShared<FJsonObject>();
	Logic->SetStringField(TEXT("Operation"), Operation);
	Logic->SetNumberField(TEXT("Number"), Number);
	return Logic;
}

TSharedPtr<FJsonObject> MakeSelectorProperty(const FString& KeyName)
{
	TSharedPtr<FJsonObject> Selector = MakeShared<FJsonObject>();
	Selector->SetStringField(TEXT("SelectedKeyName"), KeyName);
	Selector->SetBoolField(TEXT("bNoneIsAllowedValue"), false);
	return Selector;
}

TSharedPtr<FJsonObject> MakeBtNode(
	const FString& Id,
	const FString& Class,
	TSharedPtr<FJsonObject> Properties = nullptr)
{
	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("Id"), Id);
	Node->SetStringField(TEXT("Class"), Class);
	if (Properties.IsValid())
	{
		Node->SetObjectField(TEXT("Properties"), Properties);
	}
	return Node;
}

TSharedPtr<FJsonObject> MakeDecoratorLogicTest(int32 Number = 0)
{
	TSharedPtr<FJsonObject> Logic = MakeShared<FJsonObject>();
	Logic->SetStringField(TEXT("Operation"), TEXT("Test"));
	Logic->SetNumberField(TEXT("Number"), Number);
	return Logic;
}

TSharedPtr<FJsonObject> MakeTask7BehaviorTree(
	const FString& SubtreeTarget,
	bool bIncludeRootDecorator,
	bool bUseShortClassNames)
{
	const FString SelectorClass = bUseShortClassNames ? TEXT("BTComposite_Selector") : TEXT("/Script/AIModule.BTComposite_Selector");
	const FString ServiceClass = bUseShortClassNames ? TEXT("BTService_DefaultFocus") : TEXT("/Script/AIModule.BTService_DefaultFocus");
	const FString DecoratorClass = bUseShortClassNames ? TEXT("BTDecorator_Blackboard") : TEXT("/Script/AIModule.BTDecorator_Blackboard");
	const FString MoveToClass = bUseShortClassNames ? TEXT("BTTask_MoveTo") : TEXT("/Script/AIModule.BTTask_MoveTo");
	const FString RunBehaviorClass = bUseShortClassNames ? TEXT("BTTask_RunBehavior") : TEXT("/Script/AIModule.BTTask_RunBehavior");

	TSharedPtr<FJsonObject> ServiceProperties = MakeShared<FJsonObject>();
	ServiceProperties->SetNumberField(TEXT("Interval"), 1.25);
	TArray<TSharedPtr<FJsonValue>> Services;
	Services.Add(MakeObjectValue(MakeBtNode(TEXT("FocusService"), ServiceClass, ServiceProperties)));

	TSharedPtr<FJsonObject> DecoratorProperties = MakeShared<FJsonObject>();
	DecoratorProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("TargetActor")));
	TArray<TSharedPtr<FJsonValue>> EdgeDecorators;
	EdgeDecorators.Add(MakeObjectValue(MakeBtNode(TEXT("HasTarget"), DecoratorClass, DecoratorProperties)));

	TArray<TSharedPtr<FJsonValue>> EdgeLogic;
	EdgeLogic.Add(MakeObjectValue(MakeDecoratorLogicTest()));

	TSharedPtr<FJsonObject> MoveToProperties = MakeShared<FJsonObject>();
	MoveToProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("TargetActor")));
	TSharedPtr<FJsonObject> MoveToNode = MakeBtNode(TEXT("MoveToTarget"), MoveToClass, MoveToProperties);

	TSharedPtr<FJsonObject> MoveEdge = MakeShared<FJsonObject>();
	MoveEdge->SetObjectField(TEXT("Child"), MoveToNode);
	MoveEdge->SetArrayField(TEXT("Decorators"), EdgeDecorators);
	MoveEdge->SetArrayField(TEXT("DecoratorLogic"), EdgeLogic);

	TSharedPtr<FJsonObject> RunSubtreeProperties = MakeShared<FJsonObject>();
	RunSubtreeProperties->SetObjectField(TEXT("BehaviorAsset"), MakeAssetRef(MakeObjectPathFromTarget(SubtreeTarget)));
	TSharedPtr<FJsonObject> RunSubtreeNode = MakeBtNode(TEXT("RunSubtree"), RunBehaviorClass, RunSubtreeProperties);

	TSharedPtr<FJsonObject> RunSubtreeEdge = MakeShared<FJsonObject>();
	RunSubtreeEdge->SetObjectField(TEXT("Child"), RunSubtreeNode);

	TArray<TSharedPtr<FJsonValue>> Children;
	Children.Add(MakeObjectValue(MoveEdge));
	Children.Add(MakeObjectValue(RunSubtreeEdge));

	TSharedPtr<FJsonObject> Root = MakeBtNode(TEXT("RootSelector"), SelectorClass);
	Root->SetArrayField(TEXT("Services"), Services);
	Root->SetArrayField(TEXT("Children"), Children);

	TSharedPtr<FJsonObject> Tree = MakeShared<FJsonObject>();
	Tree->SetObjectField(TEXT("Root"), Root);

	TArray<TSharedPtr<FJsonValue>> RootDecorators;
	if (bIncludeRootDecorator)
	{
		TSharedPtr<FJsonObject> RootDecoratorProperties = MakeShared<FJsonObject>();
		RootDecoratorProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("TargetActor")));
		RootDecorators.Add(MakeObjectValue(MakeBtNode(TEXT("RootHasTarget"), DecoratorClass, RootDecoratorProperties)));
	}
	Tree->SetArrayField(TEXT("RootDecorators"), RootDecorators);

	TArray<TSharedPtr<FJsonValue>> RootLogic;
	if (bIncludeRootDecorator)
	{
		RootLogic.Add(MakeObjectValue(MakeDecoratorLogicTest()));
	}
	Tree->SetArrayField(TEXT("RootDecoratorLogic"), RootLogic);
	return Tree;
}

TSharedPtr<FJsonObject> MakeBehaviorTreeDocument(const FString& Target, TSharedPtr<FJsonObject> Body)
{
	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BehaviorTree"));
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	Document->SetObjectField(TEXT("Body"), Body);
	return Document;
}

TSharedPtr<FJsonObject> MakeClassRef(const FString& Path)
{
	TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
	Ref->SetStringField(TEXT("Kind"), TEXT("ClassRef"));
	Ref->SetStringField(TEXT("Path"), Path);
	return Ref;
}

bool HasDiagnostic(const FAssetDocumentCapabilityResult& Result, const FString& Path, const FString& Code)
{
	return Result.Diagnostics.ContainsByPredicate([&Path, &Code](const FAssetDocumentDiagnostic& Diagnostic)
	{
		return Diagnostic.Path == Path && Diagnostic.Code == Code;
	});
}

TSharedPtr<FJsonObject> GetObjectField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
{
	const TSharedPtr<FJsonObject>* FieldObject = nullptr;
	return Object.IsValid() && Object->TryGetObjectField(FieldName, FieldObject) && FieldObject ? *FieldObject : nullptr;
}

TSharedPtr<FJsonObject> GetObjectFromValue(const TSharedPtr<FJsonValue>& Value)
{
	const TSharedPtr<FJsonObject>* Object = nullptr;
	return Value.IsValid() && Value->TryGetObject(Object) && Object ? *Object : nullptr;
}

void AddSecondDecoratorSet(TSharedPtr<FJsonObject> Tree)
{
	TSharedPtr<FJsonObject> OtherDecoratorProperties = MakeShared<FJsonObject>();
	OtherDecoratorProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("OtherTargetActor")));
	TSharedPtr<FJsonObject> OtherDecorator = MakeBtNode(TEXT("HasOtherTarget"), TEXT("/Script/AIModule.BTDecorator_Blackboard"), OtherDecoratorProperties);

	TSharedPtr<FJsonObject> Root = GetObjectField(Tree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Root.IsValid() && Root->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstEdge = GetObjectFromValue((*Children)[0]);
		const TArray<TSharedPtr<FJsonValue>>* EdgeDecorators = nullptr;
		TArray<TSharedPtr<FJsonValue>> UpdatedDecorators;
		if (FirstEdge.IsValid() && FirstEdge->TryGetArrayField(TEXT("Decorators"), EdgeDecorators) && EdgeDecorators)
		{
			UpdatedDecorators = *EdgeDecorators;
		}
		UpdatedDecorators.Add(MakeObjectValue(OtherDecorator));
		FirstEdge->SetArrayField(TEXT("Decorators"), UpdatedDecorators);
	}

	TSharedPtr<FJsonObject> OtherRootDecoratorProperties = MakeShared<FJsonObject>();
	OtherRootDecoratorProperties->SetObjectField(TEXT("BlackboardKey"), MakeSelectorProperty(TEXT("OtherTargetActor")));
	TSharedPtr<FJsonObject> OtherRootDecorator = MakeBtNode(TEXT("RootHasOtherTarget"), TEXT("/Script/AIModule.BTDecorator_Blackboard"), OtherRootDecoratorProperties);
	const TArray<TSharedPtr<FJsonValue>>* RootDecorators = nullptr;
	TArray<TSharedPtr<FJsonValue>> UpdatedRootDecorators;
	if (Tree.IsValid() && Tree->TryGetArrayField(TEXT("RootDecorators"), RootDecorators) && RootDecorators)
	{
		UpdatedRootDecorators = *RootDecorators;
	}
	UpdatedRootDecorators.Add(MakeObjectValue(OtherRootDecorator));
	Tree->SetArrayField(TEXT("RootDecorators"), UpdatedRootDecorators);
}

bool ApplyTask7TreeFixture(
	FAutomationTestBase& Test,
	FAssetDocumentService& Service,
	const FString& Target,
	bool bIncludeRootDecorator = true,
	bool bUseShortClassNames = false)
{
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	UBlackboardData* Blackboard = MakeExistingBlackboardAsset(BlackboardTarget);
	Test.TestNotNull(TEXT("Task7 fixture blackboard exists"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}

	UBehaviorTree* Subtree = MakeExistingBehaviorTreeAsset(SubtreeTarget);
	Test.TestNotNull(TEXT("Task7 fixture subtree exists"), Subtree);
	if (!Subtree)
	{
		return false;
	}

	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		MakeTask7BehaviorTree(SubtreeTarget, bIncludeRootDecorator, bUseShortClassNames),
		MakeShared<FJsonObject>());
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, Body)));
	Test.TestTrue(TEXT("Task7 fixture apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		Test.AddError(ApplyResult.Message);
	}
	return ApplyResult.IsSuccess();
}

bool CollectIdsFromAttachmentObject(const TSharedPtr<FJsonObject>& Object, TSet<FString>& SeenIds, TArray<FString>& OutIds)
{
	if (!Object.IsValid())
	{
		return true;
	}

	FString Id;
	if (!Object->TryGetStringField(TEXT("Id"), Id))
	{
		return false;
	}
	if (SeenIds.Contains(Id))
	{
		return false;
	}

	SeenIds.Add(Id);
	OutIds.Add(Id);
	return true;
}

bool CollectIdsFromTreeNode(const TSharedPtr<FJsonObject>& Node, TSet<FString>& SeenIds, TArray<FString>& OutIds)
{
	if (!CollectIdsFromAttachmentObject(Node, SeenIds, OutIds))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Services = nullptr;
	if (Node.IsValid() && Node->TryGetArrayField(TEXT("Services"), Services) && Services)
	{
		for (const TSharedPtr<FJsonValue>& ServiceValue : *Services)
		{
			if (!CollectIdsFromAttachmentObject(GetObjectFromValue(ServiceValue), SeenIds, OutIds))
			{
				return false;
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Node.IsValid() && Node->TryGetArrayField(TEXT("Children"), Children) && Children)
	{
		for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
		{
			TSharedPtr<FJsonObject> Edge = GetObjectFromValue(ChildValue);
			const TArray<TSharedPtr<FJsonValue>>* Decorators = nullptr;
			if (Edge.IsValid() && Edge->TryGetArrayField(TEXT("Decorators"), Decorators) && Decorators)
			{
				for (const TSharedPtr<FJsonValue>& DecoratorValue : *Decorators)
				{
					if (!CollectIdsFromAttachmentObject(GetObjectFromValue(DecoratorValue), SeenIds, OutIds))
					{
						return false;
					}
				}
			}

			if (!CollectIdsFromTreeNode(GetObjectField(Edge, TEXT("Child")), SeenIds, OutIds))
			{
				return false;
			}
		}
	}

	return true;
}

bool CollectAllTreeIds(const TSharedPtr<FJsonObject>& Tree, TArray<FString>& OutIds)
{
	OutIds.Reset();
	TSet<FString> SeenIds;
	if (!CollectIdsFromTreeNode(GetObjectField(Tree, TEXT("Root")), SeenIds, OutIds))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* RootDecorators = nullptr;
	if (Tree.IsValid() && Tree->TryGetArrayField(TEXT("RootDecorators"), RootDecorators) && RootDecorators)
	{
		for (const TSharedPtr<FJsonValue>& DecoratorValue : *RootDecorators)
		{
			if (!CollectIdsFromAttachmentObject(GetObjectFromValue(DecoratorValue), SeenIds, OutIds))
			{
				return false;
			}
		}
	}

	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeProfileShapeTest,
	"AssetFactory.AssetDocument.BehaviorTree.ProfileShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeProfileShapeTest::RunTest(const FString&)
{
	TSharedPtr<IAssetDocumentProfile> RegisteredProfile = FAssetDocumentService::GetProfileRegistry().FindForClass(UBehaviorTree::StaticClass());
	TestTrue(TEXT("BehaviorTree profile is registered"), RegisteredProfile.IsValid());
	if (!RegisteredProfile.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Exact class is UBehaviorTree"), RegisteredProfile->GetExactClass(), UBehaviorTree::StaticClass());

	FAssetDocumentTemplateContext TemplateContext;
	TemplateContext.Target = TEXT("/Game/AssetDocumentTests/BT_ProfileShape");
	TemplateContext.ClassPath = TEXT("/Script/AIModule.BehaviorTree");
	const TSharedRef<FJsonObject> Template = RegisteredProfile->CreateTemplate(TemplateContext);
	TestEqual(TEXT("Template class is BehaviorTree"), Template->GetStringField(TEXT("Class")), FString(TEXT("/Script/AIModule.BehaviorTree")));

	const TSharedPtr<FJsonObject>* Body = nullptr;
	TestTrue(TEXT("Template contains Body"), Template->TryGetObjectField(TEXT("Body"), Body) && Body && Body->IsValid());
	if (Body && Body->IsValid())
	{
		TestTrue(TEXT("Body contains Blackboard"), (*Body)->HasField(TEXT("Blackboard")));
		TestTrue(TEXT("Body contains Tree"), (*Body)->HasField(TEXT("Tree")));
		TestTrue(TEXT("Body contains EditorLayout"), (*Body)->HasField(TEXT("EditorLayout")));
		TestFalse(TEXT("Body does not contain BlackboardInline"), (*Body)->HasField(TEXT("BlackboardInline")));

		const TSharedPtr<FJsonObject>* Tree = nullptr;
		TestTrue(TEXT("Tree is an object"), (*Body)->TryGetObjectField(TEXT("Tree"), Tree) && Tree && Tree->IsValid());
		if (Tree && Tree->IsValid())
		{
			TestTrue(TEXT("Tree contains RootDecorators"), (*Tree)->HasField(TEXT("RootDecorators")));
			TestTrue(TEXT("Tree contains RootDecoratorLogic"), (*Tree)->HasField(TEXT("RootDecoratorLogic")));
			TestTrue(TEXT("Tree contains Root"), (*Tree)->HasField(TEXT("Root")));
		}
	}

	const TArray<FName> BodyKeys = RegisteredProfile->GetBodyKeys();
	TestTrue(TEXT("Blackboard body key is registered"), BodyKeys.Contains(TEXT("Blackboard")));
	TestTrue(TEXT("Tree body key is registered"), BodyKeys.Contains(TEXT("Tree")));
	TestTrue(TEXT("EditorLayout body key is registered"), BodyKeys.Contains(TEXT("EditorLayout")));
	TestFalse(TEXT("BlackboardInline body key is not registered"), BodyKeys.Contains(TEXT("BlackboardInline")));
	TestNotNull(TEXT("Body root resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Body")));
	TestNotNull(TEXT("Blackboard resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Blackboard")));
	TestNotNull(TEXT("Tree resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("Tree")));
	TestNotNull(TEXT("EditorLayout resolves adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("EditorLayout")));
	TestNull(TEXT("BlackboardInline does not resolve adapter"), RegisteredProfile->ResolveBodyAdapter(TEXT("BlackboardInline")));

	const TArray<FAssetDocumentRegionPolicy> Policies = RegisteredProfile->GetRegionPolicies();
	TestNotNull(TEXT("Policy includes Body.Blackboard"), FindPolicyByRegionId(Policies, TEXT("Body.Blackboard")));
	TestNotNull(TEXT("Policy includes Body.Tree"), FindPolicyByRegionId(Policies, TEXT("Body.Tree")));
	TestNotNull(TEXT("Policy includes Body.EditorLayout"), FindPolicyByRegionId(Policies, TEXT("Body.EditorLayout")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeBlackboardReferenceTest,
	"AssetFactory.AssetDocument.BehaviorTree.BlackboardReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeBlackboardReferenceTest::RunTest(const FString&)
{
	const FString BlackboardTarget = TEXT("/Game/AssetDocumentTests/BB_BT_BlackboardReference");
	const FString BehaviorTreeTarget = TEXT("/Game/AssetDocumentTests/BT_AD_BlackboardReference");
	UBlackboardData* Blackboard = MakeExistingBlackboardAsset(BlackboardTarget);
	TestNotNull(TEXT("Referenced blackboard asset exists"), Blackboard);
	if (!Blackboard)
	{
		return false;
	}

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)), MakeEmptyBehaviorTree(), MakeShared<FJsonObject>());
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(BehaviorTreeTarget, Body)));
	TestTrue(TEXT("BehaviorTree apply with Blackboard AssetRef succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(BehaviorTreeTarget);
	TestNotNull(TEXT("BehaviorTree lifecycle creates an asset"), BehaviorTree);
	if (!BehaviorTree)
	{
		return false;
	}
	TestTrue(TEXT("BehaviorTree has exact class"), BehaviorTree->GetClass() == UBehaviorTree::StaticClass());
	TestTrue(TEXT("BlackboardAsset is assigned"), BehaviorTree->BlackboardAsset == Blackboard);

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = BehaviorTreeTarget;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("BehaviorTree extract succeeds"), ExtractResult.IsSuccess());
	const TSharedPtr<FJsonObject>* ExtractedBody = nullptr;
	const TSharedPtr<FJsonObject>* ExtractedBlackboard = nullptr;
	TestTrue(TEXT("Extracted payload contains Body"), ExtractResult.Payload.IsValid() && ExtractResult.Payload->TryGetObjectField(TEXT("Body"), ExtractedBody) && ExtractedBody && ExtractedBody->IsValid());
	TestTrue(TEXT("Extracted Body contains Blackboard AssetRef"), ExtractedBody && (*ExtractedBody)->TryGetObjectField(TEXT("Blackboard"), ExtractedBlackboard) && ExtractedBlackboard && ExtractedBlackboard->IsValid());
	if (ExtractedBlackboard && ExtractedBlackboard->IsValid())
	{
		TestEqual(TEXT("Extracted Blackboard Kind"), (*ExtractedBlackboard)->GetStringField(TEXT("Kind")), FString(TEXT("AssetRef")));
		TestEqual(TEXT("Extracted Blackboard Path"), (*ExtractedBlackboard)->GetStringField(TEXT("Path")), Blackboard->GetPathName());
	}

	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(BehaviorTreeTarget, Body)));
	TestTrue(TEXT("BehaviorTree diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("Blackboard diff path is stable"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("unchanged"), TEXT("/Body/Blackboard")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeRejectsBlackboardInlineTest,
	"AssetFactory.AssetDocument.BehaviorTree.RejectsBlackboardInline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeRejectsBlackboardInlineTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(nullptr, MakeEmptyBehaviorTree(), MakeShared<FJsonObject>());
	Body->SetObjectField(TEXT("BlackboardInline"), MakeShared<FJsonObject>());

	FAssetDocumentValidateRequest Request;
	Request.Document = MakeBehaviorTreeDocument(TEXT("/Game/AssetDocumentTests/BT_AD_RejectsBlackboardInline"), Body);
	const FAssetDocumentResult Result = FAssetDocumentService().Validate(Request);
	TestFalse(TEXT("BehaviorTree rejects BlackboardInline"), Result.IsSuccess());
	TestTrue(TEXT("BlackboardInline reports unknown region"), ResultHasDiagnostic(Result, TEXT("UnknownBodyKey"), TEXT("/Body/BlackboardInline")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeRejectsMissingBlackboardForKeySelectorsTest,
	"AssetFactory.AssetDocument.BehaviorTree.RejectsMissingBlackboardForKeySelectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeRejectsMissingBlackboardForKeySelectorsTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(nullptr, MakeTreeWithKeySelectorLikeProperty(), MakeShared<FJsonObject>());

	FAssetDocumentValidateRequest Request;
	Request.Document = MakeBehaviorTreeDocument(TEXT("/Game/AssetDocumentTests/BT_AD_RejectsMissingBlackboardForKeySelectors"), Body);
	const FAssetDocumentResult Result = FAssetDocumentService().Validate(Request);
	TestFalse(TEXT("BehaviorTree rejects key selectors without Blackboard"), Result.IsSuccess());
	TestTrue(TEXT("Missing blackboard diagnostic is stable"), ResultHasDiagnostic(Result, TEXT("MissingBehaviorTreeBlackboard"), TEXT("/Body/Blackboard")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeReflectedPropertiesTest,
	"AssetFactory.AssetDocument.BehaviorTree.ReflectedProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeReflectedPropertiesTest::RunTest(const FString&)
{
	UTestDataAsset* ScalarObject = NewObject<UTestDataAsset>(GetTransientPackage());
	TSharedRef<FJsonObject> ScalarProperties = MakeObject();
	ScalarProperties->SetStringField(TEXT("TestString"), TEXT("Scout"));
	ScalarProperties->SetNumberField(TEXT("TestInt"), 7);
	ScalarProperties->SetNumberField(TEXT("TestFloat"), 2.5);
	ScalarProperties->SetBoolField(TEXT("bTestBool"), true);
	ScalarProperties->SetStringField(TEXT("TestName"), TEXT("PatrolKey"));
	ScalarProperties->SetStringField(TEXT("TestText"), TEXT("Visible text"));

	FAssetDocumentCapabilityResult ScalarApply = FAssetDocumentReflectedPropertyUtils::ApplyProperties(ScalarObject, ScalarProperties, TEXT("/Properties"));
	TestTrue(TEXT("scalar reflected apply succeeds"), ScalarApply.bSuccess);
	TestEqual(TEXT("string applied"), ScalarObject->TestString, FString(TEXT("Scout")));
	TestEqual(TEXT("int applied"), ScalarObject->TestInt, 7);
	TestTrue(TEXT("float applied"), FMath::IsNearlyEqual(ScalarObject->TestFloat, 2.5f));
	TestTrue(TEXT("bool applied"), ScalarObject->bTestBool);
	TestEqual(TEXT("name applied"), ScalarObject->TestName, FName(TEXT("PatrolKey")));
	TestEqual(TEXT("text applied"), ScalarObject->TestText.ToString(), FString(TEXT("Visible text")));

	TSharedRef<FJsonObject> ExtractedScalars = MakeObject();
	FAssetDocumentCapabilityResult ScalarExtract = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ScalarObject, ExtractedScalars, TEXT("/Properties"));
	TestTrue(TEXT("scalar reflected extract succeeds"), ScalarExtract.bSuccess);
	TestEqual(TEXT("string extracted"), ExtractedScalars->GetStringField(TEXT("TestString")), FString(TEXT("Scout")));
	TestEqual(TEXT("name extracted"), ExtractedScalars->GetStringField(TEXT("TestName")), FString(TEXT("PatrolKey")));
	TestEqual(TEXT("text extracted"), ExtractedScalars->GetStringField(TEXT("TestText")), FString(TEXT("Visible text")));

	UBTDecorator_Blackboard* Decorator = NewObject<UBTDecorator_Blackboard>(GetTransientPackage());
	TSharedRef<FJsonObject> EnumProperties = MakeObject();
	EnumProperties->SetStringField(TEXT("FlowAbortMode"), TEXT("Both"));
	FAssetDocumentCapabilityResult EnumApply = FAssetDocumentReflectedPropertyUtils::ApplyProperties(Decorator, EnumProperties, TEXT("/Properties"));
	TestTrue(TEXT("enum reflected apply succeeds"), EnumApply.bSuccess);
	TSharedRef<FJsonObject> ExtractedEnum = MakeObject();
	TestTrue(TEXT("enum reflected extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(Decorator, ExtractedEnum, TEXT("/Properties")).bSuccess);
	TestEqual(TEXT("enum extracted"), ExtractedEnum->GetStringField(TEXT("FlowAbortMode")), FString(TEXT("Both")));

	UBehaviorTree* ReferencedTree = NewObject<UBehaviorTree>(GetTransientPackage(), TEXT("BT_ReflectedPropertyReference"));
	UBTTask_RunBehavior* ObjectRefTask = NewObject<UBTTask_RunBehavior>(GetTransientPackage());
	TSharedRef<FJsonObject> ObjectRefProperties = MakeObject();
	ObjectRefProperties->SetObjectField(TEXT("BehaviorAsset"), MakeAssetRef(ReferencedTree->GetPathName()));
	TestTrue(TEXT("AssetRef object apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(ObjectRefTask, ObjectRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedObjectRef = MakeObject();
	TestTrue(TEXT("AssetRef object extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ObjectRefTask, ExtractedObjectRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedBehaviorAsset = GetObjectField(ExtractedObjectRef, TEXT("BehaviorAsset"));
	TestTrue(TEXT("object reference extracts as AssetRef"), ExtractedBehaviorAsset.IsValid() && ExtractedBehaviorAsset->GetStringField(TEXT("Kind")) == TEXT("AssetRef"));

	TSharedRef<FJsonObject> ExtraAssetRefProperties = MakeObject();
	TSharedPtr<FJsonObject> ExtraAssetRef = MakeAssetRef(ReferencedTree->GetPathName());
	ExtraAssetRef->SetStringField(TEXT("Extra"), TEXT("invalid"));
	ExtraAssetRefProperties->SetObjectField(TEXT("BehaviorAsset"), ExtraAssetRef);
	FAssetDocumentCapabilityResult ExtraAssetRefResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(ObjectRefTask, ExtraAssetRefProperties, TEXT("/Properties"));
	TestFalse(TEXT("AssetRef rejects unknown object fields"), ExtraAssetRefResult.bSuccess);
	TestTrue(TEXT("AssetRef unknown field path/code is exact"), HasDiagnostic(ExtraAssetRefResult, TEXT("/Properties/BehaviorAsset/Extra"), TEXT("UnknownField")));

	UBTTask_RunBehavior* RawObjectRefTask = NewObject<UBTTask_RunBehavior>(GetTransientPackage());
	TSharedRef<FJsonObject> RawObjectRefProperties = MakeObject();
	RawObjectRefProperties->SetStringField(TEXT("BehaviorAsset"), ReferencedTree->GetPathName());
	TestTrue(TEXT("raw object path apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(RawObjectRefTask, RawObjectRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedRawObjectRef = MakeObject();
	TestTrue(TEXT("raw object path extracts canonically"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(RawObjectRefTask, ExtractedRawObjectRef, TEXT("/Properties")).bSuccess);
	TestTrue(TEXT("raw object path canonicalizes to AssetRef"), GetObjectField(ExtractedRawObjectRef, TEXT("BehaviorAsset")).IsValid());

	UBTTask_SetKeyValueClass* ClassRefTask = NewObject<UBTTask_SetKeyValueClass>(GetTransientPackage());
	TSharedRef<FJsonObject> ClassRefProperties = MakeObject();
	ClassRefProperties->SetObjectField(TEXT("BaseClass"), MakeClassRef(TEXT("/Script/Engine.Actor")));
	TestTrue(TEXT("ClassRef apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(ClassRefTask, ClassRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedClassRef = MakeObject();
	TestTrue(TEXT("ClassRef extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ClassRefTask, ExtractedClassRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedBaseClass = GetObjectField(ExtractedClassRef, TEXT("BaseClass"));
	TestTrue(TEXT("class reference extracts as ClassRef"), ExtractedBaseClass.IsValid() && ExtractedBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));

	TSharedRef<FJsonObject> ExtraClassRefProperties = MakeObject();
	TSharedPtr<FJsonObject> ExtraClassRef = MakeClassRef(TEXT("/Script/Engine.Actor"));
	ExtraClassRef->SetStringField(TEXT("Extra"), TEXT("invalid"));
	ExtraClassRefProperties->SetObjectField(TEXT("BaseClass"), ExtraClassRef);
	FAssetDocumentCapabilityResult ExtraClassRefResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(ClassRefTask, ExtraClassRefProperties, TEXT("/Properties"));
	TestFalse(TEXT("ClassRef rejects unknown object fields"), ExtraClassRefResult.bSuccess);
	TestTrue(TEXT("ClassRef unknown field path/code is exact"), HasDiagnostic(ExtraClassRefResult, TEXT("/Properties/BaseClass/Extra"), TEXT("UnknownField")));

	UBTTask_SetKeyValueClass* NestedClassRefTask = NewObject<UBTTask_SetKeyValueClass>(GetTransientPackage());
	TSharedRef<FJsonObject> NestedClassRefProperties = MakeObject();
	TSharedPtr<FJsonObject> NestedValue = MakeShared<FJsonObject>();
	NestedValue->SetStringField(TEXT("DefaultValue"), TEXT("/Script/Engine.Actor"));
	NestedValue->SetStringField(TEXT("BaseClass"), TEXT("/Script/Engine.Actor"));
	NestedClassRefProperties->SetObjectField(TEXT("Value"), NestedValue);
	TestTrue(TEXT("nested struct raw class refs apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedClassRefTask, NestedClassRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedNestedClassRef = MakeObject();
	TestTrue(TEXT("nested struct class refs extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(NestedClassRefTask, ExtractedNestedClassRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedNestedValue = GetObjectField(ExtractedNestedClassRef, TEXT("Value"));
	TSharedPtr<FJsonObject> ExtractedNestedDefaultClass = GetObjectField(ExtractedNestedValue, TEXT("DefaultValue"));
	TSharedPtr<FJsonObject> ExtractedNestedBaseClass = GetObjectField(ExtractedNestedValue, TEXT("BaseClass"));
	TestTrue(TEXT("nested struct default class reference extracts as ClassRef"), ExtractedNestedDefaultClass.IsValid() && ExtractedNestedDefaultClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
	TestTrue(TEXT("nested struct base class reference extracts as ClassRef"), ExtractedNestedBaseClass.IsValid() && ExtractedNestedBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
	UBTTask_SetKeyValueClass* NestedClassRefRoundtripTask = NewObject<UBTTask_SetKeyValueClass>(GetTransientPackage());
	TestTrue(TEXT("nested struct ClassRef object roundtrip apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedClassRefRoundtripTask, ExtractedNestedClassRef, TEXT("/Properties")).bSuccess);

	UBTTask_SetKeyValueObject* NestedObjectRefTask = NewObject<UBTTask_SetKeyValueObject>(GetTransientPackage());
	TSharedRef<FJsonObject> NestedObjectRefProperties = MakeObject();
	TSharedPtr<FJsonObject> NestedObjectValue = MakeShared<FJsonObject>();
	NestedObjectValue->SetStringField(TEXT("DefaultValue"), ReferencedTree->GetPathName());
	NestedObjectValue->SetStringField(TEXT("BaseClass"), UBehaviorTree::StaticClass()->GetPathName());
	NestedObjectRefProperties->SetObjectField(TEXT("Value"), NestedObjectValue);
	TestTrue(TEXT("nested struct raw object refs apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedObjectRefTask, NestedObjectRefProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedNestedObjectRef = MakeObject();
	TestTrue(TEXT("nested struct object refs extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(NestedObjectRefTask, ExtractedNestedObjectRef, TEXT("/Properties")).bSuccess);
	TSharedPtr<FJsonObject> ExtractedNestedObjectValue = GetObjectField(ExtractedNestedObjectRef, TEXT("Value"));
	TSharedPtr<FJsonObject> ExtractedNestedDefaultObject = GetObjectField(ExtractedNestedObjectValue, TEXT("DefaultValue"));
	TSharedPtr<FJsonObject> ExtractedNestedObjectBaseClass = GetObjectField(ExtractedNestedObjectValue, TEXT("BaseClass"));
	TestTrue(TEXT("nested struct default object reference extracts as AssetRef"), ExtractedNestedDefaultObject.IsValid() && ExtractedNestedDefaultObject->GetStringField(TEXT("Kind")) == TEXT("AssetRef"));
	TestTrue(TEXT("nested struct object base class reference extracts as ClassRef"), ExtractedNestedObjectBaseClass.IsValid() && ExtractedNestedObjectBaseClass->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
	UBTTask_SetKeyValueObject* NestedObjectRefRoundtripTask = NewObject<UBTTask_SetKeyValueObject>(GetTransientPackage());
	TestTrue(TEXT("nested struct AssetRef object roundtrip apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(NestedObjectRefRoundtripTask, ExtractedNestedObjectRef, TEXT("/Properties")).bSuccess);

	UBTTask_SetKeyValueStruct* StructValueTask = NewObject<UBTTask_SetKeyValueStruct>(GetTransientPackage());
	TSharedRef<FJsonObject> ExtractedStructValueTask = MakeObject();
	FAssetDocumentCapabilityResult InstancedStructExtractResult = FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(StructValueTask, ExtractedStructValueTask, TEXT("/Properties"));
	TestFalse(TEXT("FInstancedStruct extraction is explicitly rejected"), InstancedStructExtractResult.bSuccess);
	TestTrue(TEXT("FInstancedStruct extraction path/code is exact"), HasDiagnostic(InstancedStructExtractResult, TEXT("/Properties/Value/DefaultValue"), TEXT("UnsupportedProperty")));

	TSharedRef<FJsonObject> InstancedStructProperties = MakeObject();
	TSharedPtr<FJsonObject> InstancedStructValue = MakeShared<FJsonObject>();
	InstancedStructValue->SetObjectField(TEXT("DefaultValue"), MakeShared<FJsonObject>());
	InstancedStructProperties->SetObjectField(TEXT("Value"), InstancedStructValue);
	FAssetDocumentCapabilityResult InstancedStructValidateResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(StructValueTask, InstancedStructProperties, TEXT("/Properties"));
	TestFalse(TEXT("FInstancedStruct authored input is explicitly rejected"), InstancedStructValidateResult.bSuccess);
	TestTrue(TEXT("FInstancedStruct validation path/code is exact"), HasDiagnostic(InstancedStructValidateResult, TEXT("/Properties/Value/DefaultValue"), TEXT("UnsupportedProperty")));

	UBTTask_WaitBlackboardTime* SelectorTask = NewObject<UBTTask_WaitBlackboardTime>(GetTransientPackage());
	FStructProperty* SelectorProperty = FindFProperty<FStructProperty>(SelectorTask->GetClass(), TEXT("BlackboardKey"));
	TestNotNull(TEXT("BlackboardKey selector property exists"), SelectorProperty);
	if (SelectorProperty)
	{
		TSharedRef<FJsonObject> SelectorJson = MakeObject();
		SelectorJson->SetStringField(TEXT("SelectedKeyName"), TEXT("TargetActor"));
		SelectorJson->SetBoolField(TEXT("bNoneIsAllowedValue"), true);
		void* SelectorPtr = SelectorProperty->ContainerPtrToValuePtr<void>(SelectorTask);
		TestTrue(TEXT("blackboard selector apply succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, SelectorJson, TEXT("/Properties/BlackboardKey")).bSuccess);
		TSharedPtr<FJsonValue> ExtractedSelectorValue;
		TestTrue(TEXT("blackboard selector extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractBlackboardKeySelector(SelectorProperty, SelectorPtr, ExtractedSelectorValue, TEXT("/Properties/BlackboardKey")).bSuccess);
		TSharedPtr<FJsonObject> ExtractedSelector = ExtractedSelectorValue.IsValid() ? ExtractedSelectorValue->AsObject() : nullptr;
		TestTrue(TEXT("blackboard selector extracts object"), ExtractedSelector.IsValid());
		if (ExtractedSelector.IsValid())
		{
			TestEqual(TEXT("blackboard selector key name"), ExtractedSelector->GetStringField(TEXT("SelectedKeyName")), FString(TEXT("TargetActor")));
			TestTrue(TEXT("blackboard selector none allowed"), ExtractedSelector->GetBoolField(TEXT("bNoneIsAllowedValue")));
			TestFalse(TEXT("blackboard selector skips transient SelectedKeyID"), ExtractedSelector->HasField(TEXT("SelectedKeyID")));
		}

		FBlackboardKeySelector* Selector = static_cast<FBlackboardKeySelector*>(SelectorPtr);
		UBlackboardKeyType_Object* ObjectFilter = NewObject<UBlackboardKeyType_Object>(SelectorTask);
		ObjectFilter->BaseClass = AActor::StaticClass();
		Selector->AllowedTypes = { ObjectFilter };

		TSharedPtr<FJsonValue> ExtractedFilterSelectorValue;
		TestTrue(TEXT("blackboard selector filter extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractBlackboardKeySelector(SelectorProperty, SelectorPtr, ExtractedFilterSelectorValue, TEXT("/Properties/BlackboardKey")).bSuccess);
		TSharedPtr<FJsonObject> ExtractedFilterSelector = GetObjectFromValue(ExtractedFilterSelectorValue);
		const TArray<TSharedPtr<FJsonValue>>* ExtractedAllowedTypes = nullptr;
		TestTrue(TEXT("blackboard selector extracts allowed type filters"), ExtractedFilterSelector.IsValid() && ExtractedFilterSelector->TryGetArrayField(TEXT("AllowedTypes"), ExtractedAllowedTypes) && ExtractedAllowedTypes && ExtractedAllowedTypes->Num() == 1);
		if (ExtractedAllowedTypes && ExtractedAllowedTypes->Num() == 1)
		{
			TSharedPtr<FJsonObject> ExtractedAllowedType = GetObjectFromValue((*ExtractedAllowedTypes)[0]);
			TSharedPtr<FJsonObject> ExtractedAllowedTypeProperties = GetObjectField(ExtractedAllowedType, TEXT("Properties"));
			TSharedPtr<FJsonObject> ExtractedFilterBaseClass = GetObjectField(ExtractedAllowedTypeProperties, TEXT("BaseClass"));
			TestTrue(TEXT("blackboard selector allowed type extracts ClassRef"), ExtractedAllowedType.IsValid() && ExtractedAllowedType->GetStringField(TEXT("Kind")) == TEXT("ClassRef"));
			TestTrue(TEXT("blackboard selector allowed type preserves BaseClass"), ExtractedFilterBaseClass.IsValid() && ExtractedFilterBaseClass->GetStringField(TEXT("Path")) == AActor::StaticClass()->GetPathName());

			UBTTask_WaitBlackboardTime* RoundtripSelectorTask = NewObject<UBTTask_WaitBlackboardTime>(GetTransientPackage());
			void* RoundtripSelectorPtr = SelectorProperty->ContainerPtrToValuePtr<void>(RoundtripSelectorTask);
			TestTrue(TEXT("blackboard selector filter apply roundtrip succeeds"), FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, RoundtripSelectorPtr, ExtractedFilterSelector.ToSharedRef(), TEXT("/Properties/BlackboardKey")).bSuccess);
			FBlackboardKeySelector* RoundtripSelector = static_cast<FBlackboardKeySelector*>(RoundtripSelectorPtr);
			UBlackboardKeyType_Object* RoundtripObjectFilter = RoundtripSelector->AllowedTypes.Num() == 1 ? Cast<UBlackboardKeyType_Object>(RoundtripSelector->AllowedTypes[0]) : nullptr;
			TestTrue(TEXT("blackboard selector filter roundtrip restores BaseClass"), RoundtripObjectFilter && RoundtripObjectFilter->BaseClass == AActor::StaticClass());
		}

		TSharedRef<FJsonObject> InvalidFilterSelector = MakeObject();
		TArray<TSharedPtr<FJsonValue>> InvalidFilters;
		InvalidFilters.Add(MakeShared<FJsonValueObject>(MakeClassRef(TEXT("/Script/Engine.Actor"))));
		InvalidFilterSelector->SetArrayField(TEXT("AllowedTypes"), InvalidFilters);
		FAssetDocumentCapabilityResult InvalidFilterResult = FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, InvalidFilterSelector, TEXT("/Properties/BlackboardKey"));
		TestFalse(TEXT("invalid blackboard filter class rejected"), InvalidFilterResult.bSuccess);
		TestTrue(TEXT("invalid blackboard filter class path/code is exact"), HasDiagnostic(InvalidFilterResult, TEXT("/Properties/BlackboardKey/AllowedTypes/0/Path"), TEXT("InvalidClassRef")));

		TSharedRef<FJsonObject> UnknownNestedFilterSelector = MakeObject();
		TSharedPtr<FJsonObject> UnknownFilter = MakeClassRef(UBlackboardKeyType_Object::StaticClass()->GetPathName());
		TSharedPtr<FJsonObject> UnknownFilterProperties = MakeShared<FJsonObject>();
		UnknownFilterProperties->SetStringField(TEXT("NotAKeyTypeProperty"), TEXT("bad"));
		UnknownFilter->SetObjectField(TEXT("Properties"), UnknownFilterProperties);
		TArray<TSharedPtr<FJsonValue>> UnknownFilters;
		UnknownFilters.Add(MakeShared<FJsonValueObject>(UnknownFilter));
		UnknownNestedFilterSelector->SetArrayField(TEXT("AllowedTypes"), UnknownFilters);
		FAssetDocumentCapabilityResult UnknownNestedFilterResult = FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, UnknownNestedFilterSelector, TEXT("/Properties/BlackboardKey"));
		TestFalse(TEXT("unknown blackboard filter property rejected"), UnknownNestedFilterResult.bSuccess);
		TestTrue(TEXT("unknown blackboard filter property path/code is exact"), HasDiagnostic(UnknownNestedFilterResult, TEXT("/Properties/BlackboardKey/AllowedTypes/0/Properties/NotAKeyTypeProperty"), TEXT("UnknownProperty")));

		TSharedRef<FJsonObject> NonAuthoredNestedFilterSelector = MakeObject();
		TSharedPtr<FJsonObject> NonAuthoredFilter = MakeClassRef(UBlackboardKeyType_Enum::StaticClass()->GetPathName());
		TSharedPtr<FJsonObject> NonAuthoredFilterProperties = MakeShared<FJsonObject>();
		NonAuthoredFilterProperties->SetBoolField(TEXT("bIsEnumNameValid"), true);
		NonAuthoredFilter->SetObjectField(TEXT("Properties"), NonAuthoredFilterProperties);
		TArray<TSharedPtr<FJsonValue>> NonAuthoredFilters;
		NonAuthoredFilters.Add(MakeShared<FJsonValueObject>(NonAuthoredFilter));
		NonAuthoredNestedFilterSelector->SetArrayField(TEXT("AllowedTypes"), NonAuthoredFilters);
		FAssetDocumentCapabilityResult NonAuthoredNestedFilterResult = FAssetDocumentReflectedPropertyUtils::ApplyBlackboardKeySelector(SelectorProperty, SelectorPtr, NonAuthoredNestedFilterSelector, TEXT("/Properties/BlackboardKey"));
		TestFalse(TEXT("non-authored blackboard filter property rejected"), NonAuthoredNestedFilterResult.bSuccess);
		TestTrue(TEXT("non-authored blackboard filter property path/code is exact"), HasDiagnostic(NonAuthoredNestedFilterResult, TEXT("/Properties/BlackboardKey/AllowedTypes/0/Properties/bIsEnumNameValid"), TEXT("NonAuthoredProperty")));
	}

	ATestActorBase* ContainerObject = NewObject<ATestActorBase>(GetTransientPackage());
	TSharedRef<FJsonObject> ContainerProperties = MakeObject();
	TSharedPtr<FJsonObject> ConfigJson = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Entries;
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("Name"), TEXT("Damage"));
	Entry->SetNumberField(TEXT("Value"), 12.0);
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
	ConfigJson->SetArrayField(TEXT("Entries"), Entries);
	ContainerProperties->SetObjectField(TEXT("Config"), ConfigJson);
	TestTrue(TEXT("array property apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(ContainerObject, ContainerProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedContainer = MakeObject();
	TestTrue(TEXT("array property extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(ContainerObject, ExtractedContainer, TEXT("/Properties")).bSuccess);
	TestTrue(TEXT("array property extracted"), GetObjectField(ExtractedContainer, TEXT("Config")).IsValid());

	UNodeMappingContainer* MapObject = NewObject<UNodeMappingContainer>(GetTransientPackage());
	TSharedRef<FJsonObject> MapProperties = MakeObject();
	TSharedPtr<FJsonObject> SourceToTarget = MakeShared<FJsonObject>();
	SourceToTarget->SetStringField(TEXT("SourceBone"), TEXT("TargetBone"));
	MapProperties->SetObjectField(TEXT("SourceToTarget"), SourceToTarget);
	TestTrue(TEXT("map property apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(MapObject, MapProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedMap = MakeObject();
	TestTrue(TEXT("map property extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(MapObject, ExtractedMap, TEXT("/Properties")).bSuccess);
	TestTrue(TEXT("map property extracted"), GetObjectField(ExtractedMap, TEXT("SourceToTarget")).IsValid());

	UClass* PropertyEditorTestClass = LoadClass<UObject>(nullptr, TEXT("/Script/UnrealEd.PropertyEditorTestObject"));
	TestNotNull(TEXT("PropertyEditorTestObject class exists"), PropertyEditorTestClass);
	if (PropertyEditorTestClass)
	{
		UObject* UnsupportedMapKeyObject = NewObject<UObject>(GetTransientPackage(), PropertyEditorTestClass);
		TSharedRef<FJsonObject> UnsupportedMapKeyProperties = MakeObject();
		TSharedPtr<FJsonObject> UnsupportedMapKeyValue = MakeShared<FJsonObject>();
		UnsupportedMapKeyValue->SetStringField(TEXT("NotAStableKey"), TEXT("Value"));
		UnsupportedMapKeyProperties->SetObjectField(TEXT("LinearColorToStringMap"), UnsupportedMapKeyValue);
		FAssetDocumentCapabilityResult UnsupportedMapKeyResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(UnsupportedMapKeyObject, UnsupportedMapKeyProperties, TEXT("/Properties"));
		TestFalse(TEXT("unsupported map key type rejected"), UnsupportedMapKeyResult.bSuccess);
		TestTrue(TEXT("unsupported map key type path/code is exact"), HasDiagnostic(UnsupportedMapKeyResult, TEXT("/Properties/LinearColorToStringMap"), TEXT("UnsupportedProperty")));
	}

	UBlueprintEditorSettings* SetSettings = NewObject<UBlueprintEditorSettings>(GetTransientPackage());
	TSharedRef<FJsonObject> SetProperties = MakeObject();
	TArray<TSharedPtr<FJsonValue>> SetValues;
	SetValues.Add(MakeShared<FJsonValueString>(TEXT("Zulu")));
	SetValues.Add(MakeShared<FJsonValueString>(TEXT("Alpha")));
	SetProperties->SetArrayField(TEXT("TypePromotionPinDenyList"), SetValues);
	TestTrue(TEXT("set property apply succeeds through shared setter"), FAssetDocumentReflectedPropertyUtils::ApplyProperties(SetSettings, SetProperties, TEXT("/Properties")).bSuccess);
	TSharedRef<FJsonObject> ExtractedSetProperties = MakeObject();
	TestTrue(TEXT("set property extract succeeds"), FAssetDocumentReflectedPropertyUtils::ExtractAuthoredProperties(SetSettings, ExtractedSetProperties, TEXT("/Properties")).bSuccess);
	const TArray<TSharedPtr<FJsonValue>>* ExtractedSetValues = nullptr;
	TestTrue(TEXT("set property extracts stable array"), ExtractedSetProperties->TryGetArrayField(TEXT("TypePromotionPinDenyList"), ExtractedSetValues) && ExtractedSetValues && ExtractedSetValues->Num() == 2);
	if (ExtractedSetValues && ExtractedSetValues->Num() == 2)
	{
		FString FirstSetValue;
		FString SecondSetValue;
		(*ExtractedSetValues)[0]->TryGetString(FirstSetValue);
		(*ExtractedSetValues)[1]->TryGetString(SecondSetValue);
		TestEqual(TEXT("set extraction is canonically sorted first"), FirstSetValue, FString(TEXT("Alpha")));
		TestEqual(TEXT("set extraction is canonically sorted second"), SecondSetValue, FString(TEXT("Zulu")));
	}

	TSharedRef<FJsonObject> SameSetDifferentOrder = MakeObject();
	TArray<TSharedPtr<FJsonValue>> ReorderedSetValues;
	ReorderedSetValues.Add(MakeShared<FJsonValueString>(TEXT("Alpha")));
	ReorderedSetValues.Add(MakeShared<FJsonValueString>(TEXT("Zulu")));
	SameSetDifferentOrder->SetArrayField(TEXT("TypePromotionPinDenyList"), ReorderedSetValues);
	TArray<TSharedPtr<FJsonValue>> SetDiffEntries;
	TestTrue(TEXT("set diff succeeds"), FAssetDocumentReflectedPropertyUtils::DiffProperties(SetSettings, SameSetDifferentOrder, TEXT("/Properties"), SetDiffEntries).bSuccess);
	TestEqual(TEXT("set diff ignores element order"), SetDiffEntries.Num(), 0);

	TSharedRef<FJsonObject> UnknownProperties = MakeObject();
	UnknownProperties->SetStringField(TEXT("DoesNotExist"), TEXT("value"));
	FAssetDocumentCapabilityResult UnknownResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(ScalarObject, UnknownProperties, TEXT("/Properties"));
	TestFalse(TEXT("unknown property rejected"), UnknownResult.bSuccess);
	TestTrue(TEXT("unknown property path/code is exact"), HasDiagnostic(UnknownResult, TEXT("/Properties/DoesNotExist"), TEXT("UnknownProperty")));

	TSharedRef<FJsonObject> RuntimeProperties = MakeObject();
	RuntimeProperties->SetStringField(TEXT("ParentNode"), TEXT("invalid"));
	FAssetDocumentCapabilityResult RuntimeResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(Decorator, RuntimeProperties, TEXT("/Properties"));
	TestFalse(TEXT("non-authored property rejected"), RuntimeResult.bSuccess);
	TestTrue(TEXT("non-authored property path/code is exact"), HasDiagnostic(RuntimeResult, TEXT("/Properties/ParentNode"), TEXT("NonAuthoredProperty")));

	UMovieSceneCVarSection* UnsupportedObject = NewObject<UMovieSceneCVarSection>(GetTransientPackage());
	TSharedRef<FJsonObject> UnsupportedProperties = MakeObject();
	TArray<TSharedPtr<FJsonValue>> ConsoleVariableCollections;
	TSharedPtr<FJsonObject> UnsupportedCollection = MakeShared<FJsonObject>();
	UnsupportedCollection->SetField(TEXT("Interface"), MakeShared<FJsonValueNull>());
	ConsoleVariableCollections.Add(MakeShared<FJsonValueObject>(UnsupportedCollection));
	UnsupportedProperties->SetArrayField(TEXT("ConsoleVariableCollections"), ConsoleVariableCollections);
	FAssetDocumentCapabilityResult UnsupportedResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(UnsupportedObject, UnsupportedProperties, TEXT("/Properties"));
	TestFalse(TEXT("unsupported authored property rejected"), UnsupportedResult.bSuccess);
	TestTrue(TEXT("unsupported authored property path/code is exact"), HasDiagnostic(UnsupportedResult, TEXT("/Properties/ConsoleVariableCollections/0/Interface"), TEXT("UnsupportedProperty")));

	UBehaviorTree* TreeObject = NewObject<UBehaviorTree>(GetTransientPackage());
	TSharedRef<FJsonObject> TreeOwnedProperties = MakeObject();
	TreeOwnedProperties->SetField(TEXT("RootNode"), MakeShared<FJsonValueNull>());
	FAssetDocumentCapabilityResult TreeOwnedResult = FAssetDocumentReflectedPropertyUtils::ValidateProperties(TreeObject, TreeOwnedProperties, TEXT("/Properties"));
	TestFalse(TEXT("Body.Tree-owned property rejected"), TreeOwnedResult.bSuccess);
	TestTrue(TEXT("Body.Tree-owned property path/code is exact"), HasDiagnostic(TreeOwnedResult, TEXT("/Properties/RootNode"), TEXT("BodyTreeProperty")));

	TSharedRef<FJsonObject> DiffProperties = MakeObject();
	DiffProperties->SetStringField(TEXT("TestString"), TEXT("Changed"));
	TArray<TSharedPtr<FJsonValue>> DiffEntries;
	TestTrue(TEXT("reflected diff succeeds"), FAssetDocumentReflectedPropertyUtils::DiffProperties(ScalarObject, DiffProperties, TEXT("/Properties"), DiffEntries).bSuccess);
	TestTrue(TEXT("reflected diff reports changed entry"), DiffEntries.Num() == 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeTreeTest,
	"AssetFactory.AssetDocument.BehaviorTree.Tree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeTreeTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_Tree");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	TestNotNull(TEXT("BehaviorTree exists after semantic tree apply"), BehaviorTree);
	UBTCompositeNode* Root = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	TestNotNull(TEXT("semantic RootNode is materialized"), Root);
	if (!Root)
	{
		return false;
	}

	TestTrue(TEXT("root is selector class"), Root->GetClass()->GetPathName().Contains(TEXT("BTComposite_Selector")));
	TestEqual(TEXT("root services materialized"), Root->Services.Num(), 1);
	TestEqual(TEXT("root children materialized"), Root->Children.Num(), 2);
	UBTTaskNode* MoveToTask = Root->Children.IsValidIndex(0) ? Root->Children[0].ChildTask : nullptr;
	UBTTaskNode* RunSubtreeTask = Root->Children.IsValidIndex(1) ? Root->Children[1].ChildTask : nullptr;
	TestTrue(TEXT("first child is BTTask_MoveTo"), MoveToTask && MoveToTask->GetClass()->IsChildOf(UBTTask_MoveTo::StaticClass()));
	TestTrue(TEXT("second child is BTTask_RunBehavior"), RunSubtreeTask && RunSubtreeTask->GetClass()->IsChildOf(UBTTask_RunBehavior::StaticClass()));
	TestEqual(TEXT("edge decorator materialized"), Root->Children[0].Decorators.Num(), 1);

	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("semantic tree extract succeeds"), ExtractResult.IsSuccess());
	const TSharedPtr<FJsonObject>* Body = nullptr;
	const TSharedPtr<FJsonObject>* Tree = nullptr;
	const TSharedPtr<FJsonObject>* ExtractedRoot = nullptr;
	TestTrue(TEXT("extract payload contains Body.Tree.Root"), ExtractResult.Payload.IsValid()
		&& ExtractResult.Payload->TryGetObjectField(TEXT("Body"), Body)
		&& Body && (*Body)->TryGetObjectField(TEXT("Tree"), Tree)
		&& Tree && (*Tree)->TryGetObjectField(TEXT("Root"), ExtractedRoot));
	if (ExtractedRoot && ExtractedRoot->IsValid())
	{
		TestEqual(TEXT("root id extracted"), (*ExtractedRoot)->GetStringField(TEXT("Id")), FString(TEXT("RootSelector")));
		TestEqual(TEXT("root class extracted as full path"), (*ExtractedRoot)->GetStringField(TEXT("Class")), FString(TEXT("/Script/AIModule.BTComposite_Selector")));
		const TArray<TSharedPtr<FJsonValue>>* ExtractedChildren = nullptr;
		TestTrue(TEXT("children extracted"), (*ExtractedRoot)->TryGetArrayField(TEXT("Children"), ExtractedChildren) && ExtractedChildren && ExtractedChildren->Num() == 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeEmptyTreeExtractTest,
	"AssetFactory.AssetDocument.BehaviorTree.EmptyTreeExtract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeEmptyTreeExtractTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_EmptyExtract");
	UBehaviorTree* EmptyTree = NewObject<UBehaviorTree>(
		CreatePackage(*Target),
		*FPackageName::GetLongPackageAssetName(Target),
		RF_Public | RF_Standalone | RF_Transactional);
	TestNotNull(TEXT("empty BehaviorTree asset exists"), EmptyTree);
	if (!EmptyTree)
	{
		return false;
	}

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult ExtractResult = Service.Extract(ExtractRequest);
	TestTrue(TEXT("empty BehaviorTree extract succeeds"), ExtractResult.IsSuccess());

	const TSharedPtr<FJsonObject>* Body = nullptr;
	const TSharedPtr<FJsonObject>* Tree = nullptr;
	const TSharedPtr<FJsonObject>* Root = nullptr;
	TestTrue(TEXT("empty extract payload contains Body.Tree.Root"), ExtractResult.Payload.IsValid()
		&& ExtractResult.Payload->TryGetObjectField(TEXT("Body"), Body)
		&& Body && (*Body)->TryGetObjectField(TEXT("Tree"), Tree)
		&& Tree && (*Tree)->TryGetObjectField(TEXT("Root"), Root));
	TestEqual(TEXT("empty root is strict empty object"), Root && Root->IsValid() ? (*Root)->Values.Num() : -1, 0);

	TSharedPtr<FJsonObject> BodyDocument = MakeBehaviorTreeBody(nullptr, MakeEmptyBehaviorTree(), MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, BodyDocument)));
	TestTrue(TEXT("empty BehaviorTree diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("empty tree diff path is stable"), DiffPayloadHasAnyEntry(DiffResult.Payload, TEXT("/Body/Tree")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeRootDecoratorsTest,
	"AssetFactory.AssetDocument.BehaviorTree.RootDecorators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeRootDecoratorsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_RootDecorators");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, true))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	TestNotNull(TEXT("BehaviorTree exists"), BehaviorTree);
	TestEqual(TEXT("root decorators materialized"), BehaviorTree ? BehaviorTree->RootDecorators.Num() : 0, 1);
	TestEqual(TEXT("root decorator logic materialized"), BehaviorTree ? BehaviorTree->RootDecoratorOps.Num() : 0, 1);

	TSharedPtr<FJsonObject> DesiredBody = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		MakeTask7BehaviorTree(Target + TEXT("_Subtree"), true, false),
		MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DesiredBody)));
	TestTrue(TEXT("root decorator diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("root decorator semantic path is emitted"), DiffPayloadHasAnyEntry(DiffResult.Payload, TEXT("/Body/Tree/RootDecorators/RootHasTarget")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeDecoratorLogicTest,
	"AssetFactory.AssetDocument.BehaviorTree.DecoratorLogic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeDecoratorLogicTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_DecoratorLogic");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, true))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBTCompositeNode* Root = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	TestTrue(TEXT("edge decorator logic has Test op"), Root && Root->Children.Num() > 0 && Root->Children[0].DecoratorOps.Num() == 1 && Root->Children[0].DecoratorOps[0].Operation == EBTDecoratorLogic::Test);
	TestTrue(TEXT("root decorator logic has Test op"), BehaviorTree && BehaviorTree->RootDecoratorOps.Num() == 1 && BehaviorTree->RootDecoratorOps[0].Operation == EBTDecoratorLogic::Test);

	TSharedPtr<FJsonObject> DesiredTree = MakeTask7BehaviorTree(Target + TEXT("_Subtree"), true, false);
	TSharedPtr<FJsonObject> RootObject = GetObjectField(DesiredTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (RootObject.IsValid() && RootObject->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstEdge = GetObjectFromValue((*Children)[0]);
		FirstEdge->SetArrayField(TEXT("DecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	}

	TSharedPtr<FJsonObject> DesiredBody = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		DesiredTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DesiredBody)));
	TestTrue(TEXT("decorator logic diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("edge decorator logic path uses semantic node ids and index"), DiffPayloadHasAnyEntry(DiffResult.Payload, TEXT("/Body/Tree/RootSelector/Children/MoveToTarget/DecoratorLogic/0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeDecoratorLogicRejectsInvalidShapeTest,
	"AssetFactory.AssetDocument.BehaviorTree.DecoratorLogicRejectsInvalidShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeDecoratorLogicRejectsInvalidShapeTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_InvalidDecoratorLogic");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeExistingBlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	TSharedPtr<FJsonObject> InvalidTree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	TSharedPtr<FJsonObject> RootObject = GetObjectField(InvalidTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (RootObject.IsValid() && RootObject->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstEdge = GetObjectFromValue((*Children)[0]);
		TArray<TSharedPtr<FJsonValue>> InvalidLogic;
		InvalidLogic.Add(MakeObjectValue(MakeDecoratorLogicOp(TEXT("And"), 2)));
		FirstEdge->SetArrayField(TEXT("DecoratorLogic"), InvalidLogic);
	}

	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		InvalidTree,
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest AndRequest;
	AndRequest.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult AndResult = FAssetDocumentService().Validate(AndRequest);
	TestFalse(TEXT("And without two child expressions is rejected"), AndResult.IsSuccess());
	TestTrue(TEXT("invalid And logic reports exact edge path"), ResultHasDiagnostic(AndResult, TEXT("InvalidBehaviorTreeDecoratorLogicShape"), TEXT("/Body/Tree/Root/Children/0/DecoratorLogic")));

	TSharedPtr<FJsonObject> OutOfBoundsTree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	TSharedPtr<FJsonObject> OutOfBoundsRoot = GetObjectField(OutOfBoundsTree, TEXT("Root"));
	if (OutOfBoundsRoot.IsValid() && OutOfBoundsRoot->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstEdge = GetObjectFromValue((*Children)[0]);
		TArray<TSharedPtr<FJsonValue>> InvalidLogic;
		InvalidLogic.Add(MakeObjectValue(MakeDecoratorLogicTest(1)));
		FirstEdge->SetArrayField(TEXT("DecoratorLogic"), InvalidLogic);
	}

	Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		OutOfBoundsTree,
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest TestRequest;
	TestRequest.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult TestResult = FAssetDocumentService().Validate(TestRequest);
	TestFalse(TEXT("Test out of decorator bounds is rejected"), TestResult.IsSuccess());
	TestTrue(TEXT("invalid Test logic reports exact entry path"), ResultHasDiagnostic(TestResult, TEXT("InvalidBehaviorTreeDecoratorLogicShape"), TEXT("/Body/Tree/Root/Children/0/DecoratorLogic/0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeRejectsNodeLevelDecoratorsTest,
	"AssetFactory.AssetDocument.BehaviorTree.RejectsNodeLevelDecorators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeRejectsNodeLevelDecoratorsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_NodeLevelDecorators");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeExistingBlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	TSharedPtr<FJsonObject> Tree = MakeTask7BehaviorTree(SubtreeTarget, false, false);
	TSharedPtr<FJsonObject> Root = GetObjectField(Tree, TEXT("Root"));
	if (Root.IsValid())
	{
		Root->SetArrayField(TEXT("Decorators"), TArray<TSharedPtr<FJsonValue>>());
	}
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		Tree,
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest DecoratorsRequest;
	DecoratorsRequest.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult DecoratorsResult = FAssetDocumentService().Validate(DecoratorsRequest);
	TestFalse(TEXT("node-level Decorators are rejected instead of ignored"), DecoratorsResult.IsSuccess());
	TestTrue(TEXT("node-level Decorators diagnostic is exact"), ResultHasDiagnostic(DecoratorsResult, TEXT("UnsupportedBehaviorTreeNodeDecorators"), TEXT("/Body/Tree/Root/Decorators")));

	Tree = MakeTask7BehaviorTree(SubtreeTarget, false, false);
	Root = GetObjectField(Tree, TEXT("Root"));
	if (Root.IsValid())
	{
		Root->SetArrayField(TEXT("DecoratorLogic"), TArray<TSharedPtr<FJsonValue>>());
	}
	Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		Tree,
		MakeShared<FJsonObject>());
	FAssetDocumentValidateRequest LogicRequest;
	LogicRequest.Document = MakeBehaviorTreeDocument(Target, Body);
	const FAssetDocumentResult LogicResult = FAssetDocumentService().Validate(LogicRequest);
	TestFalse(TEXT("node-level DecoratorLogic is rejected instead of ignored"), LogicResult.IsSuccess());
	TestTrue(TEXT("node-level DecoratorLogic diagnostic is exact"), ResultHasDiagnostic(LogicResult, TEXT("UnsupportedBehaviorTreeNodeDecorators"), TEXT("/Body/Tree/Root/DecoratorLogic")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeSemanticDiffPathsTest,
	"AssetFactory.AssetDocument.BehaviorTree.SemanticDiffPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeSemanticDiffPathsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_SemanticDiffPaths");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, true))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	TestNotNull(TEXT("BehaviorTree exists"), BehaviorTree);
	if (!BehaviorTree)
	{
		return false;
	}

	TSharedPtr<FJsonObject> DesiredTree = MakeTask7BehaviorTree(Target + TEXT("_Subtree"), true, false);
	TSharedPtr<FJsonObject> Root = GetObjectField(DesiredTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Root.IsValid() && Root->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstEdge = GetObjectFromValue((*Children)[0]);
		const TArray<TSharedPtr<FJsonValue>>* Decorators = nullptr;
		if (FirstEdge.IsValid() && FirstEdge->TryGetArrayField(TEXT("Decorators"), Decorators) && Decorators && Decorators->Num() > 0)
		{
			TSharedPtr<FJsonObject> Decorator = GetObjectFromValue((*Decorators)[0]);
			TSharedPtr<FJsonObject> Properties = GetObjectField(Decorator, TEXT("Properties"));
			TSharedPtr<FJsonObject> BlackboardKey = GetObjectField(Properties, TEXT("BlackboardKey"));
			if (BlackboardKey.IsValid())
			{
				BlackboardKey->SetStringField(TEXT("SelectedKeyName"), TEXT("OtherTargetActor"));
			}
		}
	}

	TSharedPtr<FJsonObject> DesiredBody = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		DesiredTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DesiredBody)));
	TestTrue(TEXT("semantic diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("edge decorator change path is precise"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootSelector/Children/MoveToTarget/Decorators/HasTarget")));
	TestFalse(TEXT("edge decorator change does not dirty parent node path"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootSelector")));
	TestFalse(TEXT("edge decorator change does not dirty edge path"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootSelector/Children/MoveToTarget")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeOrderDiffPathsTest,
	"AssetFactory.AssetDocument.BehaviorTree.OrderDiffPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeOrderDiffPathsTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_OrderDiffPaths");
	const FString BlackboardTarget = Target + TEXT("_BB");
	const FString SubtreeTarget = Target + TEXT("_Subtree");
	MakeExistingBlackboardAsset(BlackboardTarget);
	MakeExistingBehaviorTreeAsset(SubtreeTarget);

	FAssetDocumentService Service;
	TSharedPtr<FJsonObject> AppliedTree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	AddSecondDecoratorSet(AppliedTree);
	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		AppliedTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult ApplyResult = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, Body)));
	TestTrue(TEXT("order fixture apply succeeds"), ApplyResult.IsSuccess());
	if (!ApplyResult.IsSuccess())
	{
		AddError(ApplyResult.Message);
		return false;
	}

	TSharedPtr<FJsonObject> DesiredTree = MakeTask7BehaviorTree(SubtreeTarget, true, false);
	AddSecondDecoratorSet(DesiredTree);
	TSharedPtr<FJsonObject> Root = GetObjectField(DesiredTree, TEXT("Root"));
	SwapArrayEntries(Root, TEXT("Children"), 0, 1);
	if (Root.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (Root->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 1)
		{
			TSharedPtr<FJsonObject> MoveEdge = GetObjectFromValue((*Children)[1]);
			SwapArrayEntries(MoveEdge, TEXT("Decorators"), 0, 1);
		}
	}
	SwapArrayEntries(DesiredTree, TEXT("RootDecorators"), 0, 1);

	TSharedPtr<FJsonObject> DesiredBody = MakeBehaviorTreeBody(
		MakeAssetRef(MakeObjectPathFromTarget(BlackboardTarget)),
		DesiredTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DesiredBody)));
	TestTrue(TEXT("order diff succeeds"), DiffResult.IsSuccess());
	TestTrue(TEXT("child order change is reported"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootSelector/Children")));
	TestTrue(TEXT("edge decorator order change is reported"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootSelector/Children/MoveToTarget/Decorators")));
	TestTrue(TEXT("root decorator order change is reported"), DiffPayloadHasEntry(DiffResult.Payload, TEXT("changed"), TEXT("/Body/Tree/RootDecorators")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeExtractsUniqueIdsForDuplicateDisplayNamesTest,
	"AssetFactory.AssetDocument.BehaviorTree.ExtractsUniqueIdsForDuplicateDisplayNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeExtractsUniqueIdsForDuplicateDisplayNamesTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_DuplicateDisplayNames");
	UBehaviorTree* BehaviorTree = MakeExistingBehaviorTreeAsset(Target);
	BehaviorTree->BlackboardAsset = MakeExistingBlackboardAsset(Target + TEXT("_BB"));
	TestNotNull(TEXT("manual BehaviorTree exists"), BehaviorTree);
	if (!BehaviorTree)
	{
		return false;
	}

	UClass* SelectorClass = LoadClass<UBTCompositeNode>(nullptr, TEXT("/Script/AIModule.BTComposite_Selector"));
	TestNotNull(TEXT("selector class exists"), SelectorClass);
	if (!SelectorClass)
	{
		return false;
	}

	UBTCompositeNode* Root = NewObject<UBTCompositeNode>(BehaviorTree, SelectorClass, NAME_None, RF_Transactional);
	Root->NodeName = TEXT("Duplicate");
	UBTService* FirstService = NewObject<UBTService>(BehaviorTree, UBTService_DefaultFocus::StaticClass(), NAME_None, RF_Transactional);
	UBTService* SecondService = NewObject<UBTService>(BehaviorTree, UBTService_DefaultFocus::StaticClass(), NAME_None, RF_Transactional);
	FirstService->NodeName = TEXT("Duplicate");
	SecondService->NodeName = TEXT("Duplicate");
	Root->Services = { FirstService, SecondService };

	UBTTaskNode* FirstTask = NewObject<UBTTaskNode>(BehaviorTree, UBTTask_MoveTo::StaticClass(), NAME_None, RF_Transactional);
	UBTTaskNode* SecondTask = NewObject<UBTTaskNode>(BehaviorTree, UBTTask_MoveTo::StaticClass(), NAME_None, RF_Transactional);
	FirstTask->NodeName = TEXT("Duplicate");
	SecondTask->NodeName = TEXT("Duplicate");

	FBTCompositeChild FirstChild;
	FirstChild.ChildTask = FirstTask;
	UBTDecorator* FirstDecorator = NewObject<UBTDecorator>(BehaviorTree, UBTDecorator_Blackboard::StaticClass(), NAME_None, RF_Transactional);
	UBTDecorator* SecondDecorator = NewObject<UBTDecorator>(BehaviorTree, UBTDecorator_Blackboard::StaticClass(), NAME_None, RF_Transactional);
	FirstDecorator->NodeName = TEXT("Duplicate");
	SecondDecorator->NodeName = TEXT("Duplicate");
	FirstChild.Decorators = { FirstDecorator, SecondDecorator };

	FBTCompositeChild SecondChild;
	SecondChild.ChildTask = SecondTask;
	Root->Children = { FirstChild, SecondChild };

	UBTDecorator* FirstRootDecorator = NewObject<UBTDecorator>(BehaviorTree, UBTDecorator_Blackboard::StaticClass(), NAME_None, RF_Transactional);
	UBTDecorator* SecondRootDecorator = NewObject<UBTDecorator>(BehaviorTree, UBTDecorator_Blackboard::StaticClass(), NAME_None, RF_Transactional);
	FirstRootDecorator->NodeName = TEXT("Duplicate");
	SecondRootDecorator->NodeName = TEXT("Duplicate");
	BehaviorTree->RootDecorators = { FirstRootDecorator, SecondRootDecorator };
	BehaviorTree->RootNode = Root;

	FAssetDocumentService Service;
	FAssetDocumentExtractRequest ExtractRequest;
	ExtractRequest.AssetPath = Target;
	ExtractRequest.bDiffOnly = true;
	const FAssetDocumentResult FirstExtract = Service.Extract(ExtractRequest);
	TestTrue(TEXT("duplicate display name extract succeeds"), FirstExtract.IsSuccess());
	const FAssetDocumentResult SecondExtract = Service.Extract(ExtractRequest);
	TestTrue(TEXT("duplicate display name extract is repeatable"), SecondExtract.IsSuccess());
	if (!FirstExtract.IsSuccess() || !SecondExtract.IsSuccess())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* FirstBody = nullptr;
	const TSharedPtr<FJsonObject>* FirstTree = nullptr;
	TestTrue(TEXT("first extract contains tree"), FirstExtract.Payload->TryGetObjectField(TEXT("Body"), FirstBody) && FirstBody && (*FirstBody)->TryGetObjectField(TEXT("Tree"), FirstTree));
	TArray<FString> FirstIds;
	TestTrue(TEXT("all extracted ids are unique"), CollectAllTreeIds(FirstTree ? *FirstTree : nullptr, FirstIds));
	TestTrue(TEXT("duplicate display names produce multiple ids"), FirstIds.Num() >= 9);

	const TSharedPtr<FJsonObject>* SecondBody = nullptr;
	const TSharedPtr<FJsonObject>* SecondTree = nullptr;
	TestTrue(TEXT("second extract contains tree"), SecondExtract.Payload->TryGetObjectField(TEXT("Body"), SecondBody) && SecondBody && (*SecondBody)->TryGetObjectField(TEXT("Tree"), SecondTree));
	const FString FirstTreeJson = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeShared<FJsonValueObject>(*FirstTree));
	const FString SecondTreeJson = FAssetDocumentJsonRegionUtils::JsonValueToComparableString(MakeShared<FJsonValueObject>(*SecondTree));
	TestEqual(TEXT("duplicate display name ids are stable across extracts"), SecondTreeJson, FirstTreeJson);

	TSharedPtr<FJsonObject> DiffBody = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		*FirstTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult DiffResult = Service.Diff(MakeDiffRequest(MakeBehaviorTreeDocument(Target, DiffBody)));
	TestTrue(TEXT("diff with extracted duplicate-display tree succeeds"), DiffResult.IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeDynamicNodeClassesTest,
	"AssetFactory.AssetDocument.BehaviorTree.DynamicNodeClasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeDynamicNodeClassesTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_DynamicClasses");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, true, true))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBTCompositeNode* Root = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	TestTrue(TEXT("short selector class resolved dynamically"), Root && Root->GetClass()->GetPathName() == TEXT("/Script/AIModule.BTComposite_Selector"));
	TestTrue(TEXT("short service class resolved dynamically"), Root && Root->Services.Num() == 1 && Root->Services[0]->GetClass()->IsChildOf(UBTService_DefaultFocus::StaticClass()));
	TestTrue(TEXT("short edge decorator class resolved dynamically"), Root && Root->Children.Num() > 0 && Root->Children[0].Decorators.Num() == 1 && Root->Children[0].Decorators[0]->GetClass()->IsChildOf(UBTDecorator_Blackboard::StaticClass()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDocumentBehaviorTreeApplyFailureDoesNotMutateExistingTest,
	"AssetFactory.AssetDocument.BehaviorTree.ApplyFailureDoesNotMutateExisting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetDocumentBehaviorTreeApplyFailureDoesNotMutateExistingTest::RunTest(const FString&)
{
	const FString Target = TEXT("/Game/AssetDocumentTests/BT_AD_Task7_FailureAtomicity");
	FAssetDocumentService Service;
	if (!ApplyTask7TreeFixture(*this, Service, Target, false))
	{
		return false;
	}

	UBehaviorTree* BehaviorTree = LoadBehaviorTreeForTarget(Target);
	UBTCompositeNode* OriginalRoot = BehaviorTree ? BehaviorTree->RootNode : nullptr;
	const int32 OriginalChildren = OriginalRoot ? OriginalRoot->Children.Num() : -1;
	const int32 OriginalServices = OriginalRoot ? OriginalRoot->Services.Num() : -1;
	const int32 OriginalDecorators = OriginalRoot && OriginalRoot->Children.Num() > 0 ? OriginalRoot->Children[0].Decorators.Num() : -1;
	UBlackboardData* OriginalBlackboard = BehaviorTree ? BehaviorTree->BlackboardAsset : nullptr;
	TestNotNull(TEXT("original root exists"), OriginalRoot);

	TSharedPtr<FJsonObject> InvalidTree = MakeTask7BehaviorTree(Target + TEXT("_Subtree"), false, false);
	TSharedPtr<FJsonObject> InvalidRoot = GetObjectField(InvalidTree, TEXT("Root"));
	if (InvalidRoot.IsValid())
	{
		InvalidRoot->SetStringField(TEXT("Class"), TEXT("/Script/AIModule.BTTask_MoveTo"));
	}

	TSharedPtr<FJsonObject> Body = MakeBehaviorTreeBody(
		MakeAssetRef(BehaviorTree->BlackboardAsset->GetPathName()),
		InvalidTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult FailedApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, Body)));
	TestFalse(TEXT("invalid root task apply fails"), FailedApply.IsSuccess());

	TestTrue(TEXT("RootNode pointer survives failed apply"), BehaviorTree->RootNode == OriginalRoot);
	TestEqual(TEXT("existing children survive failed apply"), OriginalRoot ? OriginalRoot->Children.Num() : -1, OriginalChildren);

	TSharedPtr<FJsonObject> LateInvalidTree = MakeTask7BehaviorTree(Target + TEXT("_Subtree"), false, false);
	TSharedPtr<FJsonObject> LateInvalidRoot = GetObjectField(LateInvalidTree, TEXT("Root"));
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (LateInvalidRoot.IsValid() && LateInvalidRoot->TryGetArrayField(TEXT("Children"), Children) && Children && Children->Num() > 1)
	{
		TSharedPtr<FJsonObject> SecondEdge = GetObjectFromValue((*Children)[1]);
		TSharedPtr<FJsonObject> SecondChild = GetObjectField(SecondEdge, TEXT("Child"));
		TSharedPtr<FJsonObject> Properties = GetObjectField(SecondChild, TEXT("Properties"));
		if (!Properties.IsValid())
		{
			Properties = MakeShared<FJsonObject>();
			SecondChild->SetObjectField(TEXT("Properties"), Properties);
		}
		Properties->SetStringField(TEXT("ParentNode"), TEXT("invalid"));
	}

	TSharedPtr<FJsonObject> LateInvalidBody = MakeBehaviorTreeBody(
		nullptr,
		LateInvalidTree,
		MakeShared<FJsonObject>());
	const FAssetDocumentResult LateFailedApply = Service.Apply(MakeApplyRequest(MakeBehaviorTreeDocument(Target, LateInvalidBody)));
	TestFalse(TEXT("late reflected property apply fails"), LateFailedApply.IsSuccess());
	TestTrue(TEXT("RootNode pointer survives late failed apply"), BehaviorTree->RootNode == OriginalRoot);
	TestEqual(TEXT("children survive late failed apply"), OriginalRoot ? OriginalRoot->Children.Num() : -1, OriginalChildren);
	TestEqual(TEXT("services survive late failed apply"), OriginalRoot ? OriginalRoot->Services.Num() : -1, OriginalServices);
	TestEqual(TEXT("decorators survive late failed apply"), OriginalRoot && OriginalRoot->Children.Num() > 0 ? OriginalRoot->Children[0].Decorators.Num() : -1, OriginalDecorators);
	TestTrue(TEXT("BlackboardAsset survives late failed apply"), BehaviorTree->BlackboardAsset == OriginalBlackboard);
	return true;
}

#endif
