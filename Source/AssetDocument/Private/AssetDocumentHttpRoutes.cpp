// Copyright ProjectRPG. All Rights Reserved.

#include "AssetDocumentHttpRoutes.h"

#include "AssetDocumentService.h"
#include "AssetFactoryHttpServer.h"

#include "Async/Async.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <initializer_list>

namespace
{
bool TryParseJsonBody(const FString& RequestBody, TSharedPtr<FJsonObject>& OutJson, FString& OutError)
{
	if (RequestBody.IsEmpty())
	{
		OutError = TEXT("Request body is required");
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestBody);
	if (!FJsonSerializer::Deserialize(Reader, OutJson) || !OutJson.IsValid())
	{
		OutError = TEXT("Invalid JSON body");
		return false;
	}

	return true;
}

bool TryGetStringFieldAny(const TSharedPtr<FJsonObject>& Json, std::initializer_list<const TCHAR*> FieldNames, FString& OutValue)
{
	if (!Json.IsValid())
	{
		return false;
	}

	for (const TCHAR* FieldName : FieldNames)
	{
		if (Json->TryGetStringField(FieldName, OutValue))
		{
			return true;
		}
	}
	return false;
}

bool TryGetBoolFieldAny(const TSharedPtr<FJsonObject>& Json, std::initializer_list<const TCHAR*> FieldNames, bool& OutValue)
{
	if (!Json.IsValid())
	{
		return false;
	}

	for (const TCHAR* FieldName : FieldNames)
	{
		if (Json->TryGetBoolField(FieldName, OutValue))
		{
			return true;
		}
	}
	return false;
}

bool TryGetQueryParamAny(const TMap<FString, FString>& QueryParams, std::initializer_list<const TCHAR*> FieldNames, FString& OutValue)
{
	for (const TCHAR* FieldName : FieldNames)
	{
		if (const FString* Value = QueryParams.Find(FString(FieldName)))
		{
			OutValue = *Value;
			return true;
		}
	}
	return false;
}

TSharedPtr<FJsonObject> MakeErrorJson(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> ErrorJson = MakeShared<FJsonObject>();
	ErrorJson->SetBoolField(TEXT("success"), false);
	ErrorJson->SetStringField(TEXT("error"), ErrorMessage);
	return ErrorJson;
}

int32 GetStatusCode(const FAssetDocumentResult& Result)
{
	switch (Result.Status)
	{
	case EAssetDocumentResultStatus::Success:
		return 200;
	case EAssetDocumentResultStatus::PartialFailed:
		return 207;
	case EAssetDocumentResultStatus::Failed:
	default:
		return 400;
	}
}

template<typename CallableType>
FAssetDocumentResult RunOnGameThread(CallableType&& Callable)
{
	if (IsInGameThread())
	{
		return Callable();
	}

	FAssetDocumentResult Result;
	FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);
	AsyncTask(ENamedThreads::GameThread, [&]()
	{
		Result = Callable();
		CompletionEvent->Trigger();
	});
	CompletionEvent->Wait();
	FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
	return Result;
}

void AddRouteDescriptor(TArray<TSharedPtr<FJsonValue>>& Routes, const FString& Method, const FString& Path, const FString& Description, bool bRequiresBody)
{
	TSharedPtr<FJsonObject> Route = MakeShared<FJsonObject>();
	Route->SetStringField(TEXT("method"), Method);
	Route->SetStringField(TEXT("path"), Path);
	Route->SetStringField(TEXT("description"), Description);
	Route->SetBoolField(TEXT("requires_body"), bRequiresBody);
	Routes.Add(MakeShared<FJsonValueObject>(Route));
}

TSharedPtr<FJsonObject> BuildSchemaResponse()
{
	TArray<TSharedPtr<FJsonValue>> Routes;
	AddRouteDescriptor(Routes, TEXT("POST"), TEXT("/assetfactory/assetdocument/apply"), TEXT("Apply an AssetDocument JSON document"), true);
	AddRouteDescriptor(Routes, TEXT("POST"), TEXT("/assetfactory/assetdocument/apply-file"), TEXT("Apply an AssetDocument sidecar file"), true);
	AddRouteDescriptor(Routes, TEXT("GET"), TEXT("/assetfactory/assetdocument/schema"), TEXT("Describe AssetDocument HTTP routes"), false);
	AddRouteDescriptor(Routes, TEXT("GET"), TEXT("/assetfactory/assetdocument/inspect"), TEXT("Inspect writable reflected properties for a class or asset"), false);
	AddRouteDescriptor(Routes, TEXT("POST"), TEXT("/assetfactory/assetdocument/extract"), TEXT("Extract an asset as an AssetDocument JSON document"), true);
	AddRouteDescriptor(Routes, TEXT("POST"), TEXT("/assetfactory/assetdocument/validate"), TEXT("Validate an AssetDocument JSON document or sidecar file"), true);
	AddRouteDescriptor(Routes, TEXT("POST"), TEXT("/assetfactory/assetdocument/diff"), TEXT("Diff an AssetDocument JSON document or sidecar file against the current asset"), true);

	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("success"), true);
	Json->SetNumberField(TEXT("schema_version"), 1);
	Json->SetStringField(TEXT("asset_type"), TEXT("GenericAsset"));
	Json->SetArrayField(TEXT("routes"), Routes);
	return Json;
}
}

FAssetDocumentHttpRoutes::FAssetDocumentHttpRoutes(FAssetDocumentService& InService)
	: Service(InService)
{
}

FAssetDocumentHttpRoutes::~FAssetDocumentHttpRoutes()
{
	Unregister();
}

void FAssetDocumentHttpRoutes::Register()
{
	if (!RouteHandles.IsEmpty())
	{
		return;
	}

	auto RegisterRoute = [](const TCHAR* Path, FAssetFactoryExternalRouteHandler Handler, const TCHAR* MethodText, const TCHAR* Description, bool bRequiresBody, TArray<FDelegateHandle>& OutRouteHandles)
	{
		FAssetFactoryExternalRoute Route;
		Route.Path = Path;
		Route.Handler = Handler;
		Route.MethodText = MethodText;
		Route.Description = Description;
		Route.bRequiresBody = bRequiresBody;

		const FDelegateHandle Handle = FAssetFactoryHttpServer::RegisterExternalRoute(Route);
		if (Handle.IsValid())
		{
			OutRouteHandles.Add(Handle);
		}
	};

	RegisterRoute(TEXT("/assetfactory/assetdocument/apply"), FAssetFactoryExternalRouteHandler::CreateRaw(this, &FAssetDocumentHttpRoutes::HandleApply), TEXT("POST"), TEXT("Apply an AssetDocument JSON document"), true, RouteHandles);
	RegisterRoute(TEXT("/assetfactory/assetdocument/apply-file"), FAssetFactoryExternalRouteHandler::CreateRaw(this, &FAssetDocumentHttpRoutes::HandleApplyFile), TEXT("POST"), TEXT("Apply an AssetDocument sidecar file"), true, RouteHandles);
	RegisterRoute(TEXT("/assetfactory/assetdocument/schema"), FAssetFactoryExternalRouteHandler::CreateRaw(this, &FAssetDocumentHttpRoutes::HandleSchema), TEXT("GET"), TEXT("Describe AssetDocument HTTP routes"), false, RouteHandles);
	RegisterRoute(TEXT("/assetfactory/assetdocument/inspect"), FAssetFactoryExternalRouteHandler::CreateRaw(this, &FAssetDocumentHttpRoutes::HandleInspect), TEXT("GET"), TEXT("Inspect writable reflected properties for a class or asset"), false, RouteHandles);
	RegisterRoute(TEXT("/assetfactory/assetdocument/extract"), FAssetFactoryExternalRouteHandler::CreateRaw(this, &FAssetDocumentHttpRoutes::HandleExtract), TEXT("POST"), TEXT("Extract an asset as an AssetDocument JSON document"), true, RouteHandles);
	RegisterRoute(TEXT("/assetfactory/assetdocument/validate"), FAssetFactoryExternalRouteHandler::CreateRaw(this, &FAssetDocumentHttpRoutes::HandleValidate), TEXT("POST"), TEXT("Validate an AssetDocument JSON document or sidecar file"), true, RouteHandles);
	RegisterRoute(TEXT("/assetfactory/assetdocument/diff"), FAssetFactoryExternalRouteHandler::CreateRaw(this, &FAssetDocumentHttpRoutes::HandleDiff), TEXT("POST"), TEXT("Diff an AssetDocument JSON document or sidecar file against the current asset"), true, RouteHandles);
}

void FAssetDocumentHttpRoutes::Unregister()
{
	for (const FDelegateHandle& Handle : RouteHandles)
	{
		FAssetFactoryHttpServer::UnregisterExternalRoute(Handle);
	}
	RouteHandles.Reset();
}

TSharedPtr<FJsonObject> FAssetDocumentHttpRoutes::HandleApply(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode)
{
	TSharedPtr<FJsonObject> Json;
	FString Error;
	if (!TryParseJsonBody(RequestBody, Json, Error))
	{
		OutStatusCode = 400;
		return MakeErrorJson(Error);
	}

	FAssetDocumentApplyRequest ApplyRequest;
	ApplyRequest.Document = Json;
	const FAssetDocumentResult Result = RunOnGameThread([this, ApplyRequest]()
	{
		return Service.Apply(ApplyRequest);
	});

	OutStatusCode = GetStatusCode(Result);
	return Result.ToJson();
}

TSharedPtr<FJsonObject> FAssetDocumentHttpRoutes::HandleApplyFile(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode)
{
	TSharedPtr<FJsonObject> Json;
	FString Error;
	if (!TryParseJsonBody(RequestBody, Json, Error))
	{
		OutStatusCode = 400;
		return MakeErrorJson(Error);
	}

	FAssetDocumentApplyFileRequest ApplyFileRequest;
	if (!TryGetStringFieldAny(Json, {TEXT("file_path"), TEXT("FilePath")}, ApplyFileRequest.FilePath) || ApplyFileRequest.FilePath.IsEmpty())
	{
		OutStatusCode = 400;
		return MakeErrorJson(TEXT("ApplyFile requires file_path or FilePath"));
	}
	TryGetBoolFieldAny(Json, {TEXT("save_asset"), TEXT("SaveAsset")}, ApplyFileRequest.bSaveAsset);

	const FAssetDocumentResult Result = RunOnGameThread([this, ApplyFileRequest]()
	{
		return Service.ApplyFile(ApplyFileRequest);
	});

	OutStatusCode = GetStatusCode(Result);
	return Result.ToJson();
}

TSharedPtr<FJsonObject> FAssetDocumentHttpRoutes::HandleSchema(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode)
{
	OutStatusCode = 200;
	return BuildSchemaResponse();
}

TSharedPtr<FJsonObject> FAssetDocumentHttpRoutes::HandleInspect(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode)
{
	FAssetDocumentInspectRequest InspectRequest;
	if (!TryGetQueryParamAny(QueryParams, {TEXT("class_or_asset"), TEXT("ClassOrAsset"), TEXT("target"), TEXT("Target")}, InspectRequest.ClassOrAsset) || InspectRequest.ClassOrAsset.IsEmpty())
	{
		OutStatusCode = 400;
		return MakeErrorJson(TEXT("Inspect requires class_or_asset, ClassOrAsset, target, or Target"));
	}

	const FAssetDocumentResult Result = RunOnGameThread([this, InspectRequest]()
	{
		return Service.Inspect(InspectRequest);
	});

	OutStatusCode = GetStatusCode(Result);
	return Result.ToJson();
}

TSharedPtr<FJsonObject> FAssetDocumentHttpRoutes::HandleExtract(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode)
{
	TSharedPtr<FJsonObject> Json;
	FString Error;
	if (!TryParseJsonBody(RequestBody, Json, Error))
	{
		OutStatusCode = 400;
		return MakeErrorJson(Error);
	}

	FAssetDocumentExtractRequest ExtractRequest;
	if (!TryGetStringFieldAny(Json, {TEXT("asset_path"), TEXT("AssetPath")}, ExtractRequest.AssetPath) || ExtractRequest.AssetPath.IsEmpty())
	{
		OutStatusCode = 400;
		return MakeErrorJson(TEXT("Extract requires asset_path or AssetPath"));
	}
	TryGetBoolFieldAny(Json, {TEXT("diff_only"), TEXT("DiffOnly")}, ExtractRequest.bDiffOnly);
	TryGetBoolFieldAny(Json, {TEXT("include_all_writable"), TEXT("IncludeAllWritable")}, ExtractRequest.bIncludeAllWritable);

	const FAssetDocumentResult Result = RunOnGameThread([this, ExtractRequest]()
	{
		return Service.Extract(ExtractRequest);
	});

	OutStatusCode = GetStatusCode(Result);
	return Result.ToJson();
}

TSharedPtr<FJsonObject> FAssetDocumentHttpRoutes::HandleValidate(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode)
{
	TSharedPtr<FJsonObject> Json;
	FString Error;
	if (!TryParseJsonBody(RequestBody, Json, Error))
	{
		OutStatusCode = 400;
		return MakeErrorJson(Error);
	}

	FAssetDocumentValidateRequest ValidateRequest;
	if (!TryGetStringFieldAny(Json, {TEXT("file_path"), TEXT("FilePath")}, ValidateRequest.FilePath))
	{
		ValidateRequest.Document = Json;
	}

	const FAssetDocumentResult Result = RunOnGameThread([this, ValidateRequest]()
	{
		return Service.Validate(ValidateRequest);
	});

	OutStatusCode = GetStatusCode(Result);
	return Result.ToJson();
}

TSharedPtr<FJsonObject> FAssetDocumentHttpRoutes::HandleDiff(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode)
{
	TSharedPtr<FJsonObject> Json;
	FString Error;
	if (!TryParseJsonBody(RequestBody, Json, Error))
	{
		OutStatusCode = 400;
		return MakeErrorJson(Error);
	}

	FAssetDocumentDiffRequest DiffRequest;
	if (!TryGetStringFieldAny(Json, {TEXT("file_path"), TEXT("FilePath")}, DiffRequest.FilePath))
	{
		DiffRequest.Document = Json;
	}

	const FAssetDocumentResult Result = RunOnGameThread([this, DiffRequest]()
	{
		return Service.Diff(DiffRequest);
	});

	OutStatusCode = GetStatusCode(Result);
	return Result.ToJson();
}
