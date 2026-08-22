#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "WSOnlineSettings.generated.h"

/**
 * Backend connection settings, editable in Project Settings and per-platform
 * config. The base URL is the only thing that differs between a dev machine
 * (127.0.0.1) and a phone on the LAN, so it must never be hardcoded.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "World Sports Online"))
class WORLDSPORTS_API UWSOnlineSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** FastAPI origin, no trailing slash, e.g. http://127.0.0.1:8000 */
	UPROPERTY(Config, EditAnywhere, Category = "Backend")
	FString BaseUrl = TEXT("http://127.0.0.1:8000");

	/** Per-request timeout in seconds. */
	UPROPERTY(Config, EditAnywhere, Category = "Backend", meta = (ClampMin = "1", ClampMax = "120"))
	float RequestTimeoutSeconds = 15.0f;

	/** Retries for idempotent requests on transport failure / 5xx. */
	UPROPERTY(Config, EditAnywhere, Category = "Backend", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxRetries = 3;

	/** First backoff delay; doubles per retry (1s, 2s, 4s ...). */
	UPROPERTY(Config, EditAnywhere, Category = "Backend", meta = (ClampMin = "0.1", ClampMax = "30"))
	float InitialBackoffSeconds = 1.0f;
};
