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
	if (bRequestInProgress)
	{
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

	CurrentRequest->SetTimeout(Settings->RequestTimeoutSeconds);
	CurrentRequest->SetContentAsString(BuildRequestBody(Messages, false));
	CurrentRequest->OnProcessRequestComplete().BindUObject(this, &UOpenAICompatibleService::OnHttpRequestComplete);

	UE_LOG(LogAIService, Log, TEXT("Sending chat request to: %s"), *Endpoint);
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

	CurrentRequest->SetTimeout(Settings->RequestTimeoutSeconds);
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
	bRequestInProgress = false;
	CurrentRequest = nullptr;

	FAIResponse Result;

	if (!bSuccess || !Response.IsValid())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("HTTP request failed");
		ResponseCallback.ExecuteIfBound(Result);
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	FString ResponseContent = Response->GetContentAsString();

	UE_LOG(LogAIService, Verbose, TEXT("Response code: %d"), ResponseCode);
	UE_LOG(LogAIService, Verbose, TEXT("Response content: %s"), *ResponseContent);

	if (!EHttpResponseCodes::IsOk(ResponseCode))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("HTTP Error %d: %s"), ResponseCode, *ResponseContent);
		ResponseCallback.ExecuteIfBound(Result);
		return;
	}

	Result = ParseResponse(ResponseContent);
	ResponseCallback.ExecuteIfBound(Result);
}
