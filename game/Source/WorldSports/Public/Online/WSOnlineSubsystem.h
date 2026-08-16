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
	Queued,        // persisted locally; will be (re)sent when possible
	Failed         // definitively unprocessable; dropped with a loud log
};

/** What a POST /career/results HTTP status means for the queued entry. */
enum class EWSSubmitDisposition : uint8
{
	Definitive,    // 2xx: parse the outcome, dequeue
	AuthExpired,   // 401/403: auth rejected BEFORE processing; keep, halt, re-auth
	Retryable,     // 408/429/5xx: keep the entry, halt the flush, retry later
	NeedsAthlete,  // 404: no career athlete yet — create one and retry
	Fatal          // remaining 4xx: the submission itself can never succeed; drop
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

	/** Signed in AND the account is known. The token alone is not enough:
	 * between the token arriving and /auth/me answering, the offline queue
	 * has no owner, and a result finished in that window would be dropped
	 * rather than queued. */
	UFUNCTION(BlueprintPure, Category = "WorldSports|Online")
	bool IsSignedIn() const { return !AccessToken.IsEmpty() && SignedInUser.id != 0; }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Online")
	const FWSUserDto& GetSignedInUser() const { return SignedInUser; }

	UPROPERTY(BlueprintAssignable, Category = "WorldSports|Online")
	FWSOnAuthChanged OnAuthChanged;

	// -- Results ------------------------------------------------------------

	/**
	 * Persist the result to the per-user offline queue FIRST, then try the
	 * network. The callback fires EXACTLY ONCE with the first known outcome:
	 * Accepted/Rejected/Failed if the server answers this attempt, otherwise
	 * Queued (offline, session expired, or waiting behind older entries) —
	 * never silence, so no caller can hang on it. Later server outcomes for
	 * queued entries arrive via OnResultSubmitted.
	 *
	 * Results without a ClientRef get one here; replays after a timeout or
	 * crash resend the same ref and the server answers idempotently.
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

	/** Pure classification of a result-POST status code, exposed for tests. */
	static EWSSubmitDisposition ClassifyResultStatus(int32 StatusCode);

private:
	void SendRequest(const FString& Verb, const FString& Path,
		const FString& ContentType, const FString& Content,
		FWSHttpCallback Callback, bool bAuthorized, int32 AttemptsLeft);
	void ScheduleRetry(TFunction<void()> Retry, int32 AttemptsLeft);
	void SaveQueueToDisk() const;
	void SwitchQueueUser(int32 UserId);
	void SubmitQueueHead();
	/** Create the signed-in user's career athlete, then resume the flush. */
	void CreateCareerAthleteAndResume();
	void FinishQueueHead(EWSSubmitOutcome Outcome, const FWSResultResponse& Response, const FString& Error);
	/** Flush cannot continue (offline / auth expired): every waiter that has
	 * not heard anything yet gets its one Queued notification. */
	void NotifyAllQueuedAndHalt(const FString& Reason);
	FString QueueSlotForUser(int32 UserId) const;

	FString AccessToken;
	FWSUserDto SignedInUser;
	/** Owner of the loaded offline queue; survives token expiry so queued
	 * results are never attributed to a later account. 0 = none loaded. */
	int32 QueueUserId = 0;
	TArray<FWSEventResult> OfflineQueue;
	TArray<FWSSubmitCallback> QueueCallbacks; // parallel to OfflineQueue
	bool bQueueFlushInFlight = false;
	/** Identifies the submission currently on the wire. A completion is
	 * applied only if the queue still belongs to the same account and the
	 * head is still the same result — otherwise it would dequeue whatever
	 * happens to be at index 0 now, which after an account switch is a
	 * different player's unsent race. */
	int32 InFlightUserId = 0;
	FString InFlightClientRef;
	/** Bumped by every sign-in and sign-out so a late auth answer for an
	 * abandoned attempt cannot resurrect a session. */
	uint32 AuthGeneration = 0;
	/** One athlete-creation attempt per session; a second 404 after a
	 * successful create means something else is wrong and must not loop. */
	bool bTriedCreatingAthlete = false;
	TArray<FTSTicker::FDelegateHandle> PendingTimers;
};
