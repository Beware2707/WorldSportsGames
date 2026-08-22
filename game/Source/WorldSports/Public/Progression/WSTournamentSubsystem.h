#pragma once

#include "CoreMinimal.h"
#include "Online/WSDtos.h"
#include "Sports/Results/WSEventResult.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WSTournamentSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWSOnTournamentChanged);

using FWSTournamentCallback = TFunction<void(bool bOk, const FString& Error)>;
using FWSRoundCallback =
	TFunction<void(bool bOk, const FWSTournamentResultDto&, const FString& Error)>;

/**
 * The tournament bracket, mirrored from the server.
 *
 * The client owns none of it. The server generates each round's field
 * before the round is run, decides the player's position against that
 * stored field, and decides whether they advanced — so a client cannot
 * invent an easier heat, re-roll a bad draw, or promote itself to a final.
 * Everything here is a read of that state plus the two writes the server
 * validates: enter, and submit a round's run.
 */
UCLASS()
class WORLDSPORTS_API UWSTournamentSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Enter a tournament for an event, or adopt the one already running. */
	void EnterOrResume(const FString& EventCode, FWSTournamentCallback Callback = nullptr);

	/** Re-read the active tournament from the server. */
	void Refresh(FWSTournamentCallback Callback = nullptr);

	/** Submit the current round's run. The server scores it against the
	 * field it stored before the race. */
	void SubmitRound(const FWSEventResult& Result, FWSRoundCallback Callback);

	UFUNCTION(BlueprintPure, Category = "WorldSports|Tournament")
	bool HasActiveTournament() const { return Active.IsValid() && Active.IsRunning(); }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Tournament")
	const FWSTournamentDto& GetActive() const { return Active; }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Tournament")
	const FWSTournamentResultDto& GetLastRound() const { return LastRound; }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Tournament")
	FString GetStatusText() const { return StatusText; }

	/** The field the player is about to race, straight from the server. */
	const TArray<FWSTournamentRival>& GetCurrentField() const;

	UPROPERTY(BlueprintAssignable, Category = "WorldSports|Tournament")
	FWSOnTournamentChanged OnTournamentChanged;

private:
	UFUNCTION()
	void HandleAuthChanged(bool bSignedIn);

	class UWSOnlineSubsystem* Online() const;
	void ApplyTournamentJson(const TSharedPtr<class FJsonObject>& Json);
	/** Fire every queued refresh callback exactly once. */
	void FlushRefreshCallbacks(bool bOk, const FString& Error);

	FWSTournamentDto Active;
	FWSTournamentResultDto LastRound;
	FString StatusText;
	bool bRequestInFlight = false;
	/** A refresh was asked for while one was already in flight. The answer
	 * already on the wire may predate what the caller needs to see, so
	 * another read is issued as soon as it lands. */
	bool bRefreshAgain = false;
	TArray<FWSTournamentCallback> PendingRefreshCallbacks;
};
