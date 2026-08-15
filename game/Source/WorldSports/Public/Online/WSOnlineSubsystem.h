#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Online/WSDtos.h"
#include "Sports/Results/WSEventResult.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WSOnlineSubsystem.generated.h"

/** Outcome of one HTTP exchange. bTransportOk=false means no HTTP response
 * at all (offline, DNS, timeout) — retryable. With bTransportOk=true the
 * status code is the server's actual answer and is NOT retried. */
struct FWSHttpResult
{
	bool bTransportOk = false;
	int32 StatusCode = 0;
	TSharedPtr<FJsonObject> Json;
	FString Body;
	FString ErrorText;

	bool IsSuccess() const { return bTransportOk && StatusCode >= 200 && StatusCode < 300; }
};

using FWSHttpCallback = TFunction<void(const FWSHttpResult&)>;
using FWSUserCallback = TFunction<void(bool bOk, const FWSUserDto&, const FString& Error)>;

/** How a submitted result ended up. */
enum class EWSSubmitOutcome : uint8
{
	Accepted,      // server validated it and awarded XP
	Rejected,      // server answered: not a valid performance (reason attached)
	Queued,        // no connectivity; persisted locally, will retry
	Failed         // server answered with an unexpected error status
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWSOnAuthChanged, bool, bSignedIn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWSOnResultSubmitted, const FWSResultResponse&, Response, bool, bAccepted);

using FWSSubmitCallback = TFunction<void(EWSSubmitOutcome, const FWSResultResponse&, const FString& Error)>;

/**
 * HTTP client for the FastAPI backend: auth token lifecycle, retry with
 * exponential backoff for idempotent requests, and a persistent offline
 * queue for result submissions (a result recorded with no connectivity must
 * never be lost — architecture §6).
 *
 * The access token lives in memory only. Persisting a bearer token to an
 * unencrypted SaveGame would hand it to anyone with filesystem access; until
 * there is a platform keystore integration, a new session logs in again.
 */
UCLASS()
class WORLDSPORTS_API UWSOnlineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// -- Auth ---------------------------------------------------------------

	void Login(const FString& Email, const FString& Password, FWSUserCallback Callback);
	void RegisterAccount(const FString& Email, const FString& Password,
		const FString& DisplayName, FWSUserCallback Callback);
	void Logout();

	UFUNCTION(BlueprintPure, Category = "WorldSports|Online")
	bool IsSignedIn() const { return !AccessToken.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Online")
	const FWSUserDto& GetSignedInUser() const { return SignedInUser; }

	UPROPERTY(BlueprintAssignable, Category = "WorldSports|Online")
	FWSOnAuthChanged OnAuthChanged;

	// -- Results ------------------------------------------------------------

	/**
	 * Persist the result to the offline queue FIRST, then try the network.
	 * Callback fires with Accepted/Rejected/Failed once the server answers,
	 * or Queued immediately when there is no connectivity.
	 */
	void SubmitResult(const FWSEventResult& Result, FWSSubmitCallback Callback = nullptr);

	/** Re-attempt everything in the offline queue, oldest first. */
	void FlushOfflineQueue();

	UFUNCTION(BlueprintPure, Category = "WorldSports|Online")
	int32 GetOfflineQueueDepth() const { return OfflineQueue.Num(); }

	UPROPERTY(BlueprintAssignable, Category = "WorldSports|Online")
	FWSOnResultSubmitted OnResultSubmitted;

	// -- Generic ------------------------------------------------------------

	/**
	 * One JSON request against the backend. Bearer header when signed in and
	 * bAuthorized. GET/PUT retry with exponential backoff on transport
	 * failure or 5xx; POST/DELETE never auto-retry (they may not be
	 * idempotent — the offline queue owns result resubmission instead).
	 */
	void Request(const FString& Verb, const FString& Path,
		const TSharedPtr<FJsonObject>& Body, FWSHttpCallback Callback,
		bool bAuthorized = true);

	/** The queue serialization format, exposed for tests. */
	static TArray<FString> SerializeQueue(const TArray<FWSEventResult>& Queue);
	static TArray<FWSEventResult> DeserializeQueue(const TArray<FString>& Serialized);

private:
	void SendRequest(const FString& Verb, const FString& Path,
		const FString& ContentType, const FString& Content,
		FWSHttpCallback Callback, bool bAuthorized, int32 AttemptsLeft);
	void ScheduleRetry(TFunction<void()> Retry, int32 AttemptsLeft);
	void SaveQueueToDisk() const;
	void LoadQueueFromDisk();
	void SubmitQueueHead();
	void FinishQueueHead(EWSSubmitOutcome Outcome, const FWSResultResponse& Response, const FString& Error);

	FString AccessToken;
	FWSUserDto SignedInUser;
	TArray<FWSEventResult> OfflineQueue;
	TArray<FWSSubmitCallback> QueueCallbacks; // parallel to OfflineQueue
	bool bQueueFlushInFlight = false;
	TArray<FTSTicker::FDelegateHandle> PendingTimers;
};
