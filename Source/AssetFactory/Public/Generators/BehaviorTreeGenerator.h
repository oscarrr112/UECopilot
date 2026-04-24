// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

class UBehaviorTree;
class UBlackboardData;
class UBTNode;
class UBTCompositeNode;

/**
 * Generator for UBehaviorTree assets. See spec for full JSON schema.
 *
 * Priority: 30 (after BB at 10).
 */
class ASSETFACTORY_API FBehaviorTreeGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("BehaviorTree"); }
	virtual int32 GetPriority() const override { return 30; }

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action = EGenerationAction::Create) const override;
	virtual TArray<FString> GetRequiredFields() const override { return { TEXT("Root") }; }

	virtual bool CanExtract(UObject* Asset) const override;
	virtual TSharedPtr<FJsonObject> Extract(UObject* Asset, bool bDiffOnly = false) const override;

private:
	/** Resolve a node class name (exact / fully-qualified) to a UBTNode subclass. nullptr = not found. */
	UClass* ResolveNodeClass(const FString& NodeName) const;

	/** Resolve "Blackboard" path or "BlackboardInline" object to a usable UBlackboardData. */
	UBlackboardData* ResolveBlackboard(TSharedPtr<FJsonObject> Config, const FString& BTName, const FString& BTPath);

	/** Recursively build a UBTNode tree from JSON. Parent==nullptr means root. */
	UBTNode* BuildNode(TSharedPtr<FJsonObject> NodeJson, UBehaviorTree* OuterBT, UBTCompositeNode* Parent, TArray<FString>& OutWarnings);

	void AttachDecorators(UBTNode* Target, const TArray<TSharedPtr<FJsonValue>>* DecoArray, UBehaviorTree* OuterBT, TArray<FString>& OutWarnings);
	void AttachServices(UBTCompositeNode* Composite, const TArray<TSharedPtr<FJsonValue>>* SvcArray, UBehaviorTree* OuterBT, TArray<FString>& OutWarnings);

	/** §8.1: ensure BT is editor-loadable. Implementation per Phase 0 research. */
	void FinalizeBT(UBehaviorTree* BT);

	/** Recursive extract; passes accumulating NodeIndex if available. */
	TSharedPtr<FJsonObject> ExtractNode(const UBTNode* Node, bool bDiffOnly) const;

	/** §6.3: discover FBlackboardKeySelector properties via reflection and cross-check against AllowedKeys. */
	TOptional<FString> ValidateBBKeyReferences(TSharedPtr<FJsonObject> NodeJson, const TSet<FName>& AllowedKeys, int32& InOutFakeIndex) const;

	/** §6.5: discover BehaviorTree object references via reflection and validate subtree blackboard compatibility. */
	TOptional<FString> ValidateBTAssetReferences(TSharedPtr<FJsonObject> NodeJson, const UBlackboardData* ParentBlackboard, int32& InOutFakeIndex, bool bValidateBlackboardCompatibility = true) const;
};
