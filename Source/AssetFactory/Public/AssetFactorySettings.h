// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AssetFactorySettings.generated.h"

/**
 * Settings for the AssetFactory plugin
 * Configure HTTP Server and other options
 *
 * Settings are saved to Config/DefaultEditorPerProjectUserSettings.ini
 */
UCLASS(config = EditorPerProjectUserSettings, defaultconfig)
class ASSETFACTORY_API UAssetFactorySettings : public UObject
{
	GENERATED_BODY()

public:
	UAssetFactorySettings();

	/** Get the singleton settings instance */
	static UAssetFactorySettings* Get();

	//~ HTTP Server Settings

	/** Enable HTTP Server for external tool integration (AI Agents, MCP, etc.) */
	UPROPERTY(config, EditAnywhere, Category = "HTTP Server")
	bool bEnableHttpServer = true;

	/** Port for HTTP Server (default: 8558) */
	UPROPERTY(config, EditAnywhere, Category = "HTTP Server", meta = (EditCondition = "bEnableHttpServer", ClampMin = "1024", ClampMax = "65535"))
	int32 HttpServerPort = 8558;

	/** Allow connections from remote hosts (default: false, localhost only) */
	UPROPERTY(config, EditAnywhere, Category = "HTTP Server", meta = (EditCondition = "bEnableHttpServer"))
	bool bAllowRemoteConnections = false;

	//~ Logging Settings

	/** Log HTTP requests for debugging */
	UPROPERTY(config, EditAnywhere, Category = "Logging")
	bool bLogHttpRequests = false;
};
