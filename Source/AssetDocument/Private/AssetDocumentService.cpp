// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentService.h"

#include "AssetDocumentClassResolver.h"
#include "AssetDocumentLifecycle.h"
#include "AssetDocumentProfileRegistry.h"
#include "AssetDocumentPropertyAdapter.h"
#include "AssetDocumentSidecar.h"

#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"

#include <initializer_list>

namespace
{
FString NormalizeValidateFilePath(const FString& FilePath)
{
	if (FilePath.IsEmpty())
	{
		return FString();
	}

	FString NormalizedPath = FilePath;
	FPaths::NormalizeFilename(NormalizedPath);
	if (FPaths::IsRelative(NormalizedPath) && NormalizedPath.StartsWith(TEXT("Content/"), ESearchCase::IgnoreCase))
	{
		NormalizedPath = FPaths::Combine(FPaths::ProjectDir(), NormalizedPath);
	}

	NormalizedPath = FPaths::ConvertRelativePathToFull(NormalizedPath);
	FPaths::NormalizeFilename(NormalizedPath);
	return NormalizedPath;
}

FString NormalizeValidateTarget(const FString& Target)
{
	FString NormalizedTarget = Target;
	FPaths::NormalizeFilename(NormalizedTarget);
	NormalizedTarget.TrimStartAndEndInline();

	FString PackagePath;
	FString ObjectName;
	if (NormalizedTarget.Split(TEXT("."), &PackagePath, &ObjectName))
	{
		NormalizedTarget = PackagePath;
	}

	return NormalizedTarget;
}

bool ValidateApplyTarget(const FString& Target, FString& OutError)
{
	if (Target.IsEmpty())
	{
		OutError = TEXT("Target is required");
		return false;
	}

	if (!Target.StartsWith(TEXT("/Game/")))
	{
		OutError = FString::Printf(TEXT("Target '%s' must be a long package name under /Game"), *Target);
		return false;
	}

	FText PackageNameReason;
	if (!FPackageName::IsValidLongPackageName(Target, false, &PackageNameReason))
	{
		OutError = FString::Printf(TEXT("Target '%s' is not a valid long package name: %s"), *Target, *PackageNameReason.ToString());
		return false;
	}

	if (FPackageName::GetLongPackageAssetName(Target).IsEmpty())
	{
		OutError = FString::Printf(TEXT("Target '%s' must include a non-empty asset name"), *Target);
		return false;
	}

	return true;
}

FString ToObjectPath(const FString& PackageOrObjectPath)
{
	if (PackageOrObjectPath.Contains(TEXT(".")))
	{
		return PackageOrObjectPath;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageOrObjectPath);
	if (AssetName.IsEmpty())
	{
		return PackageOrObjectPath;
	}

	return FString::Printf(TEXT("%s.%s"), *PackageOrObjectPath, *AssetName);
}

UObject* LoadAssetFromPackageOrObjectPath(const FString& PackageOrObjectPath)
{
	if (PackageOrObjectPath.IsEmpty())
	{
		return nullptr;
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *ToObjectPath(PackageOrObjectPath));
	if (!Asset && PackageOrObjectPath.Contains(TEXT(".")))
	{
		Asset = LoadObject<UObject>(nullptr, *PackageOrObjectPath);
	}
	return Asset;
}

struct FAssetDocumentResolvedTarget
{
	UObject* Asset = nullptr;
	UClass* Class = nullptr;
	FString Target;
	FString AssetPath;
};

FAssetDocumentResult ResolveClassOrAssetTarget(const FString& ClassOrAsset, const FString& OperationName, FAssetDocumentResolvedTarget& OutTarget)
{
	if (ClassOrAsset.IsEmpty())
	{
		return FAssetDocumentResult::Failure(FString::Printf(TEXT("%s requires a class name or asset path"), *OperationName));
	}

	OutTarget.Target = ClassOrAsset;

	if (ClassOrAsset.StartsWith(TEXT("/Game/")))
	{
		OutTarget.Asset = LoadAssetFromPackageOrObjectPath(ClassOrAsset);
		if (!OutTarget.Asset)
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to load asset '%s'"), *ClassOrAsset));
			Result.Target = ClassOrAsset;
			Result.AssetPath = ToObjectPath(ClassOrAsset);
			return Result;
		}

		OutTarget.Class = OutTarget.Asset->GetClass();
		OutTarget.Target = NormalizeValidateTarget(ClassOrAsset);
		OutTarget.AssetPath = OutTarget.Asset->GetPathName();
		return FAssetDocumentResult::Success(TEXT("Resolved class or asset target"));
	}

	FString Error;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassOrAsset, OutTarget.Class, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = ClassOrAsset;
		return Result;
	}

	return FAssetDocumentResult::Success(TEXT("Resolved class or asset target"));
}

FAssetDocumentResult ResolveClassTarget(const FString& ClassName, UClass*& OutClass)
{
	if (ClassName.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("CreateTemplate requires a class"));
	}

	FString Error;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassName, OutClass, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = ClassName;
		return Result;
	}

	return FAssetDocumentResult::Success(TEXT("Resolved class target"));
}

TArray<TSharedPtr<FJsonValue>> MakeStringArray(std::initializer_list<const TCHAR*> Values)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const TCHAR* Value : Values)
	{
		Result.Add(MakeShared<FJsonValueString>(Value));
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> MakeFragmentKindArray()
{
	return MakeStringArray({
		TEXT("AssetRef"),
		TEXT("ClassRef"),
		TEXT("StructValue"),
		TEXT("EmbeddedObject"),
		TEXT("DefinitionRef"),
	});
}

TSharedRef<FJsonObject> MakeGenericDocumentShape()
{
	TSharedRef<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("Definitions"), TEXT("map<string, Fragment>"));
	Shape->SetStringField(TEXT("Properties"), TEXT("reflected CDO-diff properties"));
	Shape->SetObjectField(TEXT("Body"), MakeShared<FJsonObject>());
	return Shape;
}

struct FAssetDocumentProfileResolution
{
	UClass* Class = nullptr;
	TSharedPtr<IAssetDocumentProfile> ExactProfile;
};

FAssetDocumentProfileResolution ResolveAssetDocumentProfile(const FAssetDocumentProfileRegistry& Registry, UClass* Class)
{
	FAssetDocumentProfileResolution Resolution;
	Resolution.Class = Class;
	Resolution.ExactProfile = Registry.FindForClass(Class);
	return Resolution;
}

TSharedRef<FJsonObject> MakeGenericProfilePayload(const FAssetDocumentProfileResolution& Resolution)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("Class"), Resolution.Class ? Resolution.Class->GetPathName() : FString());
	Payload->SetObjectField(TEXT("DocumentShape"), MakeGenericDocumentShape());
	Payload->SetArrayField(TEXT("BodySections"), TArray<TSharedPtr<FJsonValue>>());
	Payload->SetArrayField(TEXT("FragmentKinds"), MakeFragmentKindArray());
	return Payload;
}

TSharedRef<FJsonObject> MakeExactProfilePayload(const FAssetDocumentProfileResolution& Resolution)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("Class"), Resolution.Class ? Resolution.Class->GetPathName() : FString());
	Payload->SetObjectField(TEXT("DocumentShape"), Resolution.ExactProfile->GetDocumentShape());

	TArray<TSharedPtr<FJsonValue>> BodySections;
	for (const FName& BodyKey : Resolution.ExactProfile->GetBodyKeys())
	{
		BodySections.Add(MakeShared<FJsonValueString>(BodyKey.ToString()));
	}
	Payload->SetArrayField(TEXT("BodySections"), BodySections);
	Payload->SetArrayField(TEXT("FragmentKinds"), MakeFragmentKindArray());
	return Payload;
}

TArray<TSharedPtr<FJsonValue>> MakeRegisteredProfileArray(const FAssetDocumentProfileRegistry& Registry)
{
	TArray<TSharedPtr<FJsonValue>> RegisteredProfiles;
	for (const TSharedRef<IAssetDocumentProfile>& Profile : Registry.GetAllProfiles())
	{
		UClass* ExactClass = Profile->GetExactClass();
		if (!ExactClass)
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Class"), ExactClass->GetPathName());

		TArray<TSharedPtr<FJsonValue>> BodySections;
		for (const FName& BodyKey : Profile->GetBodyKeys())
		{
			BodySections.Add(MakeShared<FJsonValueString>(BodyKey.ToString()));
		}
		Entry->SetArrayField(TEXT("BodySections"), BodySections);
		RegisteredProfiles.Add(MakeShared<FJsonValueObject>(Entry));
	}
	return RegisteredProfiles;
}

FString JsonValueToComparableString(TSharedPtr<FJsonValue> Value)
{
	TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
	Wrapper->SetField(TEXT("value"), Value.IsValid() ? Value : MakeShared<FJsonValueNull>());

	FString JsonText;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);
	return JsonText;
}

void AddNamedValueEntry(TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Name, TSharedPtr<FJsonValue> Value)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Name);
	Entry->SetField(TEXT("value"), Value.IsValid() ? Value : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

void AddChangedEntry(TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Name, TSharedPtr<FJsonValue> Before, TSharedPtr<FJsonValue> After)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Name);
	Entry->SetField(TEXT("before"), Before.IsValid() ? Before : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("after"), After.IsValid() ? After : MakeShared<FJsonValueNull>());
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

void AddReasonEntry(TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Name, const FString& Code, const FString& Message)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Name);
	Entry->SetStringField(TEXT("code"), Code);
	Entry->SetStringField(TEXT("message"), Message);
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
}

FAssetDocumentResult MakeCapabilityValidationFailure(const FAssetDocumentCapabilityResult& CapabilityResult, const FString& Target, const FString& NormalizedFilePath)
{
	FAssetDocumentResult Result = FAssetDocumentResult::Failure(CapabilityResult.Message);
	Result.Target = Target;
	Result.SidecarFilePath = NormalizedFilePath;
	Result.Diagnostics = CapabilityResult.Diagnostics;
	if (CapabilityResult.Payload.IsValid())
	{
		Result.Payload = CapabilityResult.Payload;
	}
	return Result;
}

FAssetDocumentResult ValidateGenericAssetDocument(TSharedPtr<FJsonObject> Document, const FString& NormalizedFilePath, bool bPreflightProperties)
{
	if (!Document.IsValid())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Validate requires a JSON document or sidecar file path"));
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	FString Target;
	auto MakeFailure = [&Target, &NormalizedFilePath](const FString& Message)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Message);
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	};

	double SchemaVersion = 0.0;
	if (!Document->TryGetNumberField(TEXT("SchemaVersion"), SchemaVersion) || SchemaVersion != 1.0)
	{
		return MakeFailure(TEXT("SchemaVersion must be 1"));
	}

	const bool bHasStructuredShape = Document->HasField(TEXT("Body")) || Document->HasField(TEXT("Definitions"));
	FString AssetType;
	if (Document->TryGetStringField(TEXT("AssetType"), AssetType))
	{
		if (AssetType != TEXT("GenericAsset"))
		{
			return MakeFailure(TEXT("AssetType must be GenericAsset"));
		}
	}
	else if (!bHasStructuredShape)
	{
		return MakeFailure(TEXT("AssetType must be GenericAsset"));
	}

	if (!Document->TryGetStringField(TEXT("Target"), Target) || Target.IsEmpty())
	{
		return MakeFailure(TEXT("Target is required"));
	}
	Target = NormalizeValidateTarget(Target);

	FString Error;
	if (!ValidateApplyTarget(Target, Error))
	{
		return MakeFailure(Error);
	}

	if (!FAssetDocumentSidecar::ValidateTargetMatchesSidecar(NormalizedFilePath, Document, Error))
	{
		return MakeFailure(Error);
	}

	FString ClassName;
	if (!Document->TryGetStringField(TEXT("Class"), ClassName) || ClassName.IsEmpty())
	{
		return MakeFailure(TEXT("Class is required"));
	}

	FString ActionName;
	if (!Document->TryGetStringField(TEXT("Action"), ActionName) || ActionName.IsEmpty())
	{
		return MakeFailure(TEXT("Action is required"));
	}

	EAssetDocumentLifecycleAction Action;
	if (!FAssetDocumentLifecycle::TryParseAction(ActionName, Action, Error))
	{
		return MakeFailure(Error);
	}

	UClass* ResolvedClass = nullptr;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, Error))
	{
		return MakeFailure(Error);
	}

	const TSharedPtr<FJsonObject>* DefinitionsPtr = nullptr;
	if (Document->HasField(TEXT("Definitions")))
	{
		if (!Document->TryGetObjectField(TEXT("Definitions"), DefinitionsPtr) || !DefinitionsPtr)
		{
			return MakeFailure(TEXT("Definitions must be a JSON object"));
		}
	}

	TSharedPtr<FJsonObject> Properties;
	if (Document->HasField(TEXT("Properties")))
	{
		const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
		if (!Document->TryGetObjectField(TEXT("Properties"), PropertiesPtr) || !PropertiesPtr)
		{
			return MakeFailure(TEXT("Properties must be a JSON object"));
		}
		Properties = *PropertiesPtr;
	}

	if (Document->HasField(TEXT("Body")))
	{
		TSharedPtr<FJsonValue> BodyValue = Document->TryGetField(TEXT("Body"));
		if (!BodyValue.IsValid())
		{
			return MakeFailure(TEXT("Body is required when present"));
		}

		const FAssetDocumentProfileResolution ProfileResolution = ResolveAssetDocumentProfile(FAssetDocumentService::GetProfileRegistry(), ResolvedClass);
		if (!ProfileResolution.ExactProfile.IsValid())
		{
			FAssetDocumentResult Result = MakeFailure(FString::Printf(TEXT("Body is not supported for class '%s'"), *ResolvedClass->GetPathName()));
			FAssetDocumentDiagnostic Diagnostic;
			Diagnostic.Path = TEXT("/Body");
			Diagnostic.Code = TEXT("MissingProfile");
			Diagnostic.Message = Result.Message;
			Result.Diagnostics.Add(MoveTemp(Diagnostic));
			return Result;
		}

		const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile->ResolveBodyAdapter(TEXT("Body"));
		if (!BodyAdapter)
		{
			FAssetDocumentResult Result = MakeFailure(FString::Printf(TEXT("Profile for class '%s' does not provide Body validation"), *ResolvedClass->GetPathName()));
			FAssetDocumentDiagnostic Diagnostic;
			Diagnostic.Path = TEXT("/Body");
			Diagnostic.Code = TEXT("MissingBodyAdapter");
			Diagnostic.Message = Result.Message;
			Result.Diagnostics.Add(MoveTemp(Diagnostic));
			return Result;
		}

		FAssetDocumentCapabilityContext CapabilityContext;
		CapabilityContext.AssetClass = ResolvedClass;
		CapabilityContext.TargetAssetPath = Target;
		CapabilityContext.SourceDocumentPath = NormalizedFilePath;
		CapabilityContext.Definitions = DefinitionsPtr;
		CapabilityContext.bIsDryRun = true;

		const FAssetDocumentCapabilityResult CapabilityResult = BodyAdapter->Validate(CapabilityContext, BodyValue.ToSharedRef());
		if (!CapabilityResult.bSuccess)
		{
			return MakeCapabilityValidationFailure(CapabilityResult, Target, NormalizedFilePath);
		}
	}

	if (bPreflightProperties)
	{
		FAssetDocumentPropertyApplyResult PreflightResult = FAssetDocumentPropertyAdapter::PreflightProperties(ResolvedClass, Properties);
		if (!PreflightResult.bSuccess)
		{
			FAssetDocumentResult Result = MakeFailure(PreflightResult.Message);
			Result.Diagnostics = PreflightResult.Diagnostics;
			return Result;
		}
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument is valid"));
	Result.Target = Target;
	Result.SidecarFilePath = NormalizedFilePath;
	Result.Payload = MakeShared<FJsonObject>();
	Result.Payload->SetStringField(TEXT("target"), Target);
	Result.Payload->SetStringField(TEXT("sidecar_file_path"), NormalizedFilePath);
	return Result;
}
}

FAssetDocumentResult FAssetDocumentResult::Success(const FString& InMessage)
{
	FAssetDocumentResult Result;
	Result.Status = EAssetDocumentResultStatus::Success;
	Result.Message = InMessage;
	return Result;
}

FAssetDocumentResult FAssetDocumentResult::Failure(const FString& InMessage)
{
	FAssetDocumentResult Result;
	Result.Status = EAssetDocumentResultStatus::Failed;
	Result.Message = InMessage;
	return Result;
}

TSharedPtr<FJsonObject> FAssetDocumentResult::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("success"), IsSuccess());
	Json->SetStringField(TEXT("message"), Message);
	Json->SetStringField(TEXT("target"), Target);
	Json->SetStringField(TEXT("asset_path"), AssetPath);
	Json->SetStringField(TEXT("sidecar_file_path"), SidecarFilePath);
	Json->SetBoolField(TEXT("saved_asset"), bSavedAsset);
	Json->SetBoolField(TEXT("wrote_sidecar"), bWroteSidecar);

	TArray<TSharedPtr<FJsonValue>> DiagnosticValues;
	for (const FAssetDocumentDiagnostic& Diagnostic : Diagnostics)
	{
		TSharedPtr<FJsonObject> DiagnosticJson = MakeShared<FJsonObject>();
		DiagnosticJson->SetStringField(TEXT("path"), Diagnostic.Path);
		DiagnosticJson->SetStringField(TEXT("code"), Diagnostic.Code);
		DiagnosticJson->SetStringField(TEXT("message"), Diagnostic.Message);
		DiagnosticValues.Add(MakeShared<FJsonValueObject>(DiagnosticJson));
	}
	Json->SetArrayField(TEXT("diagnostics"), DiagnosticValues);

	if (Payload.IsValid())
	{
		Json->SetObjectField(TEXT("payload"), Payload);
	}

	return Json;
}

FAssetDocumentProfileRegistry& FAssetDocumentService::GetProfileRegistry()
{
	static FAssetDocumentProfileRegistry Registry;
	return Registry;
}

FAssetDocumentResult FAssetDocumentService::Apply(const FAssetDocumentApplyRequest& Request)
{
	if (!Request.Document.IsValid())
	{
		return FAssetDocumentResult::Failure(TEXT("Apply requires a JSON document"));
	}

	const FString NormalizedSourceDocumentPath = NormalizeValidateFilePath(Request.SourceDocumentPath);

	double SchemaVersion = 0.0;
	if (!Request.Document->TryGetNumberField(TEXT("SchemaVersion"), SchemaVersion) || SchemaVersion != 1.0)
	{
		return FAssetDocumentResult::Failure(TEXT("SchemaVersion must be 1"));
	}

	const bool bHasStructuredShape = Request.Document->HasField(TEXT("Body")) || Request.Document->HasField(TEXT("Definitions"));
	FString AssetType;
	if (Request.Document->TryGetStringField(TEXT("AssetType"), AssetType))
	{
		if (AssetType != TEXT("GenericAsset"))
		{
			return FAssetDocumentResult::Failure(TEXT("AssetType must be GenericAsset"));
		}
	}
	else if (!bHasStructuredShape)
	{
		return FAssetDocumentResult::Failure(TEXT("AssetType must be GenericAsset"));
	}

	FString Target;
	if (!Request.Document->TryGetStringField(TEXT("Target"), Target) || Target.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("Target is required"));
	}
	Target = NormalizeValidateTarget(Target);

	FString Error;
	if (!ValidateApplyTarget(Target, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Target;
		return Result;
	}

	FString ClassName;
	if (!Request.Document->TryGetStringField(TEXT("Class"), ClassName) || ClassName.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Class is required"));
		Result.Target = Target;
		return Result;
	}

	FString ActionName;
	if (!Request.Document->TryGetStringField(TEXT("Action"), ActionName) || ActionName.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Action is required"));
		Result.Target = Target;
		return Result;
	}

	EAssetDocumentLifecycleAction Action;
	if (!FAssetDocumentLifecycle::TryParseAction(ActionName, Action, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Target;
		return Result;
	}

	UClass* ResolvedClass = nullptr;
	if (!FAssetDocumentClassResolver::ResolveClass(ClassName, ResolvedClass, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = Target;
		return Result;
	}

	const TSharedPtr<FJsonObject>* DefinitionsPtr = nullptr;
	if (Request.Document->HasField(TEXT("Definitions")))
	{
		if (!Request.Document->TryGetObjectField(TEXT("Definitions"), DefinitionsPtr) || !DefinitionsPtr)
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Definitions must be a JSON object"));
			Result.Target = Target;
			Result.SidecarFilePath = NormalizedSourceDocumentPath;
			return Result;
		}
	}

	TSharedPtr<FJsonObject> Properties;
	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (Request.Document->TryGetObjectField(TEXT("Properties"), PropertiesPtr) && PropertiesPtr)
	{
		Properties = *PropertiesPtr;
	}

	FAssetDocumentPropertyApplyResult PreflightResult = FAssetDocumentPropertyAdapter::PreflightProperties(ResolvedClass, Properties);
	if (!PreflightResult.bSuccess)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(PreflightResult.Message);
		Result.Target = Target;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		Result.Diagnostics = PreflightResult.Diagnostics;
		return Result;
	}

	FAssetDocumentLifecycleResult LifecycleResult = FAssetDocumentLifecycle::CreateOrLoad(Target, ResolvedClass, Action);
	if (!LifecycleResult.Asset)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(LifecycleResult.Error);
		Result.Target = Target;
		Result.AssetPath = LifecycleResult.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		return Result;
	}

	FAssetDocumentPropertyApplyResult PropertyResult = FAssetDocumentPropertyAdapter::ApplyProperties(LifecycleResult.Asset, Properties);
	if (!PropertyResult.bSuccess)
	{
		FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(PropertyResult.Message);
		Result.Target = Target;
		Result.AssetPath = LifecycleResult.ObjectPath;
		Result.SidecarFilePath = NormalizedSourceDocumentPath;
		Result.Diagnostics = PropertyResult.Diagnostics;
		return Result;
	}

	TArray<FAssetDocumentDiagnostic> Diagnostics = PropertyResult.Diagnostics;
	if (Request.Document->HasField(TEXT("Body")))
	{
		TSharedPtr<FJsonValue> BodyValue = Request.Document->TryGetField(TEXT("Body"));
		if (!BodyValue.IsValid())
		{
			FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Body is required when present"));
			Result.Target = Target;
			Result.AssetPath = LifecycleResult.ObjectPath;
			Result.SidecarFilePath = NormalizedSourceDocumentPath;
			Result.Diagnostics = Diagnostics;
			return Result;
		}

		const TSharedPtr<FJsonObject> BodyObject = BodyValue->AsObject();
		if (!BodyObject.IsValid())
		{
			FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Body must be a JSON object"));
			Result.Target = Target;
			Result.AssetPath = LifecycleResult.ObjectPath;
			Result.SidecarFilePath = NormalizedSourceDocumentPath;
			FAssetDocumentDiagnostic Diagnostic;
			Diagnostic.Path = TEXT("/Body");
			Diagnostic.Code = TEXT("InvalidBodyType");
			Diagnostic.Message = Result.Message;
			Diagnostics.Add(MoveTemp(Diagnostic));
			Result.Diagnostics = Diagnostics;
			return Result;
		}

		const FAssetDocumentProfileResolution ProfileResolution = ResolveAssetDocumentProfile(FAssetDocumentService::GetProfileRegistry(), ResolvedClass);
		if (!ProfileResolution.ExactProfile.IsValid())
		{
			FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Body is not supported for class '%s'"), *ResolvedClass->GetPathName()));
			Result.Target = Target;
			Result.AssetPath = LifecycleResult.ObjectPath;
			Result.SidecarFilePath = NormalizedSourceDocumentPath;
			FAssetDocumentDiagnostic Diagnostic;
			Diagnostic.Path = TEXT("/Body");
			Diagnostic.Code = TEXT("MissingProfile");
			Diagnostic.Message = Result.Message;
			Diagnostics.Add(MoveTemp(Diagnostic));
			Result.Diagnostics = Diagnostics;
			return Result;
		}

		TArray<const IAssetDocumentCapability*> BodyAdapters;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObject->Values)
		{
			const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile->ResolveBodyAdapter(FName(*Pair.Key));
			if (!BodyAdapter)
			{
				FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
				FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Profile for class '%s' does not provide Body adapter for '%s'"), *ResolvedClass->GetPathName(), *Pair.Key));
				Result.Target = Target;
				Result.AssetPath = LifecycleResult.ObjectPath;
				Result.SidecarFilePath = NormalizedSourceDocumentPath;
				FAssetDocumentDiagnostic Diagnostic;
				Diagnostic.Path = FString::Printf(TEXT("/Body/%s"), *Pair.Key);
				Diagnostic.Code = TEXT("MissingBodyAdapter");
				Diagnostic.Message = Result.Message;
				Diagnostics.Add(MoveTemp(Diagnostic));
				Result.Diagnostics = Diagnostics;
				return Result;
			}

			BodyAdapters.AddUnique(BodyAdapter);
		}

		if (BodyAdapters.Num() == 0)
		{
			if (const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile->ResolveBodyAdapter(TEXT("Body")))
			{
				BodyAdapters.Add(BodyAdapter);
			}
		}

		for (const IAssetDocumentCapability* BodyAdapter : BodyAdapters)
		{
			FAssetDocumentCapabilityContext CapabilityContext;
			CapabilityContext.Asset = LifecycleResult.Asset;
			CapabilityContext.AssetClass = ResolvedClass;
			CapabilityContext.TargetAssetPath = Target;
			CapabilityContext.SourceDocumentPath = NormalizedSourceDocumentPath;
			CapabilityContext.Definitions = DefinitionsPtr;
			CapabilityContext.bIsDryRun = false;

			FAssetDocumentCapabilityResult CapabilityResult = const_cast<IAssetDocumentCapability*>(BodyAdapter)->Apply(CapabilityContext, BodyValue.ToSharedRef());
			if (!CapabilityResult.bSuccess)
			{
				FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
				FAssetDocumentResult Result = MakeCapabilityValidationFailure(CapabilityResult, Target, NormalizedSourceDocumentPath);
				Result.AssetPath = LifecycleResult.ObjectPath;
				Result.Diagnostics.Insert(Diagnostics, 0);
				return Result;
			}

			Diagnostics.Append(CapabilityResult.Diagnostics);
		}
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument applied"));
	Result.Target = Target;
	Result.AssetPath = LifecycleResult.ObjectPath;
	Result.SidecarFilePath = NormalizedSourceDocumentPath;
	Result.Diagnostics = Diagnostics;
	Result.bWroteSidecar = false;

	if (Request.bSaveAsset)
	{
		LifecycleResult.Asset->MarkPackageDirty();
		UPackage* Package = LifecycleResult.Asset->GetOutermost();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, LifecycleResult.Asset, *PackageFileName, SaveArgs);
		if (!bSaved)
		{
			FAssetDocumentLifecycle::CleanupCreatedAsset(LifecycleResult);
			Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to save asset package '%s'"), *Package->GetName()));
			Result.Target = Target;
			Result.AssetPath = LifecycleResult.ObjectPath;
			Result.SidecarFilePath = NormalizedSourceDocumentPath;
			Result.Diagnostics = Diagnostics;
			Result.bSavedAsset = false;
			Result.bWroteSidecar = false;
			return Result;
		}
		Result.bSavedAsset = true;
	}

	return Result;
}

FAssetDocumentResult FAssetDocumentService::ApplyFile(const FAssetDocumentApplyFileRequest& Request)
{
	const FString NormalizedFilePath = NormalizeValidateFilePath(Request.FilePath);
	if (NormalizedFilePath.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("ApplyFile requires a sidecar file path"));
	}

	TSharedPtr<FJsonObject> Document;
	FString Error;
	if (!FAssetDocumentSidecar::LoadJsonFile(NormalizedFilePath, Document, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	if (!FAssetDocumentSidecar::ValidateTargetMatchesSidecar(NormalizedFilePath, Document, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.SidecarFilePath = NormalizedFilePath;

		FString Target;
		if (Document.IsValid() && Document->TryGetStringField(TEXT("Target"), Target))
		{
			Result.Target = NormalizeValidateTarget(Target);
		}
		return Result;
	}

	FAssetDocumentApplyRequest ApplyRequest;
	ApplyRequest.Document = Document;
	ApplyRequest.SourceDocumentPath = NormalizedFilePath;
	ApplyRequest.bSaveAsset = Request.bSaveAsset;
	ApplyRequest.bWriteSidecar = false;

	FAssetDocumentResult Result = Apply(ApplyRequest);
	Result.SidecarFilePath = NormalizedFilePath;

	if (!Result.Payload.IsValid())
	{
		Result.Payload = MakeShared<FJsonObject>();
	}
	Result.Payload->SetStringField(TEXT("sidecar_file_path"), NormalizedFilePath);
	Result.Payload->SetBoolField(TEXT("triggered_by_watcher"), Request.bTriggeredByWatcher);

	return Result;
}

FAssetDocumentResult FAssetDocumentService::Inspect(const FAssetDocumentInspectRequest& Request) const
{
	FAssetDocumentResolvedTarget ResolvedTarget;
	FAssetDocumentResult ResolveResult = ResolveClassOrAssetTarget(Request.ClassOrAsset, TEXT("Inspect"), ResolvedTarget);
	if (!ResolveResult.IsSuccess())
	{
		return ResolveResult;
	}

	TSharedPtr<FJsonObject> Payload = FAssetDocumentPropertyAdapter::InspectProperties(ResolvedTarget.Class, ResolvedTarget.Asset);
	if (!Payload.IsValid())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Failed to inspect reflected properties"));
		Result.Target = ResolvedTarget.Target;
		Result.AssetPath = ResolvedTarget.AssetPath;
		return Result;
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument inspected"));
	Result.Target = ResolvedTarget.Target;
	Result.AssetPath = ResolvedTarget.AssetPath;
	Result.Payload = Payload;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::InspectProfile(const FAssetDocumentProfileRequest& Request) const
{
	FAssetDocumentResolvedTarget ResolvedTarget;
	FAssetDocumentResult ResolveResult = ResolveClassOrAssetTarget(Request.ClassOrAsset, TEXT("InspectProfile"), ResolvedTarget);
	if (!ResolveResult.IsSuccess())
	{
		return ResolveResult;
	}

	const FAssetDocumentProfileResolution ProfileResolution = ResolveAssetDocumentProfile(GetProfileRegistry(), ResolvedTarget.Class);
	TSharedPtr<FJsonObject> Payload = ProfileResolution.ExactProfile.IsValid()
		? MakeExactProfilePayload(ProfileResolution)
		: MakeGenericProfilePayload(ProfileResolution);

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument profile inspected"));
	Result.Target = ResolvedTarget.Target;
	Result.AssetPath = ResolvedTarget.AssetPath;
	Result.Payload = Payload;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::CreateTemplate(const FAssetDocumentTemplateRequest& Request) const
{
	const FString NormalizedTarget = NormalizeValidateTarget(Request.Target);
	if (NormalizedTarget.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("CreateTemplate requires a target"));
	}

	FString Error;
	if (!ValidateApplyTarget(NormalizedTarget, Error))
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
		Result.Target = NormalizedTarget;
		return Result;
	}

	UClass* ResolvedClass = nullptr;
	FAssetDocumentResult ResolveResult = ResolveClassTarget(Request.Class, ResolvedClass);
	if (!ResolveResult.IsSuccess())
	{
		return ResolveResult;
	}

	const FAssetDocumentProfileResolution ProfileResolution = ResolveAssetDocumentProfile(GetProfileRegistry(), ResolvedClass);
	if (ProfileResolution.ExactProfile.IsValid())
	{
		FAssetDocumentTemplateContext Context;
		Context.Target = NormalizedTarget;
		Context.ClassPath = ResolvedClass->GetPathName();

		FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument template created"));
		Result.Target = NormalizedTarget;
		Result.Payload = ProfileResolution.ExactProfile->CreateTemplate(Context);
		return Result;
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("SchemaVersion"), 1);
	Payload->SetStringField(TEXT("Target"), NormalizedTarget);
	Payload->SetStringField(TEXT("Class"), ResolvedClass->GetPathName());
	Payload->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Payload->SetObjectField(TEXT("Definitions"), MakeShared<FJsonObject>());
	Payload->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument template created"));
	Result.Target = NormalizedTarget;
	Result.Payload = Payload;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::GetSchema() const
{
	TSharedPtr<FJsonObject> FieldNaming = MakeShared<FJsonObject>();
	FieldNaming->SetBoolField(TEXT("ban_abbreviations"), true);
	FieldNaming->SetBoolField(TEXT("use_ue_stable_field_names"), true);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("schema_version"), 1);
	Payload->SetStringField(TEXT("asset_type"), TEXT("GenericAsset"));
	Payload->SetArrayField(TEXT("asset_document_tools"), MakeStringArray({
		TEXT("get_asset_document_schema"),
		TEXT("inspect_asset_document_target"),
		TEXT("inspect_asset_document_profile"),
		TEXT("create_asset_document_template"),
		TEXT("extract_asset_document"),
		TEXT("validate_asset_document"),
		TEXT("diff_asset_document"),
		TEXT("apply_asset_document"),
		TEXT("apply_asset_document_file"),
	}));
	Payload->SetArrayField(TEXT("fragment_kinds"), MakeFragmentKindArray());
	Payload->SetObjectField(TEXT("field_naming"), FieldNaming);
	Payload->SetArrayField(TEXT("registered_profiles"), MakeRegisteredProfileArray(GetProfileRegistry()));

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument schema described"));
	Result.Payload = Payload;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Extract(const FAssetDocumentExtractRequest& Request) const
{
	if (Request.AssetPath.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("Extract requires an asset path"));
	}

	UObject* Asset = LoadAssetFromPackageOrObjectPath(Request.AssetPath);
	const FString Target = NormalizeValidateTarget(Request.AssetPath);
	if (!Asset)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to load asset '%s'"), *Request.AssetPath));
		Result.Target = Target;
		Result.AssetPath = ToObjectPath(Request.AssetPath);
		return Result;
	}

	const bool bSkipDefaults = Request.bDiffOnly && !Request.bIncludeAllWritable;
	TSharedPtr<FJsonObject> Properties = FAssetDocumentPropertyAdapter::ExtractWritablePropertiesToJson(Asset, bSkipDefaults);
	if (!Properties.IsValid())
	{
		Properties = MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), 1);
	Document->SetStringField(TEXT("AssetType"), TEXT("GenericAsset"));
	Document->SetStringField(TEXT("Target"), Target);
	Document->SetStringField(TEXT("Class"), Asset->GetClass()->GetPathName());
	Document->SetStringField(TEXT("Action"), TEXT("CreateOrUpdate"));
	Document->SetObjectField(TEXT("Properties"), Properties);

	const FAssetDocumentProfileResolution ProfileResolution = ResolveAssetDocumentProfile(FAssetDocumentService::GetProfileRegistry(), Asset->GetClass());
	if (ProfileResolution.ExactProfile.IsValid())
	{
		if (const IAssetDocumentCapability* BodyAdapter = ProfileResolution.ExactProfile->ResolveBodyAdapter(TEXT("Body")))
		{
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			FAssetDocumentCapabilityContext CapabilityContext;
			CapabilityContext.Asset = Asset;
			CapabilityContext.AssetClass = Asset->GetClass();
			CapabilityContext.TargetAssetPath = Target;

			const FAssetDocumentCapabilityResult CapabilityResult = BodyAdapter->Extract(CapabilityContext, Body);
			if (!CapabilityResult.bSuccess)
			{
				FAssetDocumentResult Result = FAssetDocumentResult::Failure(CapabilityResult.Message);
				Result.Target = Target;
				Result.AssetPath = Asset->GetPathName();
				Result.Diagnostics = CapabilityResult.Diagnostics;
				return Result;
			}

			Document->SetObjectField(TEXT("Body"), Body);
		}
	}

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument extracted"));
	Result.Target = Target;
	Result.AssetPath = Asset->GetPathName();
	Result.Payload = Document;
	return Result;
}

FAssetDocumentResult FAssetDocumentService::Validate(const FAssetDocumentValidateRequest& Request) const
{
	const FString NormalizedFilePath = NormalizeValidateFilePath(Request.FilePath);
	TSharedPtr<FJsonObject> Document = Request.Document;

	if (!Document.IsValid() && NormalizedFilePath.IsEmpty())
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(TEXT("Validate requires a JSON document or sidecar file path"));
		return Result;
	}

	if (!Document.IsValid())
	{
		FString Error;
		if (!FAssetDocumentSidecar::LoadJsonFile(NormalizedFilePath, Document, Error))
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
			Result.SidecarFilePath = NormalizedFilePath;
			return Result;
		}
	}

	return ValidateGenericAssetDocument(Document, NormalizedFilePath, true);
}

FAssetDocumentResult FAssetDocumentService::Diff(const FAssetDocumentDiffRequest& Request) const
{
	const FString NormalizedFilePath = NormalizeValidateFilePath(Request.FilePath);
	TSharedPtr<FJsonObject> Document = Request.Document;

	if (!Document.IsValid() && NormalizedFilePath.IsEmpty())
	{
		return FAssetDocumentResult::Failure(TEXT("Diff requires a JSON document or sidecar file path"));
	}

	if (!Document.IsValid())
	{
		FString Error;
		if (!FAssetDocumentSidecar::LoadJsonFile(NormalizedFilePath, Document, Error))
		{
			FAssetDocumentResult Result = FAssetDocumentResult::Failure(Error);
			Result.SidecarFilePath = NormalizedFilePath;
			return Result;
		}
	}

	const FAssetDocumentResult ValidateResult = ValidateGenericAssetDocument(Document, NormalizedFilePath, false);
	if (!ValidateResult.IsSuccess())
	{
		return ValidateResult;
	}

	FString Target;
	Document->TryGetStringField(TEXT("Target"), Target);
	Target = NormalizeValidateTarget(Target);
	if (!NormalizedFilePath.IsEmpty())
	{
		Target = FAssetDocumentSidecar::ResolveObjectPathFromSidecar(NormalizedFilePath);
	}

	UObject* Asset = LoadAssetFromPackageOrObjectPath(Target);
	if (!Asset)
	{
		FAssetDocumentResult Result = FAssetDocumentResult::Failure(FString::Printf(TEXT("Failed to load asset '%s'"), *Target));
		Result.Target = Target;
		Result.AssetPath = ToObjectPath(Target);
		Result.SidecarFilePath = NormalizedFilePath;
		return Result;
	}

	TSharedPtr<FJsonObject> Properties;
	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	if (Document->TryGetObjectField(TEXT("Properties"), PropertiesPtr) && PropertiesPtr)
	{
		Properties = *PropertiesPtr;
	}
	if (!Properties.IsValid())
	{
		Properties = MakeShared<FJsonObject>();
	}

	TArray<TSharedPtr<FJsonValue>> Changed;
	TArray<TSharedPtr<FJsonValue>> Unchanged;
	TArray<TSharedPtr<FJsonValue>> Skipped;
	TArray<TSharedPtr<FJsonValue>> Failed;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
	{
		FProperty* Property = FindFProperty<FProperty>(Asset->GetClass(), *Pair.Key);
		if (!Property)
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("UnknownProperty"), FString::Printf(TEXT("Property '%s' does not exist"), *Pair.Key));
			continue;
		}

		const FString NonWritableReason = FAssetDocumentPropertyAdapter::GetNonWritableReason(Property);
		if (!NonWritableReason.IsEmpty())
		{
			AddReasonEntry(Skipped, Pair.Key, TEXT("NonWritable"), FString::Printf(TEXT("Property '%s' is not writable: %s"), *Pair.Key, *NonWritableReason));
			continue;
		}

		const void* CurrentValuePtr = Property->ContainerPtrToValuePtr<void>(Asset);
		TSharedPtr<FJsonValue> BeforeValue = FAssetDocumentPropertyAdapter::ExtractPropertyValue(Property, CurrentValuePtr);
		if (!BeforeValue.IsValid())
		{
			AddReasonEntry(Skipped, Pair.Key, TEXT("UnsupportedSerialization"), FString::Printf(TEXT("Property '%s' cannot be serialized"), *Pair.Key));
			continue;
		}

		UObject* PreviewAsset = DuplicateObject<UObject>(Asset, GetTransientPackage());
		if (!PreviewAsset)
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("DuplicateFailed"), FString::Printf(TEXT("Failed to duplicate asset while diffing '%s'"), *Pair.Key));
			continue;
		}

		TSharedPtr<FJsonObject> SingleProperty = MakeShared<FJsonObject>();
		SingleProperty->SetField(Pair.Key, Pair.Value);
		FAssetDocumentPropertyApplyResult ApplyResult = FAssetDocumentPropertyAdapter::ApplyProperties(PreviewAsset, SingleProperty);
		if (!ApplyResult.bSuccess)
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("TypeValidationFailed"), ApplyResult.Message);
			continue;
		}

		const void* AfterValuePtr = Property->ContainerPtrToValuePtr<void>(PreviewAsset);
		TSharedPtr<FJsonValue> AfterValue = FAssetDocumentPropertyAdapter::ExtractPropertyValue(Property, AfterValuePtr);
		if (!AfterValue.IsValid())
		{
			AddReasonEntry(Failed, Pair.Key, TEXT("AfterSerializationFailed"), FString::Printf(TEXT("Property '%s' could not be serialized after applying document value"), *Pair.Key));
			continue;
		}

		if (JsonValueToComparableString(BeforeValue) == JsonValueToComparableString(AfterValue))
		{
			AddNamedValueEntry(Unchanged, Pair.Key, BeforeValue);
		}
		else
		{
			AddChangedEntry(Changed, Pair.Key, BeforeValue, AfterValue);
		}
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("changed"), Changed);
	Payload->SetArrayField(TEXT("unchanged"), Unchanged);
	Payload->SetArrayField(TEXT("skipped"), Skipped);
	Payload->SetArrayField(TEXT("failed"), Failed);

	FAssetDocumentResult Result = FAssetDocumentResult::Success(TEXT("AssetDocument diffed"));
	Result.Target = Target;
	Result.AssetPath = Asset->GetPathName();
	Result.SidecarFilePath = NormalizedFilePath;
	Result.Payload = Payload;
	return Result;
}
