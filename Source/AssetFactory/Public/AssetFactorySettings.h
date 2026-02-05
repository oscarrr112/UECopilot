// Copyright ProjectRPG. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AssetFactorySettings.generated.h"

/**
 * Settings for the AssetFactory plugin
 * Configure HTTP Server and other options
 *
 * Access via Project Settings -> Plugins -> Asset Factory
 */
UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "Asset Factory"))
class ASSETFACTORY_API UAssetFactorySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAssetFactorySettings();

	/** Get the singleton settings instance */
	static UAssetFactorySettings* Get();

	//~ UDeveloperSettings interface
	virtual FName GetContainerName() const override { return FName("Project"); }
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	virtual FName GetSectionName() const override { return FName("Asset Factory"); }

	//~ HTTP Server Settings

	/** Enable HTTP Server for external tool integration (AI Agents, MCP, etc.) */
	UPROPERTY(config, EditAnywhere, Category = "HTTP Server")
	bool bEnableHttpServer = true;

	/** Port for HTTP Server (default: 8559, note: 8558 is used by UE Zen Server) */
	UPROPERTY(config, EditAnywhere, Category = "HTTP Server", meta = (EditCondition = "bEnableHttpServer", ClampMin = "1024", ClampMax = "65535"))
	int32 HttpServerPort = 8559;

	/** Allow connections from remote hosts (default: false, localhost only) */
	UPROPERTY(config, EditAnywhere, Category = "HTTP Server", meta = (EditCondition = "bEnableHttpServer"))
	bool bAllowRemoteConnections = false;

	//~ Logging Settings

	/** Log HTTP requests for debugging */
	UPROPERTY(config, EditAnywhere, Category = "Logging")
	bool bLogHttpRequests = false;
};
