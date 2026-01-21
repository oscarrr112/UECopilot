// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/OpenAICompatibleService.h"
#include "UECopilotSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

DEFINE_LOG_CATEGORY(LogAIService);

UOpenAICompatibleService* UOpenAICompatibleService::Instance = nullptr;

UOpenAICompatibleService::UOpenAICompatibleService()
{
}

UOpenAICompatibleService* UOpenAICompatibleService::Get()
{
	if (!Instance)
	{
		Instance = NewObject<UOpenAICompatibleService>();
		Instance->AddToRoot(); // Prevent garbage collection
	}
	return Instance;
}

void UOpenAICompatibleService::SendChatRequest(const TArray<FChatMessage>& Messages, FOnAIResponseReceived OnComplete)
{
	static int32 RequestCounter = 0;
	int32 ThisRequestId = ++RequestCounter;

	UE_LOG(LogAIService, Log, TEXT("[Request %d] Starting new chat request"), ThisRequestId);

	if (bRequestInProgress)
	{
		UE_LOG(LogAIService, Warning, TEXT("[Request %d] Blocked - another request is in progress"), ThisRequestId);
		FAIResponse ErrorResponse;
		ErrorResponse.bSuccess = false;
		ErrorResponse.ErrorMessage = TEXT("A request is already in progress");
		OnComplete.ExecuteIfBound(ErrorResponse);
		return;
	}

	UUECopilotSettings* Settings = UUECopilotSettings::Get();
	if (!Settings)
	{
		FAIResponse ErrorResponse;
		ErrorResponse.bSuccess = false;
		ErrorResponse.ErrorMessage = TEXT("Failed to get settings");
		OnComplete.ExecuteIfBound(ErrorResponse);
		return;
	}

	FString Endpoint = Settings->GetEndpointURL();
	FString APIKey = Settings->GetAPIKey();

	if (Endpoint.IsEmpty())
	{
		FAIResponse ErrorResponse;
		ErrorResponse.bSuccess = false;
		ErrorResponse.ErrorMessage = TEXT("API endpoint not configured");
		OnComplete.ExecuteIfBound(ErrorResponse);
		return;
	}

	CurrentRequestId = ThisRequestId;
	ResponseCallback = OnComplete;
	bRequestInProgress = true;

	// Create HTTP request
	CurrentRequest = FHttpModule::Get().CreateRequest();
	CurrentRequest->SetURL(Endpoint);
	CurrentRequest->SetVerb(TEXT("POST"));
	CurrentRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	if (!APIKey.IsEmpty())
	{
		CurrentRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *APIKey));
	}

	// Set timeout - convert to float for SetTimeout
	float TimeoutSeconds = static_cast<float>(Settings->RequestTimeoutSeconds);
	CurrentRequest->SetTimeout(TimeoutSeconds);

	// Also try to set activity timeout if available (UE 5.4+)
	// This controls how long to wait for data between chunks
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	CurrentRequest->SetActivityTimeout(TimeoutSeconds);
#endif

	UE_LOG(LogAIService, Log, TEXT("[Request %d] Setting timeout to %.0f seconds (from settings: %d)"),
		ThisRequestId, TimeoutSeconds, Settings->RequestTimeoutSeconds);

	FString RequestBody = BuildRequestBody(Messages, false);
	int32 RequestSizeBytes = RequestBody.Len() * sizeof(TCHAR);
	UE_LOG(LogAIService, Log, TEXT("[Request %d] Body size: %d bytes (%d characters)"),
		ThisRequestId, RequestSizeBytes, RequestBody.Len());

	// Warn if request is very large
	if (RequestBody.Len() > 50000)
	{
		UE_LOG(LogAIService, Warning, TEXT("[Request %d] Request body is very large (%d chars), this may cause issues"), ThisRequestId, RequestBody.Len());
	}

	CurrentRequest->SetContentAsString(RequestBody);
	CurrentRequest->OnProcessRequestComplete().BindUObject(this, &UOpenAICompatibleService::OnHttpRequestComplete);

	UE_LOG(LogAIService, Log, TEXT("[Request %d] Sending to: %s"), ThisRequestId, *Endpoint);

	double StartTime = FPlatformTime::Seconds();
	RequestStartTime = StartTime;

	CurrentRequest->ProcessRequest();
}

void UOpenAICompatibleService::SendChatRequestStreaming(
	const TArray<FChatMessage>& Messages,
	FOnAIStreamChunk OnChunk,
	FOnAIStreamComplete OnComplete,
	FOnAIError OnError)
{
	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A request is already in progress"));
		return;
	}

	UUECopilotSettings* Settings = UUECopilotSettings::Get();
	if (!Settings)
	{
		OnError.ExecuteIfBound(TEXT("Failed to get settings"));
		return;
	}

	FString Endpoint = Settings->GetEndpointURL();
	FString APIKey = Settings->GetAPIKey();

	if (Endpoint.IsEmpty())
	{
		OnError.ExecuteIfBound(TEXT("API endpoint not configured"));
		return;
	}

	StreamChunkCallback = OnChunk;
	StreamCompleteCallback = OnComplete;
	StreamErrorCallback = OnError;
	AccumulatedResponse.Empty();
	bRequestInProgress = true;

	// Create HTTP request
	CurrentRequest = FHttpModule::Get().CreateRequest();
	CurrentRequest->SetURL(Endpoint);
	CurrentRequest->SetVerb(TEXT("POST"));
	CurrentRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	CurrentRequest->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));

	if (!APIKey.IsEmpty())
	{
		CurrentRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *APIKey));
	}

	// Set timeout
	float TimeoutSeconds = static_cast<float>(Settings->RequestTimeoutSeconds);
	CurrentRequest->SetTimeout(TimeoutSeconds);
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	CurrentRequest->SetActivityTimeout(TimeoutSeconds);
#endif
	UE_LOG(LogAIService, Log, TEXT("Streaming request timeout set to %.0f seconds"), TimeoutSeconds);

	// Note: Using non-streaming request, then parsing SSE response at completion
	// True streaming requires platform-specific implementation in UE 5.7+
	CurrentRequest->SetContentAsString(BuildRequestBody(Messages, true));

	CurrentRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
	{
		bRequestInProgress = false;
		CurrentRequest = nullptr;

		if (bSuccess && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			// Parse SSE response and extract all chunks
			FString Content = Response->GetContentAsString();
			TArray<FString> Lines;
			Content.ParseIntoArrayLines(Lines);

			FString FullResponse;
			for (const FString& Line : Lines)
			{
				if (Line.StartsWith(TEXT("data: ")))
				{
					FString Data = Line.Mid(6);
					if (Data == TEXT("[DONE]"))
					{
						continue;
					}

					FString Chunk = ParseStreamChunk(Data);
					if (!Chunk.IsEmpty())
					{
						FullResponse += Chunk;
						StreamChunkCallback.ExecuteIfBound(Chunk);
					}
				}
			}

			AccumulatedResponse = FullResponse;
			StreamCompleteCallback.ExecuteIfBound();
		}
		else
		{
			FString ErrorMessage = TEXT("Request failed");
			if (Response.IsValid())
			{
				ErrorMessage = FString::Printf(TEXT("HTTP %d: %s"), Response->GetResponseCode(), *Response->GetContentAsString());
			}
			StreamErrorCallback.ExecuteIfBound(ErrorMessage);
		}
	});

	UE_LOG(LogAIService, Log, TEXT("Sending streaming chat request to: %s"), *Endpoint);
	CurrentRequest->ProcessRequest();
}

void UOpenAICompatibleService::CancelRequest()
{
	if (CurrentRequest.IsValid())
	{
		CurrentRequest->CancelRequest();
		CurrentRequest = nullptr;
	}
	bRequestInProgress = false;
}

FString UOpenAICompatibleService::BuildRequestBody(const TArray<FChatMessage>& Messages, bool bStream) const
{
	UUECopilotSettings* Settings = UUECopilotSettings::Get();

	TSharedRef<FJsonObject> RequestObject = MakeShared<FJsonObject>();
	RequestObject->SetStringField(TEXT("model"), Settings->GetModelName());
	RequestObject->SetNumberField(TEXT("max_tokens"), Settings->MaxTokens);
	RequestObject->SetNumberField(TEXT("temperature"), Settings->Temperature);
	RequestObject->SetBoolField(TEXT("stream"), bStream);

	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const FChatMessage& Message : Messages)
	{
		TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();

		FString RoleString;
		switch (Message.Role)
		{
		case EChatMessageRole::System:
			RoleString = TEXT("system");
			break;
		case EChatMessageRole::User:
			RoleString = TEXT("user");
			break;
		case EChatMessageRole::Assistant:
			RoleString = TEXT("assistant");
			break;
		}

		MessageObject->SetStringField(TEXT("role"), RoleString);
		MessageObject->SetStringField(TEXT("content"), Message.Content);
		MessagesArray.Add(MakeShared<FJsonValueObject>(MessageObject));
	}
	RequestObject->SetArrayField(TEXT("messages"), MessagesArray);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RequestObject, Writer);

	return RequestBody;
}

FAIResponse UOpenAICompatibleService::ParseResponse(const FString& ResponseContent) const
{
	FAIResponse Result;

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Failed to parse JSON response");
		return Result;
	}

	// Check for error
	if (JsonObject->HasField(TEXT("error")))
	{
		const TSharedPtr<FJsonObject>* ErrorObject;
		if (JsonObject->TryGetObjectField(TEXT("error"), ErrorObject))
		{
			Result.ErrorMessage = (*ErrorObject)->GetStringField(TEXT("message"));
		}
		Result.bSuccess = false;
		return Result;
	}

	// Parse choices
	const TArray<TSharedPtr<FJsonValue>>* ChoicesArray;
	if (JsonObject->TryGetArrayField(TEXT("choices"), ChoicesArray) && ChoicesArray->Num() > 0)
	{
		const TSharedPtr<FJsonObject>* FirstChoice;
		if ((*ChoicesArray)[0]->TryGetObject(FirstChoice))
		{
			const TSharedPtr<FJsonObject>* MessageObject;
			if ((*FirstChoice)->TryGetObjectField(TEXT("message"), MessageObject))
			{
				Result.Content = (*MessageObject)->GetStringField(TEXT("content"));
				Result.bSuccess = true;
			}
		}
	}

	// Parse usage
	const TSharedPtr<FJsonObject>* UsageObject;
	if (JsonObject->TryGetObjectField(TEXT("usage"), UsageObject))
	{
		Result.PromptTokens = (*UsageObject)->GetIntegerField(TEXT("prompt_tokens"));
		Result.CompletionTokens = (*UsageObject)->GetIntegerField(TEXT("completion_tokens"));
		Result.TotalTokens = (*UsageObject)->GetIntegerField(TEXT("total_tokens"));
	}

	return Result;
}

FString UOpenAICompatibleService::ParseStreamChunk(const FString& ChunkData) const
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ChunkData);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return TEXT("");
	}

	const TArray<TSharedPtr<FJsonValue>>* ChoicesArray;
	if (JsonObject->TryGetArrayField(TEXT("choices"), ChoicesArray) && ChoicesArray->Num() > 0)
	{
		const TSharedPtr<FJsonObject>* FirstChoice;
		if ((*ChoicesArray)[0]->TryGetObject(FirstChoice))
		{
			const TSharedPtr<FJsonObject>* DeltaObject;
			if ((*FirstChoice)->TryGetObjectField(TEXT("delta"), DeltaObject))
			{
				FString Content;
				if ((*DeltaObject)->TryGetStringField(TEXT("content"), Content))
				{
					return Content;
				}
			}
		}
	}

	return TEXT("");
}

void UOpenAICompatibleService::OnHttpRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
	double ElapsedTime = FPlatformTime::Seconds() - RequestStartTime;
	int32 CompletedRequestId = CurrentRequestId;

	UE_LOG(LogAIService, Log, TEXT("[Request %d] Response received after %.2f seconds, bSuccess=%s"),
		CompletedRequestId, ElapsedTime, bSuccess ? TEXT("true") : TEXT("false"));

	bRequestInProgress = false;
	CurrentRequest = nullptr;

	FAIResponse Result;

	if (!bSuccess)
	{
		Result.bSuccess = false;

		// Try to get more detailed error info
		if (Request.IsValid())
		{
			EHttpRequestStatus::Type Status = Request->GetStatus();
			FString StatusName;
			switch (Status)
			{
			case EHttpRequestStatus::NotStarted:
				StatusName = TEXT("NotStarted");
				Result.ErrorMessage = TEXT("HTTP request was not started");
				break;
			case EHttpRequestStatus::Processing:
				StatusName = TEXT("Processing");
				Result.ErrorMessage = TEXT("HTTP request is still processing (timeout?)");
				break;
			case EHttpRequestStatus::Failed:
				StatusName = TEXT("Failed");
				Result.ErrorMessage = TEXT("HTTP request failed - connection error or network issue. Check your internet connection and API endpoint URL.");
				break;
			case EHttpRequestStatus::Succeeded:
				StatusName = TEXT("Succeeded");
				Result.ErrorMessage = TEXT("Request completed but marked as failed");
				break;
			default:
				StatusName = FString::Printf(TEXT("Unknown(%d)"), (int32)Status);
				Result.ErrorMessage = FString::Printf(TEXT("HTTP request failed with status: %d"), (int32)Status);
				break;
			}
			UE_LOG(LogAIService, Warning, TEXT("[Request %d] HTTP Status: %s, Elapsed: %.2fs"),
				CompletedRequestId, *StatusName, ElapsedTime);
		}
		else
		{
			Result.ErrorMessage = TEXT("HTTP request failed - unknown error");
		}

		UE_LOG(LogAIService, Error, TEXT("[Request %d] %s"), CompletedRequestId, *Result.ErrorMessage);
		ResponseCallback.ExecuteIfBound(Result);
		return;
	}

	if (!Response.IsValid())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Invalid HTTP response received");
		UE_LOG(LogAIService, Error, TEXT("[Request %d] %s"), CompletedRequestId, *Result.ErrorMessage);
		ResponseCallback.ExecuteIfBound(Result);
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	FString ResponseContent = Response->GetContentAsString();

	UE_LOG(LogAIService, Log, TEXT("[Request %d] Response code: %d, Content size: %d bytes"),
		CompletedRequestId, ResponseCode, ResponseContent.Len());
	UE_LOG(LogAIService, Verbose, TEXT("[Request %d] Response content: %s"), CompletedRequestId, *ResponseContent);

	if (!EHttpResponseCodes::IsOk(ResponseCode))
	{
		Result.bSuccess = false;
		if (ResponseCode == 401)
		{
			Result.ErrorMessage = TEXT("Authentication failed (401) - check your API key");
		}
		else if (ResponseCode == 403)
		{
			Result.ErrorMessage = TEXT("Access forbidden (403) - API key may not have permission");
		}
		else if (ResponseCode == 404)
		{
			Result.ErrorMessage = TEXT("Endpoint not found (404) - check your API endpoint URL");
		}
		else if (ResponseCode == 429)
		{
			Result.ErrorMessage = TEXT("Rate limit exceeded (429) - please wait and try again");
		}
		else if (ResponseCode >= 500)
		{
			Result.ErrorMessage = FString::Printf(TEXT("Server error (%d) - the API service may be down"), ResponseCode);
		}
		else
		{
			Result.ErrorMessage = FString::Printf(TEXT("HTTP Error %d: %s"), ResponseCode, *ResponseContent);
		}
		UE_LOG(LogAIService, Error, TEXT("[Request %d] %s"), CompletedRequestId, *Result.ErrorMessage);
		ResponseCallback.ExecuteIfBound(Result);
		return;
	}

	Result = ParseResponse(ResponseContent);
	UE_LOG(LogAIService, Log, TEXT("[Request %d] Successfully parsed response, Content length: %d chars"),
		CompletedRequestId, Result.Content.Len());
	ResponseCallback.ExecuteIfBound(Result);
}
