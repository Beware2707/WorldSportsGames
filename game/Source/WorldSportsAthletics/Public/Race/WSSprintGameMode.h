#pragma once

#include "CoreMinimal.h"
#include "Framework/WSEventGameMode.h"
#include "Online/WSOnlineSubsystem.h"
#include "Race/WSSprintAudio.h"
#include "Simulation/WSSprintEvents.h"
#include "Simulation/WSSprintSimulation.h"

#include "WSSprintGameMode.generated.h"

class AWSSprintRunner;
class AWSSprintTrack;
class UWSSprintHud;

/** Which screen the app is showing. The race lifecycle phases live on the
 * game state; this is the layer above them. */
UENUM(BlueprintType)
enum class EWSAppState : uint8
{
	Menu,
	SignIn,
	Racing,
	Leaderboard,
	Settings,
	Career,        // athlete, attributes, stage, records
	CreateAthlete, // first-run career creation
	Training,      // a drill in progress
	Tournament     // bracket: enter, see the draw, race the next round
};

/** One row of the server's leaderboard. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSLeaderboardRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Race") int32 Rank = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Race") FString AthleteName;
	UPROPERTY(BlueprintReadOnly, Category = "Race") FString Country;
	UPROPERTY(BlueprintReadOnly, Category = "Race") FString ValueText;
};

/** One finisher, for the result screen. */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSRaceStanding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Race") int32 Position = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Race") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Race") double TimeSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Race") double ReactionMs = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Race") bool bIsPlayer = false;
	UPROPERTY(BlueprintReadOnly, Category = "Race") bool bFalseStart = false;
};

/**
 * A timed sprint race — 100m, 200m or 400m, chosen from the event table.
 * Sprint-specific ONLY in its input handling and camera choreography;
 * everything else runs on the sport-agnostic phase machine in
 * AWSEventGameMode, which is what lets each event be data.
 *
 * Race clock: negative before the gun (Set phase), 0.0 at the gun. Every
 * runner — player and AI alike — advances through its own simulation
 * against that single clock.
 */
UCLASS()
class WORLDSPORTSATHLETICS_API AWSSprintGameMode : public AWSEventGameMode
{
	GENERATED_BODY()

public:
	AWSSprintGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// -- Player input (routed from the controller) -----------------------
	//
	// The controller reports raw press/release; the race rules live here.
	// A press before the athlete has left the blocks is a hold (whenever it
	// arrives — a player who first touches the screen AFTER the gun must
	// still be able to start), and any later press is a rhythm tap.
	void PlayerPress();
	void PlayerRelease();
	void PlayerLean();

	/** The event this race runs. Data, from the WSSprintEvents table. */
	const FWSSprintEventSpec& CurrentEvent() const;

	/** Choose the event to race (menu / career selection). */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void SelectEvent(const FString& EventCode);

	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetSelectedEventName() const;

	/** Step through the event table (wraps). Delta is +1 / -1. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void CycleEvent(int32 Delta);

	/** Start (or restart) a race. Safe to call from the result screen. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void StartRace();

	// -- Screen flow -----------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Race")
	EWSAppState GetAppState() const { return AppState; }

	UFUNCTION(BlueprintCallable, Category = "Race")
	void ShowScreen(EWSAppState NewState);

	/** Menu → Quick Play: clears the field and starts a fresh race. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void StartQuickPlay();

	/** Leave the race and return to the menu (mid-race = abandoned run). */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void ReturnToMenu();

	UFUNCTION(BlueprintCallable, Category = "Race")
	void SetPaused(bool bPause);

	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsPaused() const { return bPaused; }

	/** Replay the last seconds of the finish, from the recorded positions. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void PlayFinishReplay();

	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsReplaying() const { return ReplayCursor != INDEX_NONE; }

	// -- Leaderboard -----------------------------------------------------

	/** Fetch the global board for the SELECTED event. Anonymous is fine —
	 * the server allows the global scope without a token. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void RefreshLeaderboard();

	UFUNCTION(BlueprintPure, Category = "Race")
	const TArray<FWSLeaderboardRow>& GetLeaderboard() const { return LeaderboardRows; }

	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetLeaderboardStatus() const { return LeaderboardStatus; }

	// -- Account ---------------------------------------------------------

	/** Sign in (or register when bRegister) with credentials the PLAYER
	 * typed on their own device. Nothing is stored beyond the session. */
	void SubmitCredentials(const FString& Email, const FString& Password,
		const FString& DisplayName, bool bRegister);

	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetAccountStatus() const { return AccountStatus; }

	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsSignedIn() const;

	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetSignedInName() const;

	UFUNCTION(BlueprintCallable, Category = "Race")
	void SignOut();

	// -- Career ----------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Race")
	void RefreshCareer();

	/** Create the career athlete from the creation screen. */
	void CreateAthlete(const FString& Name, const FString& Gender);

	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetCareerStatus() const { return CareerStatus; }

	UFUNCTION(BlueprintPure, Category = "Race")
	bool HasCareerAthlete() const;

	/** Athlete summary lines for the career screen. */
	FString GetCareerSummary() const;

	/** Records and statistics, fetched together with the career screen. */
	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetCareerRecordsText() const { return RecordsText; }

	// -- Tournament ------------------------------------------------------

	/** Enter (or resume) a tournament in the selected event, and show the
	 * bracket. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void EnterTournament();

	/** Race the current round. The field is the server's, not the client's. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void RaceTournamentRound();

	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsTournamentRace() const { return bTournamentRace; }

	/** Bracket as text: each round, its draw, and how it went. */
	FString GetTournamentSummary() const;

	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetTournamentStatus() const { return TournamentStatus; }

	// -- Training --------------------------------------------------------

	/** Begin the reaction drill: hold, wait for the tone, release. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void StartReactionDrill();

	void DrillPress();
	void DrillRelease();

	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetDrillPrompt() const;

	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetDrillResult() const { return DrillResultText; }

	/** Test hook: deliver a submit answer belonging to the PREVIOUS race
	 * through the real, generation-guarded handler. */
	void DebugDeliverStaleSubmit();

	// -- Console commands ------------------------------------------------
	//
	// On-device verification without depending on touch coordinates:
	//   adb shell "am broadcast -a android.intent.action.RUN -e cmd WSQuickPlay"
	// They drive the same entry points the buttons do, so exercising them
	// proves the real path, not a parallel one.

	UFUNCTION(Exec) void WSQuickPlay() { StartQuickPlay(); }
	UFUNCTION(Exec) void WSMenu() { ReturnToMenu(); }
	UFUNCTION(Exec) void WSLeaderboard() { ShowScreen(EWSAppState::Leaderboard); }
	UFUNCTION(Exec) void WSSettings() { ShowScreen(EWSAppState::Settings); }
	UFUNCTION(Exec) void WSAccount() { ShowScreen(EWSAppState::SignIn); }
	UFUNCTION(Exec) void WSCareer() { ShowScreen(EWSAppState::Career); }
	UFUNCTION(Exec) void WSTraining() { ShowScreen(EWSAppState::Training); }
	UFUNCTION(Exec) void WSTournament() { ShowScreen(EWSAppState::Tournament); }
	UFUNCTION(Exec) void WSEnterTournament() { EnterTournament(); }
	UFUNCTION(Exec) void WSRaceRound() { RaceTournamentRound(); }
	UFUNCTION(Exec) void WSNextEvent() { CycleEvent(1); }
	UFUNCTION(Exec) void WSSelectEvent(const FString& EventCode) { SelectEvent(EventCode); }
	/** Reports the live race/app state into the log, for adb logcat. */
	UFUNCTION(Exec) void WSStatus();

	// -- HUD queries -----------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "Race")
	double GetRaceClock() const { return RaceClock; }

	UFUNCTION(BlueprintPure, Category = "Race")
	AWSSprintRunner* GetPlayerRunner() const { return PlayerRunner; }

	/** The track this race is run on. */
	AWSSprintTrack* GetTrack() const { return Track; }

	/** The whole field, player included. */
	const TArray<TObjectPtr<AWSSprintRunner>>& GetRunners() const { return Runners; }

	UFUNCTION(BlueprintPure, Category = "Race")
	int32 GetPlayerPosition() const;

	UFUNCTION(BlueprintPure, Category = "Race")
	const TArray<FWSRaceStanding>& GetStandings() const { return Standings; }

	/** Server's answer, once it arrives. Empty until then. */
	UFUNCTION(BlueprintPure, Category = "Race")
	FString GetServerVerdict() const { return ServerVerdict; }

	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsAwaitingServer() const { return bAwaitingServer; }

protected:
	virtual bool CanLeavePhase(EWSEventPhase Phase) const override;
	virtual void OnPhaseEntered(EWSEventPhase NewPhase) override;

private:
	void SpawnField();
	void UpdateCamera(float DeltaSeconds);
	/** Keep the last few seconds of every runner's position so the finish
	 * can be replayed. Cosmetic only — never fed back into a simulation. */
	void RecordReplayFrame();
	void TickReplay(float DeltaSeconds);
	void InitAudio();
	void TickAudio(float DeltaSeconds);
	void BuildStandings();
	void SubmitPlayerResult();
	/** The one place a submit answer is applied, guarded by Generation. */
	void HandleSubmitOutcome(uint32 Generation, EWSSubmitOutcome Outcome,
		const FWSResultResponse& Response, const FString& Error);
	FWSSprintAttributes ResolvePlayerAttributes() const;
	class UWSProgressionSubsystem* Progression() const;
	class UWSTournamentSubsystem* Tournaments() const;
	void SubmitTournamentRound();

	UPROPERTY()
	TObjectPtr<AWSSprintTrack> Track;

	UPROPERTY()
	TArray<TObjectPtr<AWSSprintRunner>> Runners;

	UPROPERTY()
	TObjectPtr<AWSSprintRunner> PlayerRunner;

	UPROPERTY()
	TObjectPtr<AActor> RaceCamera;

	UPROPERTY()
	TObjectPtr<UWSSprintHud> Hud;

	// Not "HudClass": AGameModeBase already has one (the AHUD class).
	UPROPERTY()
	TSubclassOf<UWSSprintHud> SprintHudClass;

	TArray<FWSSprintInputEvent> PlayerTrace; // submitted digest source
	TArray<FWSRaceStanding> Standings;

	/** One recorded instant of the finish, for the replay. */
	struct FReplayFrame
	{
		double Clock = 0.0;
		TArray<FVector> Positions;
	};
	TArray<FReplayFrame> ReplayFrames;
	int32 ReplayCursor = INDEX_NONE;
	float ReplayTime = 0.0f;

	// Generated cues; see WSSprintAudio.h for why they are synthesised and
	// why each playback needs a freshly queued wave.
	FWSSoundCue GunSound;
	FWSSoundCue MarksSound;
	FWSSoundCue SetSound;
	FWSSoundCue FootfallSound;
	FWSSoundCue FinishSound;
	/** Seconds before the gun that the "set" call lands, drawn per race. */
	double SetCallOffsetSeconds = 1.35;
	bool bPlayedMarks = false;
	bool bPlayedSet = false;
	bool bPlayedGun = false;
	bool bPlayedFinish = false;
	double NextFootfallDistance = 0.0;

	EWSAppState AppState = EWSAppState::Menu;
	bool bPaused = false;
	TArray<FWSLeaderboardRow> LeaderboardRows;
	bool bLeaderboardInFlight = false;
	FString CareerStatus;
	FString RecordsText;
	FString TournamentStatus;
	/** Empty means the default event (the 100m). */
	FString SelectedEventCode;
	/** True while the current race belongs to a tournament round: its result
	 * goes to the bracket endpoint, which scores it against the field the
	 * server stored BEFORE the race. */
	bool bTournamentRace = false;
	/** The rivals the server drew for the current round. */
	TArray<FWSTournamentRival> TournamentField;

	// -- Reaction drill state (the first training mini-game) -------------
	// It reuses the race's start mechanic deliberately: the skill being
	// trained is the same skill the 100m measures.
	bool bDrillArmed = false;      // holding, waiting for the tone
	bool bDrillToneFired = false;
	double DrillClock = 0.0;       // negative until the tone
	double DrillWaitSeconds = 0.0; // seeded, so the tone cannot be predicted
	FString DrillResultText;
	FString LeaderboardStatus;
	FString AccountStatus;

	double RaceClock = 0.0;
	double SetDurationSeconds = 3.0;
	uint32 RaceSeed = 0;
	/** Incremented by every StartRace. An async submit callback compares it
	 * so a late answer can never drive a race it does not belong to. */
	uint32 RaceGeneration = 0;
	bool bRaceRunning = false;
	bool bHolding = false;
	bool bAwaitingServer = false;
	FString ServerVerdict;
	float ResultDwell = 0.0f;
};
