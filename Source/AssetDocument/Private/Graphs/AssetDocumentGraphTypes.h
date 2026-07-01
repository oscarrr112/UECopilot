// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct FAssetDocumentGraphParseOptions
{
	FString Path = TEXT("");
	bool bRejectUnknownGraphFields = true;
};

struct FAssetDocumentGraphDiagnostic
{
	FString Code;
	FString Path;
	FString Message;
};

struct FAssetDocumentGraphEndpoint
{
	FString Node;
	FString Pin;

	FString ToKey() const;
	TSharedRef<FJsonObject> ToJsonObject() const;
};

struct FAssetDocumentLinkSpec
{
	FAssetDocumentGraphEndpoint From;
	FAssetDocumentGraphEndpoint To;

	FString ToKey() const;
	TSharedRef<FJsonObject> ToJsonObject() const;
};

struct FAssetDocumentMemberRef
{
	FString Kind;
	FString OwnerClass;
	FString Name;
	FString Guid;
	bool bHasSelfContext = false;
	bool bSelfContext = false;

	TSharedRef<FJsonObject> ToJsonObject() const;
};

struct FAssetDocumentPinOverrideSpec
{
	FString Pin;
	FString Direction;
	TSharedPtr<FJsonValue> Type;
	TSharedPtr<FJsonValue> DefaultValue;
	TSharedPtr<FJsonValue> DefaultObject;
	TSharedPtr<FJsonValue> DefaultTextValue;
	TOptional<bool> Hidden;
	TOptional<bool> AdvancedView;

	TSharedRef<FJsonObject> ToJsonObject() const;
};

struct FAssetDocumentNodeSpec
{
	FString Id;
	FString NodeGuid;
	FString Class;
	FString Capability;
	FString Kind;
	TSharedPtr<FJsonObject> Spawner;
	TSharedPtr<FJsonObject> Fields;
	TSharedPtr<FJsonObject> Pins;
	TSharedPtr<FJsonObject> Member;
	TArray<FAssetDocumentPinOverrideSpec> PinOverrides;
	TSharedPtr<FJsonObject> Position;
	TSharedPtr<FJsonObject> SubgraphRefs;
	TSharedPtr<FJsonObject> Evidence;
	FString Comment;
	bool bHasComment = false;

	TSharedRef<FJsonObject> ToJsonObject() const;
};

struct FAssetDocumentGraphSpec
{
	FString Id;
	FString Kind;
	TSharedPtr<FJsonObject> Owner;
	FString OwnerNodeId;
	FString OwnerPin;
	FString Name;
	FString Schema;
	FString GraphGuid;
	FString Category;
	FString Description;
	TSharedPtr<FJsonObject> Signature;
	TSharedPtr<FJsonValue> EntryPins;
	TSharedPtr<FJsonValue> ResultPins;
	TSharedPtr<FJsonObject> Position;
	TSharedPtr<FJsonValue> Metadata;
	TSharedPtr<FJsonValue> Diagnostics;
	TSharedPtr<FJsonValue> Skipped;
	TSharedPtr<FJsonValue> UnderscoreSkipped;
	TSharedPtr<FJsonObject> Evidence;
	TArray<FAssetDocumentNodeSpec> Nodes;
	TArray<FAssetDocumentLinkSpec> Links;
	TArray<FAssetDocumentGraphSpec> Subgraphs;

	TSharedRef<FJsonObject> ToJsonObject() const;
};

struct FAssetDocumentGraphParseResult
{
	TArray<FAssetDocumentGraphSpec> Graphs;
	TArray<FAssetDocumentGraphDiagnostic> Diagnostics;

	bool IsValid() const { return Diagnostics.IsEmpty(); }
	void AddDiagnostic(const FString& Code, const FString& Path, const FString& Message);
};

namespace AssetDocumentGraphJson
{
	TSharedPtr<FJsonValue> CloneJsonValue(const TSharedPtr<FJsonValue>& Value);
	TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Object);
	bool AreJsonValuesEqual(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right);
	bool AreJsonObjectsEqual(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right);
}
