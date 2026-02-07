// Copyright ProjectRPG. All Rights Reserved.

#include "AssetFactoryHttpServer.h"
#include "AssetFactoryModule.h"
#include "AssetFactorySubsystem.h"
#include "AssetGeneratorRegistry.h"
#include "GenerationTypes.h"
#include "Utils/PropertyPathResolver.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Editor.h"
#include "ObjectTools.h"

FAssetFactoryHttpServer::FAssetFactoryHttpServer()
{
}

FAssetFactoryHttpServer::~FAssetFactoryHttpServer()
{
	Stop();
}

bool FAssetFactoryHttpServer::Start(uint32 Port)
{
	if (bIsRunning)
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("HTTP Server is already running on port %d"), CurrentPort);
		return true;
	}

	// Get HTTP router
	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	HttpRouter = HttpServerModule.GetHttpRouter(Port);

	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogAssetFactory, Error, TEXT("Failed to get HTTP router for port %d"), Port);
		return false;
	}

	// Register routes
	// POST /assetfactory/generate
	GenerateRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/assetfactory/generate")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAssetFactoryHttpServer::HandleGenerate)
	);

	// POST /assetfactory/extract
	ExtractRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/assetfactory/extract")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAssetFactoryHttpServer::HandleExtract)
	);

	// POST /assetfactory/delete
	DeleteRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/assetfactory/delete")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAssetFactoryHttpServer::HandleDelete)
	);

	// POST /assetfactory/query
	QueryRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/assetfactory/query")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAssetFactoryHttpServer::HandleQuery)
	);

	// GET /assetfactory/generators
	GeneratorsRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/assetfactory/generators")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAssetFactoryHttpServer::HandleListGenerators)
	);

	// GET /assetfactory/health
	HealthRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/assetfactory/health")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAssetFactoryHttpServer::HandleHealth)
	);

	// Start listeners
	HttpServerModule.StartAllListeners();

	bIsRunning = true;
	CurrentPort = Port;

	// Write service discovery file for AI agents
	WriteServiceDiscoveryFile();

	UE_LOG(LogAssetFactory, Display, TEXT("AssetFactory HTTP Server started on port %d"), Port);
	UE_LOG(LogAssetFactory, Display, TEXT("  POST http://localhost:%d/assetfactory/generate"), Port);
	UE_LOG(LogAssetFactory, Display, TEXT("  POST http://localhost:%d/assetfactory/extract"), Port);
	UE_LOG(LogAssetFactory, Display, TEXT("  POST http://localhost:%d/assetfactory/delete"), Port);
	UE_LOG(LogAssetFactory, Display, TEXT("  POST http://localhost:%d/assetfactory/query"), Port);
	UE_LOG(LogAssetFactory, Display, TEXT("  GET  http://localhost:%d/assetfactory/generators"), Port);
	UE_LOG(LogAssetFactory, Display, TEXT("  GET  http://localhost:%d/assetfactory/health"), Port);
	UE_LOG(LogAssetFactory, Display, TEXT("  Service discovery: %s"), *GetServiceDiscoveryFilePath());

	return true;
}

void FAssetFactoryHttpServer::Stop()
{
	if (!bIsRunning)
	{
		return;
	}

	// Delete service discovery file
	DeleteServiceDiscoveryFile();

	// Unbind routes
	if (HttpRouter.IsValid())
	{
		HttpRouter->UnbindRoute(GenerateRouteHandle);
		HttpRouter->UnbindRoute(ExtractRouteHandle);
		HttpRouter->UnbindRoute(DeleteRouteHandle);
		HttpRouter->UnbindRoute(QueryRouteHandle);
		HttpRouter->UnbindRoute(GeneratorsRouteHandle);
		HttpRouter->UnbindRoute(HealthRouteHandle);
	}

	bIsRunning = false;
	CurrentPort = 0;
	HttpRouter.Reset();

	UE_LOG(LogAssetFactory, Display, TEXT("AssetFactory HTTP Server stopped"));
}

bool FAssetFactoryHttpServer::HandleGenerate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Parse request body as JSON (properly handle length to avoid reading garbage)
	FString RequestBody;
	if (!Request.Body.IsEmpty())
	{
		// Convert UTF8 bytes to FString with explicit length
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		RequestBody = FString(Converter.Length(), Converter.Get());
	}

	if (RequestBody.IsEmpty())
	{
		SendErrorResponse(OnComplete, 400, TEXT("Request body is empty"));
		return true;
	}

	// Parse JSON
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestBody);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		SendErrorResponse(OnComplete, 400, FString::Printf(TEXT("Invalid JSON: %s"), *Reader->GetErrorMessage()));
		return true;
	}

	// Get AssetFactory subsystem
	UAssetFactorySubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetFactorySubsystem>() : nullptr;
	if (!Subsystem)
	{
		SendErrorResponse(OnComplete, 500, TEXT("AssetFactorySubsystem not available"));
		return true;
	}

	// Generate assets (must be on game thread)
	FGenerationReport Report;

	if (IsInGameThread())
	{
		Report = Subsystem->GenerateFromJson(JsonObject);
	}
	else
	{
		// Execute on game thread and wait
		FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			Report = Subsystem->GenerateFromJson(JsonObject);
			CompletionEvent->Trigger();
		});

		CompletionEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
	}

	// Convert report to JSON and send response
	TSharedPtr<FJsonObject> ResponseJson = ReportToJson(Report);
	SendJsonResponse(OnComplete, Report.HasFailures() ? 207 : 200, ResponseJson);

	return true;
}

bool FAssetFactoryHttpServer::HandleListGenerators(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();

	// Get all registered generators
	TArray<TSharedPtr<FJsonValue>> GeneratorsArray;

	TArray<FString> AssetTypes = FAssetGeneratorRegistry::Get().GetRegisteredTypes();
	for (const FString& AssetType : AssetTypes)
	{
		IAssetGenerator* Generator = FAssetGeneratorRegistry::Get().FindGenerator(AssetType);
		if (Generator)
		{
			TSharedPtr<FJsonObject> GenObj = MakeShared<FJsonObject>();
			GenObj->SetStringField(TEXT("type"), AssetType);
			GenObj->SetNumberField(TEXT("priority"), Generator->GetPriority());

			// Add required fields
			TArray<TSharedPtr<FJsonValue>> RequiredFieldsArray;
			for (const FString& Field : Generator->GetRequiredFields())
			{
				RequiredFieldsArray.Add(MakeShared<FJsonValueString>(Field));
			}
			GenObj->SetArrayField(TEXT("requiredFields"), RequiredFieldsArray);

			GeneratorsArray.Add(MakeShared<FJsonValueObject>(GenObj));
		}
	}

	ResponseJson->SetArrayField(TEXT("generators"), GeneratorsArray);
	ResponseJson->SetNumberField(TEXT("count"), GeneratorsArray.Num());

	SendJsonResponse(OnComplete, 200, ResponseJson);
	return true;
}

bool FAssetFactoryHttpServer::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();
	ResponseJson->SetStringField(TEXT("status"), TEXT("ok"));
	ResponseJson->SetStringField(TEXT("service"), TEXT("AssetFactory"));
	ResponseJson->SetNumberField(TEXT("port"), CurrentPort);

	// Check if subsystem is available
	bool bSubsystemAvailable = GEditor && GEditor->GetEditorSubsystem<UAssetFactorySubsystem>() != nullptr;
	ResponseJson->SetBoolField(TEXT("subsystemAvailable"), bSubsystemAvailable);

	SendJsonResponse(OnComplete, 200, ResponseJson);
	return true;
}

bool FAssetFactoryHttpServer::HandleExtract(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Parse request body as JSON
	FString RequestBody;
	if (!Request.Body.IsEmpty())
	{
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		RequestBody = FString(Converter.Length(), Converter.Get());
	}

	if (RequestBody.IsEmpty())
	{
		SendErrorResponse(OnComplete, 400, TEXT("Request body is empty"));
		return true;
	}

	// Parse JSON
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestBody);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		SendErrorResponse(OnComplete, 400, FString::Printf(TEXT("Invalid JSON: %s"), *Reader->GetErrorMessage()));
		return true;
	}

	// Get AssetFactory subsystem
	UAssetFactorySubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetFactorySubsystem>() : nullptr;
	if (!Subsystem)
	{
		SendErrorResponse(OnComplete, 500, TEXT("AssetFactorySubsystem not available"));
		return true;
	}

	// Get options
	bool bDiffOnly = false;
	JsonObject->TryGetBoolField(TEXT("DiffOnly"), bDiffOnly);

	// Get paths to query (optional)
	TArray<FString> Paths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	if (JsonObject->TryGetArrayField(TEXT("Paths"), PathsArray))
	{
		for (const TSharedPtr<FJsonValue>& PathValue : *PathsArray)
		{
			FString PathStr;
			if (PathValue->TryGetString(PathStr))
			{
				Paths.Add(PathStr);
			}
		}
	}
	// Also support single "Path" field
	FString SinglePath;
	if (JsonObject->TryGetStringField(TEXT("Path"), SinglePath))
	{
		Paths.Add(SinglePath);
	}

	// Get asset paths
	TArray<FString> AssetPaths;
	const TArray<TSharedPtr<FJsonValue>>* AssetsArray = nullptr;
	if (JsonObject->TryGetArrayField(TEXT("Assets"), AssetsArray))
	{
		for (const TSharedPtr<FJsonValue>& AssetValue : *AssetsArray)
		{
			FString AssetPath;
			if (AssetValue->TryGetString(AssetPath))
			{
				AssetPaths.Add(AssetPath);
			}
		}
	}
	// Also support single "Asset" field
	FString SingleAsset;
	if (JsonObject->TryGetStringField(TEXT("Asset"), SingleAsset))
	{
		AssetPaths.Add(SingleAsset);
	}

	if (AssetPaths.Num() == 0)
	{
		SendErrorResponse(OnComplete, 400, TEXT("No assets specified. Use 'Assets' array or 'Asset' string."));
		return true;
	}

	// Extract assets (must be on game thread)
	TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;
	int32 FailedCount = 0;

	auto DoExtract = [&]()
	{
		for (const FString& AssetPath : AssetPaths)
		{
			TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
			ResultObj->SetStringField(TEXT("asset"), AssetPath);

			TSharedPtr<FJsonObject> ExtractedConfig = Subsystem->ExtractAsset(AssetPath, bDiffOnly);
			if (ExtractedConfig.IsValid())
			{
				ResultObj->SetStringField(TEXT("status"), TEXT("Success"));
				ResultObj->SetObjectField(TEXT("config"), ExtractedConfig);
				SuccessCount++;
			}
			else
			{
				ResultObj->SetStringField(TEXT("status"), TEXT("Failed"));
				ResultObj->SetStringField(TEXT("error"), TEXT("Failed to extract asset or asset not found"));
				FailedCount++;
			}

			ResultsArray.Add(MakeShared<FJsonValueObject>(ResultObj));
		}
	};

	if (IsInGameThread())
	{
		DoExtract();
	}
	else
	{
		FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			DoExtract();
			CompletionEvent->Trigger();
		});
		CompletionEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
	}

	ResponseJson->SetBoolField(TEXT("success"), FailedCount == 0);
	ResponseJson->SetNumberField(TEXT("total"), AssetPaths.Num());
	ResponseJson->SetNumberField(TEXT("succeeded"), SuccessCount);
	ResponseJson->SetNumberField(TEXT("failed"), FailedCount);
	ResponseJson->SetArrayField(TEXT("results"), ResultsArray);

	SendJsonResponse(OnComplete, FailedCount > 0 ? 207 : 200, ResponseJson);
	return true;
}

bool FAssetFactoryHttpServer::HandleDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Parse request body as JSON (properly handle length to avoid reading garbage)
	FString RequestBody;
	if (!Request.Body.IsEmpty())
	{
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		RequestBody = FString(Converter.Length(), Converter.Get());
	}

	TSharedPtr<FJsonObject> JsonRequest;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonRequest) || !JsonRequest.IsValid())
	{
		SendErrorResponse(OnComplete, 400, TEXT("Invalid JSON in request body"));
		return true;
	}

	// Get asset paths to delete
	TArray<FString> AssetPaths;

	// Support both "Assets" array and single "Asset" string
	const TArray<TSharedPtr<FJsonValue>>* AssetsArray;
	if (JsonRequest->TryGetArrayField(TEXT("Assets"), AssetsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AssetsArray)
		{
			FString Path;
			if (Value->TryGetString(Path))
			{
				AssetPaths.Add(Path);
			}
		}
	}
	else
	{
		FString SingleAsset;
		if (JsonRequest->TryGetStringField(TEXT("Asset"), SingleAsset))
		{
			AssetPaths.Add(SingleAsset);
		}
	}

	if (AssetPaths.Num() == 0)
	{
		SendErrorResponse(OnComplete, 400, TEXT("No assets specified. Use 'Assets' array or 'Asset' string."));
		return true;
	}

	// Prepare response
	TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;
	int32 FailedCount = 0;

	// Delete must run on game thread
	auto DoDelete = [&]()
	{
		// Get ObjectTools for deletion
		for (const FString& AssetPath : AssetPaths)
		{
			TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
			ResultObj->SetStringField(TEXT("asset"), AssetPath);

			// Build full asset path with object name suffix if needed
			FString FullPath = AssetPath;
			FString AssetName = FPaths::GetBaseFilename(AssetPath);
			FString FullPathWithSuffix = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);

			// Try to find the asset
			UObject* Asset = StaticFindObject(UObject::StaticClass(), nullptr, *FullPathWithSuffix);
			if (!Asset)
			{
				Asset = LoadObject<UObject>(nullptr, *FullPathWithSuffix);
			}
			if (!Asset)
			{
				Asset = StaticFindObject(UObject::StaticClass(), nullptr, *AssetPath);
			}
			if (!Asset)
			{
				Asset = LoadObject<UObject>(nullptr, *AssetPath);
			}

			if (Asset)
			{
				// Get the package
				UPackage* Package = Asset->GetOutermost();
				FString PackagePath = Package->GetName();

				// Delete the asset using ObjectTools
				TArray<UObject*> ObjectsToDelete;
				ObjectsToDelete.Add(Asset);

				// Use ObjectTools to properly delete (handles references, etc.)
				int32 DeletedCount = ObjectTools::DeleteObjects(ObjectsToDelete, /*bShowConfirmation=*/false);

				if (DeletedCount > 0)
				{
					ResultObj->SetStringField(TEXT("status"), TEXT("Success"));
					ResultObj->SetStringField(TEXT("message"), TEXT("Asset deleted successfully"));
					SuccessCount++;
					UE_LOG(LogAssetFactory, Log, TEXT("Deleted asset: %s"), *AssetPath);
				}
				else
				{
					ResultObj->SetStringField(TEXT("status"), TEXT("Failed"));
					ResultObj->SetStringField(TEXT("error"), TEXT("Failed to delete asset (may have references)"));
					FailedCount++;
					UE_LOG(LogAssetFactory, Warning, TEXT("Failed to delete asset: %s"), *AssetPath);
				}
			}
			else
			{
				ResultObj->SetStringField(TEXT("status"), TEXT("Failed"));
				ResultObj->SetStringField(TEXT("error"), TEXT("Asset not found"));
				FailedCount++;
				UE_LOG(LogAssetFactory, Warning, TEXT("Asset not found for deletion: %s"), *AssetPath);
			}

			ResultsArray.Add(MakeShared<FJsonValueObject>(ResultObj));
		}
	};

	if (IsInGameThread())
	{
		DoDelete();
	}
	else
	{
		FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			DoDelete();
			CompletionEvent->Trigger();
		});
		CompletionEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
	}

	ResponseJson->SetBoolField(TEXT("success"), FailedCount == 0);
	ResponseJson->SetNumberField(TEXT("total"), AssetPaths.Num());
	ResponseJson->SetNumberField(TEXT("succeeded"), SuccessCount);
	ResponseJson->SetNumberField(TEXT("failed"), FailedCount);
	ResponseJson->SetArrayField(TEXT("results"), ResultsArray);

	SendJsonResponse(OnComplete, FailedCount > 0 ? 207 : 200, ResponseJson);
	return true;
}

void FAssetFactoryHttpServer::SendJsonResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, TSharedPtr<FJsonObject> JsonResponse)
{
	// Serialize JSON to string
	FString ResponseBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseBody);
	FJsonSerializer::Serialize(JsonResponse.ToSharedRef(), Writer);

	// Create response
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseBody, TEXT("application/json"));
	Response->Code = static_cast<EHttpServerResponseCodes>(StatusCode);

	OnComplete(MoveTemp(Response));
}

void FAssetFactoryHttpServer::SendErrorResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> ErrorJson = MakeShared<FJsonObject>();
	ErrorJson->SetBoolField(TEXT("success"), false);
	ErrorJson->SetStringField(TEXT("error"), ErrorMessage);

	UE_LOG(LogAssetFactory, Warning, TEXT("HTTP Error %d: %s"), StatusCode, *ErrorMessage);

	SendJsonResponse(OnComplete, StatusCode, ErrorJson);
}

TSharedPtr<FJsonObject> FAssetFactoryHttpServer::ReportToJson(const FGenerationReport& Report)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();

	Json->SetBoolField(TEXT("success"), !Report.HasFailures());
	Json->SetNumberField(TEXT("total"), Report.TotalCount);
	Json->SetNumberField(TEXT("succeeded"), Report.SuccessCount + Report.UpdatedCount);
	Json->SetNumberField(TEXT("skipped"), Report.SkippedCount);
	Json->SetNumberField(TEXT("failed"), Report.FailedCount);
	Json->SetStringField(TEXT("summary"), Report.GetSummary());

	// Add individual results
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	for (const FGenerationResult& Result : Report.Results)
	{
		TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

		FString StatusStr;
		switch (Result.Status)
		{
		case EGenerationStatus::Success: StatusStr = TEXT("Success"); break;
		case EGenerationStatus::Updated: StatusStr = TEXT("Updated"); break;
		case EGenerationStatus::Skipped: StatusStr = TEXT("Skipped"); break;
		case EGenerationStatus::Failed: StatusStr = TEXT("Failed"); break;
		}

		ResultObj->SetStringField(TEXT("status"), StatusStr);
		ResultObj->SetStringField(TEXT("assetType"), Result.AssetType);
		ResultObj->SetStringField(TEXT("name"), Result.AssetName);
		ResultObj->SetStringField(TEXT("path"), Result.AssetPath);
		ResultObj->SetStringField(TEXT("fullPath"), Result.GetFullPath());
		ResultObj->SetStringField(TEXT("message"), Result.Message);

		ResultsArray.Add(MakeShared<FJsonValueObject>(ResultObj));
	}
	Json->SetArrayField(TEXT("results"), ResultsArray);

	return Json;
}

FString FAssetFactoryHttpServer::GetServiceDiscoveryFilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetFactory"), TEXT("service.json"));
}

void FAssetFactoryHttpServer::WriteServiceDiscoveryFile()
{
	TSharedPtr<FJsonObject> ServiceInfo = MakeShared<FJsonObject>();

	// Basic info
	ServiceInfo->SetStringField(TEXT("service"), TEXT("AssetFactory"));
	ServiceInfo->SetNumberField(TEXT("port"), CurrentPort);
	ServiceInfo->SetStringField(TEXT("host"), TEXT("localhost"));
	ServiceInfo->SetStringField(TEXT("baseUrl"), FString::Printf(TEXT("http://localhost:%d"), CurrentPort));

	// Endpoints
	TArray<TSharedPtr<FJsonValue>> EndpointsArray;

	// Health endpoint
	{
		TSharedPtr<FJsonObject> Endpoint = MakeShared<FJsonObject>();
		Endpoint->SetStringField(TEXT("method"), TEXT("GET"));
		Endpoint->SetStringField(TEXT("path"), TEXT("/assetfactory/health"));
		Endpoint->SetStringField(TEXT("description"), TEXT("Health check"));
		EndpointsArray.Add(MakeShared<FJsonValueObject>(Endpoint));
	}

	// Generators endpoint
	{
		TSharedPtr<FJsonObject> Endpoint = MakeShared<FJsonObject>();
		Endpoint->SetStringField(TEXT("method"), TEXT("GET"));
		Endpoint->SetStringField(TEXT("path"), TEXT("/assetfactory/generators"));
		Endpoint->SetStringField(TEXT("description"), TEXT("List available asset generators"));
		EndpointsArray.Add(MakeShared<FJsonValueObject>(Endpoint));
	}

	// Generate endpoint
	{
		TSharedPtr<FJsonObject> Endpoint = MakeShared<FJsonObject>();
		Endpoint->SetStringField(TEXT("method"), TEXT("POST"));
		Endpoint->SetStringField(TEXT("path"), TEXT("/assetfactory/generate"));
		Endpoint->SetStringField(TEXT("description"), TEXT("Generate assets from JSON"));
		Endpoint->SetStringField(TEXT("contentType"), TEXT("application/json"));
		EndpointsArray.Add(MakeShared<FJsonValueObject>(Endpoint));
	}

	// Extract endpoint
	{
		TSharedPtr<FJsonObject> Endpoint = MakeShared<FJsonObject>();
		Endpoint->SetStringField(TEXT("method"), TEXT("POST"));
		Endpoint->SetStringField(TEXT("path"), TEXT("/assetfactory/extract"));
		Endpoint->SetStringField(TEXT("description"), TEXT("Extract asset configuration as JSON"));
		Endpoint->SetStringField(TEXT("contentType"), TEXT("application/json"));
		EndpointsArray.Add(MakeShared<FJsonValueObject>(Endpoint));
	}

	// Delete endpoint
	{
		TSharedPtr<FJsonObject> Endpoint = MakeShared<FJsonObject>();
		Endpoint->SetStringField(TEXT("method"), TEXT("POST"));
		Endpoint->SetStringField(TEXT("path"), TEXT("/assetfactory/delete"));
		Endpoint->SetStringField(TEXT("description"), TEXT("Delete assets"));
		Endpoint->SetStringField(TEXT("contentType"), TEXT("application/json"));
		EndpointsArray.Add(MakeShared<FJsonValueObject>(Endpoint));
	}

	ServiceInfo->SetArrayField(TEXT("endpoints"), EndpointsArray);

	// Timestamp
	ServiceInfo->SetStringField(TEXT("startTime"), FDateTime::UtcNow().ToIso8601());

	// Serialize to string
	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(ServiceInfo.ToSharedRef(), Writer);

	// Ensure directory exists
	FString DirectoryPath = FPaths::GetPath(GetServiceDiscoveryFilePath());
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*DirectoryPath))
	{
		PlatformFile.CreateDirectoryTree(*DirectoryPath);
	}

	// Write file
	if (FFileHelper::SaveStringToFile(JsonString, *GetServiceDiscoveryFilePath()))
	{
		UE_LOG(LogAssetFactory, Log, TEXT("Service discovery file written: %s"), *GetServiceDiscoveryFilePath());
	}
	else
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Failed to write service discovery file: %s"), *GetServiceDiscoveryFilePath());
	}
}

void FAssetFactoryHttpServer::DeleteServiceDiscoveryFile()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString FilePath = GetServiceDiscoveryFilePath();

	if (PlatformFile.FileExists(*FilePath))
	{
		if (PlatformFile.DeleteFile(*FilePath))
		{
			UE_LOG(LogAssetFactory, Log, TEXT("Service discovery file deleted: %s"), *FilePath);
		}
		else
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Failed to delete service discovery file: %s"), *FilePath);
		}
	}
}

bool FAssetFactoryHttpServer::HandleQuery(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Parse request body as JSON (properly handle length to avoid reading garbage)
	FString RequestBody;
	if (!Request.Body.IsEmpty())
	{
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		RequestBody = FString(Converter.Length(), Converter.Get());
	}
	else
	{
		RequestBody = TEXT("{}");
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestBody);
	TSharedPtr<FJsonObject> JsonObject;

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		SendErrorResponse(OnComplete, 400, TEXT("Invalid JSON in request body"));
		return true;
	}

	// Get asset path and paths to query
	FString AssetPath;
	if (!JsonObject->TryGetStringField(TEXT("Asset"), AssetPath))
	{
		SendErrorResponse(OnComplete, 400, TEXT("Missing 'Asset' field"));
		return true;
	}

	// Get paths array
	TArray<FString> Paths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArray;
	if (JsonObject->TryGetArrayField(TEXT("Paths"), PathsArray))
	{
		for (const TSharedPtr<FJsonValue>& PathValue : *PathsArray)
		{
			FString Path;
			if (PathValue->TryGetString(Path))
			{
				Paths.Add(Path);
			}
		}
	}

	// Single path alternative
	FString SinglePath;
	if (JsonObject->TryGetStringField(TEXT("Path"), SinglePath))
	{
		Paths.Add(SinglePath);
	}

	if (Paths.Num() == 0)
	{
		SendErrorResponse(OnComplete, 400, TEXT("Missing 'Paths' array or 'Path' string"));
		return true;
	}

	// Get subsystem
	UAssetFactorySubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetFactorySubsystem>() : nullptr;
	if (!Subsystem)
	{
		SendErrorResponse(OnComplete, 500, TEXT("AssetFactory subsystem not available"));
		return true;
	}

	// Extract asset config (must be on game thread)
	TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> ExtractedConfig;

	auto DoQuery = [&]()
	{
		ExtractedConfig = Subsystem->ExtractAsset(AssetPath, false);
	};

	if (IsInGameThread())
	{
		DoQuery();
	}
	else
	{
		FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			DoQuery();
			CompletionEvent->Trigger();
		});
		CompletionEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
	}

	if (!ExtractedConfig.IsValid())
	{
		SendErrorResponse(OnComplete, 404, FString::Printf(TEXT("Failed to extract asset: %s"), *AssetPath));
		return true;
	}

	// Resolve paths
	TSharedPtr<FJsonObject> Results = MakeShared<FJsonObject>();
	for (const FString& Path : Paths)
	{
		TSharedPtr<FJsonValue> Value = FPropertyPathResolver::Resolve(ExtractedConfig, Path);
		if (Value.IsValid())
		{
			Results->SetField(Path, Value);
		}
		else
		{
			Results->SetField(Path, MakeShared<FJsonValueNull>());
		}
	}

	ResponseJson->SetBoolField(TEXT("success"), true);
	ResponseJson->SetStringField(TEXT("asset"), AssetPath);
	ResponseJson->SetObjectField(TEXT("results"), Results);

	SendJsonResponse(OnComplete, 200, ResponseJson);
	return true;
}
