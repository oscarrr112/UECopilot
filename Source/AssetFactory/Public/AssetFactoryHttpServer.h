// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpRouteHandle.h"

class UAssetFactorySubsystem;

/**
 * HTTP Server for AssetFactory
 * Exposes asset generation functionality via HTTP endpoints
 *
 * Endpoints:
 *   POST /generate - Generate assets from JSON
 *   GET  /generators - List available asset generators
 *   GET  /health - Health check
 *
 * Example usage:
 *   curl -X POST http://localhost:8558/assetfactory/generate \
 *     -H "Content-Type: application/json" \
 *     -d '{"Assets":[{"AssetType":"Blueprint","Name":"BP_Test","Path":"/Game/Test","Action":"CreateOrUpdate","ParentClass":"Actor"}]}'
 */
class ASSETFACTORY_API FAssetFactoryHttpServer
{
public:
	FAssetFactoryHttpServer();
	~FAssetFactoryHttpServer();

	/** Start the HTTP server on the specified port */
	bool Start(uint32 Port = 8558);

	/** Stop the HTTP server */
	void Stop();

	/** Check if server is running */
	bool IsRunning() const { return bIsRunning; }

	/** Get the port the server is running on */
	uint32 GetPort() const { return CurrentPort; }

private:
	/** Handle POST /generate request */
	bool HandleGenerate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle GET /generators request */
	bool HandleListGenerators(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle GET /health request */
	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Send JSON response */
	void SendJsonResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, TSharedPtr<FJsonObject> JsonResponse);

	/** Send error response */
	void SendErrorResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, const FString& ErrorMessage);

	/** Convert FGenerationReport to JSON */
	TSharedPtr<FJsonObject> ReportToJson(const struct FGenerationReport& Report);

private:
	/** Route handles for cleanup */
	FHttpRouteHandle GenerateRouteHandle;
	FHttpRouteHandle GeneratorsRouteHandle;
	FHttpRouteHandle HealthRouteHandle;

	/** Server state */
	bool bIsRunning = false;
	uint32 CurrentPort = 0;

	/** HTTP Router */
	TSharedPtr<IHttpRouter> HttpRouter;
};
