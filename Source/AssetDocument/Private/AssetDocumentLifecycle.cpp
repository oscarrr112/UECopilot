// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentLifecycle.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/GarbageCollection.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

bool FAssetDocumentLifecycle::TryParseAction(const FString& ActionName, EAssetDocumentLifecycleAction& OutAction, FString& OutError)
{
	if (ActionName == TEXT("Create"))
	{
		OutAction = EAssetDocumentLifecycleAction::Create;
		return true;
	}

	if (ActionName == TEXT("Update"))
	{
		OutAction = EAssetDocumentLifecycleAction::Update;
		return true;
	}

	if (ActionName == TEXT("CreateOrUpdate"))
	{
		OutAction = EAssetDocumentLifecycleAction::CreateOrUpdate;
		return true;
	}

	OutError = FString::Printf(TEXT("Unsupported Action '%s'"), *ActionName);
	return false;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateOrLoad(const FString& Target, UClass* Class, EAssetDocumentLifecycleAction Action, TSharedPtr<FJsonObject> Document)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);

	UObject* ExistingAsset = FindObject<UObject>(nullptr, *Result.ObjectPath);
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(Target, FPackageName::GetAssetPackageExtension());
	if (!ExistingAsset && IFileManager::Get().FileExists(*PackageFileName))
	{
		ExistingAsset = LoadObject<UObject>(nullptr, *Result.ObjectPath);
	}

	if (ExistingAsset && Class == UBlueprint::StaticClass() && ExistingAsset->GetClass() != UBlueprint::StaticClass())
	{
		Result.Error = FString::Printf(TEXT("Existing asset '%s' is not an exact UBlueprint asset"), *Result.ObjectPath);
		return Result;
	}

	if (ExistingAsset && Class == UWidgetBlueprint::StaticClass() && ExistingAsset->GetClass() != UWidgetBlueprint::StaticClass())
	{
		Result.Error = FString::Printf(TEXT("Existing asset '%s' is not an exact UWidgetBlueprint asset"), *Result.ObjectPath);
		return Result;
	}

	if (ExistingAsset && Class != UBlueprint::StaticClass() && !ExistingAsset->IsA(Class))
	{
		Result.Error = FString::Printf(TEXT("Existing asset '%s' is not a '%s'"), *Result.ObjectPath, *Class->GetName());
		return Result;
	}

	if (Action == EAssetDocumentLifecycleAction::Create && ExistingAsset)
	{
		Result.Error = FString::Printf(TEXT("Create failed because target '%s' already exists"), *Target);
		return Result;
	}

	if (Action == EAssetDocumentLifecycleAction::Update && !ExistingAsset)
	{
		Result.Error = FString::Printf(TEXT("Update failed because target '%s' does not exist"), *Target);
		return Result;
	}

	if (ExistingAsset)
	{
		Result.Asset = ExistingAsset;
		return Result;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(Target);
	if (AssetName.IsEmpty())
	{
		Result.Error = FString::Printf(TEXT("Invalid target '%s'"), *Target);
		return Result;
	}

	UPackage* Package = CreatePackage(*Target);
	if (!Package)
	{
		Result.Error = FString::Printf(TEXT("Failed to create package '%s'"), *Target);
		return Result;
	}

	if (Class == UBlueprint::StaticClass())
	{
		return CreateBlueprintAsset(Target, Package, AssetName, Document);
	}

	if (Class == UWidgetBlueprint::StaticClass())
	{
		return CreateWidgetBlueprintAsset(Target, Package, AssetName, Document);
	}

	UObject* NewAsset = NewObject<UObject>(Package, Class, *AssetName, RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		Result.Error = FString::Printf(TEXT("Failed to create asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	FAssetRegistryModule::AssetCreated(NewAsset);

	Result.Asset = NewAsset;
	Result.bCreated = true;
	return Result;
}

void FAssetDocumentLifecycle::CleanupCreatedAsset(const FAssetDocumentLifecycleResult& LifecycleResult)
{
	if (!LifecycleResult.bCreated || !LifecycleResult.Asset)
	{
		return;
	}

	UObject* Asset = LifecycleResult.Asset;
	UPackage* Package = Asset->GetOutermost();

	FAssetRegistryModule::AssetDeleted(Asset);
	Asset->ClearFlags(RF_Public | RF_Standalone);
	Asset->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	Asset->MarkAsGarbage();

	if (Package)
	{
		Package->ClearDirtyFlag();
		Package->MarkAsGarbage();
	}

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
}

FString FAssetDocumentLifecycle::MakeObjectPath(const FString& Target)
{
	return FString::Printf(TEXT("%s.%s"), *Target, *FPackageName::GetLongPackageAssetName(Target));
}

bool FAssetDocumentLifecycle::TryResolveBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError)
{
	OutParentClass = nullptr;

	if (!Document.IsValid())
	{
		OutError = TEXT("UBlueprint creation requires Body.ParentClass");
		return false;
	}

	const TSharedPtr<FJsonObject>* Body = nullptr;
	if (!Document->TryGetObjectField(TEXT("Body"), Body) || !Body || !Body->IsValid())
	{
		OutError = TEXT("UBlueprint creation requires Body.ParentClass");
		return false;
	}

	const TSharedPtr<FJsonObject>* ParentClass = nullptr;
	if (!(*Body)->TryGetObjectField(TEXT("ParentClass"), ParentClass) || !ParentClass || !ParentClass->IsValid())
	{
		OutError = TEXT("UBlueprint creation requires Body.ParentClass");
		return false;
	}

	FString Kind;
	if (!(*ParentClass)->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("ClassRef"))
	{
		OutError = TEXT("Body.ParentClass.Kind must be ClassRef");
		return false;
	}

	FString ParentClassPath;
	if (!(*ParentClass)->TryGetStringField(TEXT("Class"), ParentClassPath) || ParentClassPath.IsEmpty())
	{
		OutError = TEXT("Body.ParentClass.Class is required");
		return false;
	}

	OutParentClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ParentClassPath);
	if (!OutParentClass)
	{
		OutError = FString::Printf(TEXT("Failed to resolve Body.ParentClass.Class '%s'"), *ParentClassPath);
		return false;
	}

	if (!OutParentClass->IsChildOf(UObject::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is not a UObject class"), *OutParentClass->GetName());
		return false;
	}

	if (OutParentClass->HasAnyClassFlags(CLASS_Abstract))
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is abstract"), *OutParentClass->GetName());
		return false;
	}

	return true;
}

bool TryReadLifecycleParentClassRef(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError, const TCHAR* AssetClassName)
{
	OutParentClass = nullptr;

	if (!Document.IsValid())
	{
		OutError = FString::Printf(TEXT("%s creation requires Body.ParentClass"), AssetClassName);
		return false;
	}

	const TSharedPtr<FJsonObject>* Body = nullptr;
	if (!Document->TryGetObjectField(TEXT("Body"), Body) || !Body || !Body->IsValid())
	{
		OutError = FString::Printf(TEXT("%s creation requires Body.ParentClass"), AssetClassName);
		return false;
	}

	const TSharedPtr<FJsonObject>* ParentClass = nullptr;
	if (!(*Body)->TryGetObjectField(TEXT("ParentClass"), ParentClass) || !ParentClass || !ParentClass->IsValid())
	{
		OutError = FString::Printf(TEXT("%s creation requires Body.ParentClass"), AssetClassName);
		return false;
	}

	FString Kind;
	if (!(*ParentClass)->TryGetStringField(TEXT("Kind"), Kind) || Kind != TEXT("ClassRef"))
	{
		OutError = TEXT("Body.ParentClass.Kind must be ClassRef");
		return false;
	}

	FString ParentClassPath;
	if (!(*ParentClass)->TryGetStringField(TEXT("Class"), ParentClassPath) || ParentClassPath.IsEmpty())
	{
		OutError = TEXT("Body.ParentClass.Class is required");
		return false;
	}

	OutParentClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ParentClassPath);
	if (!OutParentClass)
	{
		OutError = FString::Printf(TEXT("Failed to resolve Body.ParentClass.Class '%s'"), *ParentClassPath);
		return false;
	}

	return true;
}

bool FAssetDocumentLifecycle::TryResolveWidgetBlueprintParentClass(const TSharedPtr<FJsonObject>& Document, UClass*& OutParentClass, FString& OutError)
{
	if (!TryReadLifecycleParentClassRef(Document, OutParentClass, OutError, TEXT("WidgetBlueprint")))
	{
		return false;
	}

	if (!OutParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is not a UUserWidget subclass"), *OutParentClass->GetName());
		return false;
	}

	if (OutParentClass->HasAnyClassFlags(CLASS_Abstract) && OutParentClass != UUserWidget::StaticClass())
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is abstract"), *OutParentClass->GetName());
		return false;
	}

	if (OutParentClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		OutError = FString::Printf(TEXT("Body.ParentClass.Class '%s' is deprecated or newer-version-only"), *OutParentClass->GetName());
		return false;
	}

	return true;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);

	UClass* ParentClass = nullptr;
	if (!TryResolveBlueprintParentClass(Document, ParentClass, Result.Error))
	{
		return Result;
	}

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		*AssetName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());
	if (!Blueprint)
	{
		Result.Error = FString::Printf(TEXT("Failed to create UBlueprint asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (Blueprint->Status == BS_Error)
	{
		Result.Asset = Blueprint;
		Result.bCreated = true;
		Result.Error = FString::Printf(TEXT("Failed to compile UBlueprint asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	FAssetRegistryModule::AssetCreated(Blueprint);

	Result.Asset = Blueprint;
	Result.bCreated = true;
	return Result;
}

FAssetDocumentLifecycleResult FAssetDocumentLifecycle::CreateWidgetBlueprintAsset(const FString& Target, UPackage* Package, const FString& AssetName, const TSharedPtr<FJsonObject>& Document)
{
	FAssetDocumentLifecycleResult Result;
	Result.ObjectPath = MakeObjectPath(Target);

	UClass* ParentClass = nullptr;
	if (!TryResolveWidgetBlueprintParentClass(Document, ParentClass, Result.Error))
	{
		return Result;
	}

	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->ParentClass = ParentClass;

	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(
		UWidgetBlueprint::StaticClass(),
		Package,
		*AssetName,
		RF_Public | RF_Standalone,
		nullptr,
		GWarn));
	if (!WidgetBlueprint)
	{
		Result.Error = FString::Printf(TEXT("Failed to create WidgetBlueprint asset '%s'"), *Result.ObjectPath);
		return Result;
	}

#if WITH_EDITORONLY_DATA
	if (WidgetBlueprint->WidgetTree)
	{
		WidgetBlueprint->WidgetTree->RootWidget = nullptr;
		WidgetBlueprint->WidgetTree->NamedSlotBindings.Empty();
	}
	WidgetBlueprint->Bindings.Empty();
	WidgetBlueprint->Animations.Empty();
#endif

	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
	if (WidgetBlueprint->Status == BS_Error)
	{
		Result.Asset = WidgetBlueprint;
		Result.bCreated = true;
		Result.Error = FString::Printf(TEXT("Failed to compile WidgetBlueprint asset '%s'"), *Result.ObjectPath);
		return Result;
	}

	FAssetRegistryModule::AssetCreated(WidgetBlueprint);

	Result.Asset = WidgetBlueprint;
	Result.bCreated = true;
	return Result;
}
