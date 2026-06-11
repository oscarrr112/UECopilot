// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class IHttpRouter;
class UAssetFactorySubsystem;
struct FHttpRouteHandleInternal;
struct FHttpServerRequest;
struct FHttpServerResponse;

enum class EHttpServerRequestVerbs : uint16;
using FHttpRouteHandle = TSharedPtr<const FHttpRouteHandleInternal>;
using FHttpResultCallback = TFunction<void(TUniquePtr<FHttpServerResponse>&& Response)>;
using FHttpRequestHandler = TDelegate<bool(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)>;
using FAssetFactoryExternalRouteHandler = TDelegate<TSharedPtr<FJsonObject>(const FString& RequestBody, const TMap<FString, FString>& QueryParams, int32& OutStatusCode)>;

struct ASSETFACTORY_API FAssetFactoryExternalRoute
{
	FString Path;
	FAssetFactoryExternalRouteHandler Handler;
	FString MethodText;
	FString Description;
	bool bRequiresBody = false;
};

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
 *   GET  /assetfactory/screenshot - Capture Level Viewport as base64 JPEG
 *   POST /assetfactory/extract_bsl - Decompile Blueprint asset to BSL text
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

	/** Register an external route to bind on the next HTTP server start */
	static FDelegateHandle RegisterExternalRoute(const FAssetFactoryExternalRoute& Route);

	/** Unregister a previously registered external route */
	static void UnregisterExternalRoute(FDelegateHandle Handle);

private:
	static TMap<FDelegateHandle, FAssetFactoryExternalRoute>& GetExternalRoutes();
	static TSet<FAssetFactoryHttpServer*>& GetActiveServers();

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

	/** Handle GET /screenshot request - capture Level Viewport as base64 JPEG */
	bool HandleGetViewportScreenshot(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle POST /execute request - execute Python code in the editor */
	bool HandleExecute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle POST /datatable/rows request - update specific rows in a DataTable */
	bool HandleUpdateDataTableRows(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle POST /extract_bsl request - decompile Blueprint to BSL text */
	bool HandleExtractBSL(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle POST /apply_bsl request - compile BSL source and apply to Blueprint */
	bool HandleApplyBSL(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Handle POST /extract_graph request - export Blueprint node graph as JSON */
	bool HandleExtractGraph(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Send JSON response */
	void SendJsonResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, TSharedPtr<FJsonObject> JsonResponse);

	/** Send error response */
	void SendErrorResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, const FString& ErrorMessage);

	/** Convert FGenerationReport to JSON */
	TSharedPtr<FJsonObject> ReportToJson(const struct FGenerationReport& Report);

	void BindExternalRoutes();
	void UnbindExternalRoutes();

private:
	/** Route handles for cleanup */
	TArray<FHttpRouteHandle> RouteHandles;

	/** External route handles for cleanup */
	TArray<FHttpRouteHandle> ExternalRouteHandles;

	/** Server state */
	bool bIsRunning = false;
	uint32 CurrentPort = 0;

	/** HTTP Router */
	TSharedPtr<IHttpRouter> HttpRouter;
};
