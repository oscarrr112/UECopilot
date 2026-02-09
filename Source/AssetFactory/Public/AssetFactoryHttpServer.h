// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpRouteHandle.h"

class UAssetFactorySubsystem;

/**
 * HTTP Server for AssetFactory
 * Exposes asset generation and extraction functionality via HTTP endpoints
 *
 * Endpoints:
 *   POST /assetfactory/generate - Generate assets from JSON
 *   POST /assetfactory/extract - Extract asset configuration as JSON
 *   POST /assetfactory/delete - Delete assets
 *   POST /assetfactory/query - Query extracted JSON using property paths
 *   GET  /assetfactory/generators - List available asset generators
 *   GET  /assetfactory/health - Health check
 *   GET  /assetfactory/context - Get current editor state (selected actors, assets, level, etc.)
 *   POST /assetfactory/execute - Execute Python code in the editor
 *
 * Service Discovery:
 *   On startup, writes service info to {ProjectDir}/Saved/AssetFactory/service.json
 *   AI Agents can read this file to discover available endpoints dynamically.
 *
 * Example usage (generate):
 *   curl -X POST http://localhost:8559/assetfactory/generate \
 *     -H "Content-Type: application/json" \
 *     -d '{"Assets":[{"AssetType":"Blueprint","Name":"BP_Test","Path":"/Game/Test","Action":"CreateOrUpdate","ParentClass":"Actor"}]}'
 *
 * Example usage (extract):
 *   curl -X POST http://localhost:8559/assetfactory/extract \
 *     -H "Content-Type: application/json" \
 *     -d '{"Assets":["/Game/Test/BP_Test"]}'
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

	/** Get the path to the service discovery file */
	static FString GetServiceDiscoveryFilePath();

private:
	/** Write service discovery file for AI agents */
	void WriteServiceDiscoveryFile();

	/** Delete service discovery file */
	void DeleteServiceDiscoveryFile();

	/** Handle POST /generate request */
	bool HandleGenerate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle GET /generators request */
	bool HandleListGenerators(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle GET /health request */
	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle POST /extract request */
	bool HandleExtract(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle POST /query request */
	bool HandleQuery(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle POST /delete request */
	bool HandleDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle GET /context request - returns current editor state */
	bool HandleContext(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle POST /execute request - execute Python code in the editor */
	bool HandleExecute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Send JSON response */
	void SendJsonResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, TSharedPtr<FJsonObject> JsonResponse);

	/** Send error response */
	void SendErrorResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, const FString& ErrorMessage);

	/** Convert FGenerationReport to JSON */
	TSharedPtr<FJsonObject> ReportToJson(const struct FGenerationReport& Report);

private:
	/** Route handles for cleanup */
	FHttpRouteHandle GenerateRouteHandle;
	FHttpRouteHandle ExtractRouteHandle;
	FHttpRouteHandle DeleteRouteHandle;
	FHttpRouteHandle QueryRouteHandle;
	FHttpRouteHandle GeneratorsRouteHandle;
	FHttpRouteHandle HealthRouteHandle;
	FHttpRouteHandle ContextRouteHandle;
	FHttpRouteHandle ExecuteRouteHandle;

	/** Server state */
	bool bIsRunning = false;
	uint32 CurrentPort = 0;

	/** HTTP Router */
	TSharedPtr<IHttpRouter> HttpRouter;
};
