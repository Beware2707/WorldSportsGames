#pragma once

#include "CoreMinimal.h"
#include "Framework/WSEventGameMode.h"
#include "Online/WSOnlineSubsystem.h"
#include "Race/WSSprintAudio.h"
#include "Simulation/WSJumpSimulation.h"
#include "Simulation/WSThrowSimulation.h"
#include "Simulation/WSPaceSimulation.h"
#include "Simulation/WSRelaySimulation.h"
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

/**
 * One attempt in a field event's series.
 *
 * Jumps and throws differ in everything except how they are SCORED: a mark
 * in metres, or a foul with a reason. Keeping one shape for the series
 * means the scoreboard, the best mark and the submission are written once
 * rather than once per kind.
 */
USTRUCT(BlueprintType)
struct WORLDSPORTSATHLETICS_API FWSFieldAttempt
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Race") double Metres = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Race") bool bFoul = false;
	/** Why it was a foul, in the sport's own words. Empty for a mark. */
	UPROPERTY(BlueprintReadOnly, Category = "Race") FString FoulReason;
	/** Wind that stood for this attempt; 0 for events that record none. */
	UPROPERTY(BlueprintReadOnly, Category = "Race") double Wind = 0.0;
	/** Vertical events: the bar this attempt was against, and whether it
	 * survived. A failure is not a foul — the athlete tries again. */
	UPROPERTY(BlueprintReadOnly, Category = "Race") double BarMetres = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Race") bool bVertical = false;
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

	/** Set and record the effort the player is asking for. */
	void PushPlayerEffort(double Effort);

	/** Effort the player is holding, 0..1, for the HUD's pace control. */
	UFUNCTION(BlueprintPure, Category = "Race")
	float GetPlayerEffort() const { return static_cast<float>(PlayerEffort); }

	/** The event this race runs, when it is a sprint. Data, from the
	 * WSSprintEvents table. Meaningless if IsPaceEvent(). */
	const FWSSprintEventSpec& CurrentEvent() const;

	/** The event this race runs, when it is middle distance. */
	const FWSPaceEventSpec& CurrentPaceEvent() const;

	/** The selected event as a RELAY row. Meaningless unless
	 * IsRelayEvent(). */
	const FWSRelayEventSpec& CurrentRelayEvent() const;

	/** True when the selected event is paced rather than sprinted: no
	 * blocks, no cadence band, effort and a kick instead. */
	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsPaceEvent() const;

	/** True when the selected event is one of the relays: four legs, three
	 * baton exchanges, and one clock over all of it. */
	bool IsRelayEvent() const;

	/** True for a FIELD event: a mark in metres from a series of attempts,
	 * instead of a race against a field. */
	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsJumpEvent() const;

	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsThrowEvent() const;

	/** Either kind of field event: both are a series, not a race. */
	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsFieldEvent() const { return IsJumpEvent() || IsThrowEvent(); }

	const FWSJumpEventSpec& CurrentJumpEvent() const;
	const FWSThrowEventSpec& CurrentThrowEvent() const;

	/** Wind-up progress and power, 0..1, for a throw's HUD. */
	UFUNCTION(BlueprintPure, Category = "Race")
	float GetThrowPower() const;

	UFUNCTION(BlueprintPure, Category = "Race")
	float GetThrowWindUp() const;

	/** Let the throw go. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void PlayerThrowRelease();

	// -- The jump, as the HUD sees it ------------------------------------

	/** The attempt being taken, 1-based, or 0 outside a jumping event.
	 * Clamped to the series length: once the last one is done there is no
	 * fourth attempt to announce. */
	UFUNCTION(BlueprintPure, Category = "Race")
	int32 GetJumpAttempt() const;

	/** How many attempts have actually been taken. */
	UFUNCTION(BlueprintPure, Category = "Race")
	int32 GetAttemptsTaken() const { return Attempts.Num(); }

	/** Metres to the board; negative once past it. */
	UFUNCTION(BlueprintPure, Category = "Race")
	float GetMetresToBoard() const;

	/** The best legal mark so far, in metres; 0 if there is none yet. */
	UFUNCTION(BlueprintPure, Category = "Race")
	float GetBestMark() const { return static_cast<float>(BestMark); }

	/** True for a vertical field event: a bar, a ladder, and elimination
	 * by failures rather than a fixed number of attempts. */
	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsVerticalEvent() const;

	/** The bar the athlete is facing, in metres. */
	UFUNCTION(BlueprintPure, Category = "Race")
	float GetCurrentBar() const { return static_cast<float>(CurrentBar); }

	/** Failures at THIS height so far. Three ends the competition. */
	UFUNCTION(BlueprintPure, Category = "Race")
	int32 GetFailuresAtHeight() const { return FailuresAtHeight; }

	/** How many failures at one height end the day. Three, in the sport. */
	int32 GetFailuresAllowed() const;

	/**
	 * True when this field event's result carries a wind reading.
	 *
	 * World Athletics records wind for the HORIZONTAL jumps only. A throw
	 * has none, and neither has a high jump or a pole vault — so neither
	 * may report one. Named rather than inlined because the live test and
	 * the game had drifted apart on exactly this.
	 */
	bool FieldResultHasWind() const;

	/** How many takeoffs this jump is made of: one, or three for a triple
	 * jump. */
	int32 GetJumpPhaseCount() const;

	/** Which of them is in the air right now, 1-based; 0 on the runway. */
	int32 GetJumpPhase() const;

	/** True while the takeoff into the next phase can be timed — the only
	 * moment the button does anything mid-jump. */
	bool IsJumpPhaseWindowOpen() const;

	/** Seconds until the phase in the air lands; negative once it has. */
	double GetJumpPhaseTimeRemaining() const;

	/** True while an attempt is actually being made. Between attempts the
	 * athlete is walking back, and every live readout — the board, the
	 * phase, the power — is describing an attempt that is already over. */
	bool IsFieldAttemptLive() const;

	/** What each attempt scored, in order. A foul reads as a foul rather
	 * than as a zero, because a foul is not a short jump. */
	FString GetAttemptSummary() const;

	/** Take off. The one decision the whole event turns on. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void PlayerTakeoff();

	/** Hand the baton over. Legal only inside the takeover zone; anywhere
	 * else disqualifies the team, exactly as it does on a track. */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void PlayerPass();

	/** Metres to the next handover line; 0 on the last leg. */
	float GetMetresToHandover() const;

	/** True while the baton can legally change hands. */
	bool IsInTakeoverZone() const;

	/** Which leg the team is on, 1-based; 0 outside a relay. */
	int32 GetRelayLeg() const;

	/** How long this relay's takeover zone is, in metres; 0 outside one. */
	float GetTakeoverZoneMetres() const;

	/** How many legs this relay has; 0 outside one. */
	int32 GetRelayLegCount() const;

	/** The selected event's code, distance and split count, whichever kind
	 * it is. Everything shared — the track, the camera, the leaderboard
	 * query, the submitted event code — reads these rather than reaching
	 * into one table and getting the wrong answer for the other kind. */
	FString SelectedEventCodeOrDefault() const;

	/** Every running event, sprints first — what the menu cycles through. */
	static TArray<FString> AllEventCodes();
	double SelectedDistanceMetres() const;
	int32 SelectedSplitCount() const;

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
	/** The player's middle-distance inputs, for the submitted digest. */
	TArray<FWSPaceInputEvent> PlayerPaceTrace;

	/** The relay's own input trace: taps, the gun, and three handovers. */
	TArray<FWSRelayInputEvent> PlayerRelayTrace;

public:
	/** The trace as submitted, for tests. It is hashed into InputDigest, so
	 * its times must be one clock's and in order or it cannot be replayed. */
	const TArray<FWSRelayInputEvent>& GetPlayerRelayTrace() const
	{
		return PlayerRelayTrace;
	}

private:
	/** Effort the player is currently asking for in a paced race. */
	double PlayerEffort = 0.0;

	// -- Field events -----------------------------------------------------
	TSharedPtr<FWSJumpSimulation> JumpSim;
	TArray<FWSJumpInputEvent> PlayerJumpTrace;
	TSharedPtr<FWSThrowSimulation> ThrowSim;
	TArray<FWSThrowInputEvent> PlayerThrowTrace;
	/** Marks so far. A foul is recorded as a foul, not as zero metres. */
	TArray<FWSFieldAttempt> Attempts;
	int32 AttemptIndex = 0;
	double BestMark = 0.0;
	/** Seconds left of the pause between attempts. */
	float AttemptRestSeconds = 0.0f;

	// -- The vertical ladder ---------------------------------------------
	/** A copy of the event, because the BAR changes as the day goes on and
	 * the table itself is shared, immutable data. */
	FWSJumpEventSpec ActiveJumpSpec;
	double CurrentBar = 0.0;
	int32 FailuresAtHeight = 0;

	void SubmitFieldResult();
	void StartFieldAttempt();
	void RecordFieldAttempt(const FWSFieldAttempt& Attempt);
	void TickJump(float DeltaSeconds);
	void TickThrow(float DeltaSeconds);
	/** How many attempts this field event allows. */
	int32 FieldAttemptCount() const;
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
