#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WSAnalyticsSubsystem.generated.h"

/**
 * Buffered analytics dispatch to POST /api/v1/analytics/events.
 *
 * Events buffer locally and ship in batches (<= 100, the server's cap).
 * Anonymous (pre-login) events are fine — the endpoint accepts them. A
 * failed batch returns to the buffer; the buffer itself is capped and drops
 * the OLDEST events first, because recent behaviour is worth more than old.
 */
UCLASS()
class WORLDSPORTS_API UWSAnalyticsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Queue one event, e.g. Track("GameStarted", {{"event","sprint-100m"}}).
	 * Names outside the server's allowlist are rejected per-event server-side
	 * and reported in the batch response — watch the log in dev. */
	UFUNCTION(BlueprintCallable, Category = "WorldSports|Analytics")
	void Track(const FString& Name, const TMap<FString, FString>& Properties);

	/** Send the buffer now instead of waiting for the timer. */
	void Flush();

	UFUNCTION(BlueprintPure, Category = "WorldSports|Analytics")
	int32 GetBufferedCount() const { return Buffer.Num(); }

private:
	struct FPendingEvent
	{
		FString Name;
		TMap<FString, FString> Properties;
		FDateTime ClientTs;
	};

	bool TickFlush(float DeltaTime);

	TArray<FPendingEvent> Buffer;
	bool bFlushInFlight = false;
	FTSTicker::FDelegateHandle TickerHandle;
};
