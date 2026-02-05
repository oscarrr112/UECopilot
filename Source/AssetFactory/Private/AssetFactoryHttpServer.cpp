// Copyright ProjectRPG. All Rights Reserved.

#include "AssetFactoryHttpServer.h"
#include "AssetFactoryModule.h"
#include "AssetFactorySubsystem.h"
#include "AssetGeneratorRegistry.h"
#include "GenerationTypes.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"

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

	UE_LOG(LogAssetFactory, Display, TEXT("AssetFactory HTTP Server started on port %d"), Port);
	UE_LOG(LogAssetFactory, Display, TEXT("  POST http://localhost:%d/assetfactory/generate"), Port);
	UE_LOG(LogAssetFactory, Display, TEXT("  GET  http://localhost:%d/assetfactory/generators"), Port);
	UE_LOG(LogAssetFactory, Display, TEXT("  GET  http://localhost:%d/assetfactory/health"), Port);

	return true;
}

void FAssetFactoryHttpServer::Stop()
{
	if (!bIsRunning)
	{
		return;
	}

	// Unbind routes
	if (HttpRouter.IsValid())
	{
		HttpRouter->UnbindRoute(GenerateRouteHandle);
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
	// Parse request body as JSON
	FString RequestBody = Request.Body.IsEmpty()
		? FString()
		: FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Request.Body.GetData())));

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
