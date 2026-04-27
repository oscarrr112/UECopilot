// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetGenerator.h"

class UStateTree;
class UStateTreeEditorData;
class UStateTreeSchema;
struct FStateTreeCompilerLog;

/**
 * Core lifecycle generator for UStateTree assets.
 *
 * This first slice creates editor data, compiles through the official compiler,
 * and extracts a skeleton. Node construction, transitions, parameters, and
 * bindings are intentionally handled by future specs.
 */
class ASSETFACTORY_API FStateTreeGenerator : public IAssetGenerator
{
public:
	virtual FString GetAssetType() const override { return TEXT("StateTree"); }
	virtual int32 GetPriority() const override { return 40; }

	virtual FGenerationResult Generate(
		const FString& Name,
		const FString& Path,
		EGenerationAction Action,
		TSharedPtr<FJsonObject> Config
	) override;

	virtual TOptional<FString> ValidateConfig(TSharedPtr<FJsonObject> Config, EGenerationAction Action = EGenerationAction::Create) const override;
	virtual TArray<FString> GetRequiredFields() const override { return { TEXT("SchemaClass") }; }

	virtual bool CanExtract(UObject* Asset) const override;
	virtual TSharedPtr<FJsonObject> Extract(UObject* Asset, bool bDiffOnly = false) const override;

private:
	UClass* ResolveSchemaClass(const FString& SchemaClassName) const;
	TOptional<FString> ValidateSchemaClass(const FString& SchemaClassName) const;
	TOptional<FString> ValidateUpdateSchema(UStateTree* ExistingTree, UClass* RequestedSchemaClass) const;

	UStateTree* CreateStateTreeAsset(const FString& Name, UPackage* Package, UClass* SchemaClass) const;
	UStateTreeEditorData* GetEditorData(UStateTree* StateTree) const;
	UStateTreeSchema* GetEditorSchemaInstance(UStateTree* StateTree) const;

	bool ApplySchemaProperties(UStateTreeSchema* Schema, TSharedPtr<FJsonObject> Config, FString& OutError) const;
	bool CompileStateTree(UStateTree* StateTree, FString& OutError) const;
	FString FormatCompilerLog(const FStateTreeCompilerLog& Log) const;
	bool SaveStateTreePackage(UStateTree* StateTree, UPackage* Package, const FString& Name, const FString& Path, FString& OutError) const;

	TSharedPtr<FJsonObject> ExtractSchemaProperties(const UStateTreeSchema* Schema, bool bDiffOnly) const;
	TArray<TSharedPtr<FJsonValue>> ExtractSubTreesSkeleton(const UStateTreeEditorData* EditorData) const;
};
