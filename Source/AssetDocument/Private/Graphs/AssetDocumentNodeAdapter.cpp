// Copyright ProjectRPG. All Rights Reserved.

#include "Graphs/AssetDocumentNodeAdapter.h"

#include "UObject/Class.h"

namespace
{
FString GetClassPathName(UClass* Class)
{
	return Class ? Class->GetPathName() : FString();
}
}

TSharedRef<FJsonObject> FAssetDocumentUnsupportedNodeDiagnostic::ToJsonObject() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("Code"), Code);
	Object->SetStringField(TEXT("Path"), Path);
	Object->SetStringField(TEXT("Class"), Class);
	Object->SetStringField(TEXT("Capability"), Capability);
	if (Member.IsValid())
	{
		Object->SetObjectField(TEXT("Member"), Member);
	}
	else
	{
		Object->SetField(TEXT("Member"), MakeShared<FJsonValueNull>());
	}
	Object->SetStringField(TEXT("Reason"), Reason);
	Object->SetStringField(TEXT("SuggestedAction"), SuggestedAction);
	return Object;
}

void FAssetDocumentNodeAdapterRegistry::RegisterAdapter(const FString& ClassPath, TSharedRef<IAssetDocumentNodeAdapter> Adapter)
{
	if (!ClassPath.IsEmpty())
	{
		AdaptersByClassPath.Add(ClassPath, Adapter);
	}
}

void FAssetDocumentNodeAdapterRegistry::RegisterAdapter(UClass* Class, TSharedRef<IAssetDocumentNodeAdapter> Adapter)
{
	RegisterAdapter(GetClassPathName(Class), Adapter);
}

TSharedPtr<IAssetDocumentNodeAdapter> FAssetDocumentNodeAdapterRegistry::FindAdapter(const FString& ClassPath) const
{
	if (const TSharedPtr<IAssetDocumentNodeAdapter>* Adapter = AdaptersByClassPath.Find(ClassPath))
	{
		return *Adapter;
	}
	return nullptr;
}

TSharedPtr<IAssetDocumentNodeAdapter> FAssetDocumentNodeAdapterRegistry::FindAdapter(UClass* Class) const
{
	return FindAdapter(GetClassPathName(Class));
}

void FAssetDocumentNodeAdapterRegistry::Reset()
{
	AdaptersByClassPath.Reset();
}
