#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/WSGameStateBase.h"
#include "Misc/AutomationTest.h"
#include "Race/WSSprintGameMode.h"
#include "Race/WSSprintRunner.h"
#include "Race/WSSprintTrack.h"
#include "Simulation/WSJumpSimulation.h"
#include "Simulation/WSPaceSimulation.h"
#include "Simulation/WSSprintEvents.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Race-flow tests in a REAL world: the game mode spawns the track and the
 * eight runners, ticks a full race, and produces standings. Unit-testing
 * the simulation proves the physics; only this proves the game.
 */

namespace
{
/** A world with the sprint game mode, ticked manually. */
struct FRaceHarness
{
	UWorld* World = nullptr;
	AWSSprintGameMode* GameMode = nullptr;

	explicit FRaceHarness(bool bStartRace = true)
	{
		PreviousWorld = GWorld;
		World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		GWorld = World;

		// The game mode and game state are spawned directly rather than
		// through world play-initialization: a headless automation world has
		// no game instance or local player, and the race must not depend on
		// either. Everything the race needs is driven from the game mode.
		GameState = World->SpawnActor<AWSGameStateBase>(AWSGameStateBase::StaticClass());
		World->SetGameState(GameState);
		GameMode = World->SpawnActor<AWSSprintGameMode>(AWSSprintGameMode::StaticClass());
		if (GameMode)
		{
			GameMode->DispatchBeginPlay();
			// The app opens on the menu now; most tests are about the race.
			if (bStartRace)
			{
				GameMode->StartQuickPlay();
			}
		}
	}

	~FRaceHarness()
	{
		GWorld = PreviousWorld;
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	UWorld* PreviousWorld = nullptr;
	AWSGameStateBase* GameState = nullptr;

	/** Tick the race at a fixed frame rate for up to MaxSeconds. */
	void RunFor(double MaxSeconds, double FrameDt = 1.0 / 60.0,
		TFunctionRef<void(double)> PerFrame = [](double) {})
	{
		const int32 MaxFrames = static_cast<int32>(MaxSeconds / FrameDt);
		for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
		{
			GameMode->Tick(static_cast<float>(FrameDt));
			PerFrame(GameMode->GetRaceClock());
		}
	}
};

/** Taps in time with the band the player's own runner reports. */
void PlayWell(AWSSprintGameMode* GameMode, double Clock, double& NextTapAt)
{
	AWSSprintRunner* Runner = GameMode->GetPlayerRunner();
	if (!Runner || !Runner->GetState().bReleased || Runner->HasFinished())
	{
		return;
	}
	if (Clock >= NextTapAt)
	{
		GameMode->PlayerPress();
		const double TargetHz =
			FMath::Max(Runner->TargetCadenceAt(Runner->GetState().Distance), 0.5);
		NextTapAt = Clock + 1.0 / TargetHz;
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRaceFullLoopTest,
	"WorldSports.Race.FullRaceProducesStandings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRaceFullLoopTest::RunTest(const FString&)
{
	FRaceHarness Harness;
	if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
	{
		return false;
	}
	TestNotNull(TEXT("player runner exists"), Harness.GameMode->GetPlayerRunner());
	TestEqual(TEXT("race starts in Ready"),
		static_cast<int32>(Harness.GameMode->GetPhase()),
		static_cast<int32>(EWSEventPhase::Ready));

	// Hold in the blocks, release ~170ms after the gun, then keep rhythm.
	bool bHeld = false;
	bool bReleased = false;
	double NextTapAt = 0.0;
	Harness.RunFor(30.0, 1.0 / 60.0, [&](double Clock)
	{
		if (!bHeld && Clock > -2.0)
		{
			Harness.GameMode->PlayerPress();
			bHeld = true;
		}
		if (!bReleased && Clock >= 0.17)
		{
			Harness.GameMode->PlayerRelease();
			bReleased = true;
			NextTapAt = Clock + 0.12;
		}
		if (bReleased)
		{
			PlayWell(Harness.GameMode, Clock, NextTapAt);
		}
	});

	AWSSprintRunner* Player = Harness.GameMode->GetPlayerRunner();
	TestTrue(TEXT("player finished"), Player->HasFinished());
	TestFalse(TEXT("no false start"), Player->HasFalseStarted());

	const FWSRaceOutcome Outcome = Player->GetOutcome();
	TestTrue(FString::Printf(TEXT("plausible time %.3f"), Outcome.TimeSeconds),
		Outcome.TimeSeconds > 9.5 && Outcome.TimeSeconds < 20.0);
	TestEqual(TEXT("ten splits"), Outcome.Splits.Num(), 10);
	TestTrue(TEXT("reaction is legal"), Outcome.ReactionMs >= 100.0);

	// Every lane raced, and the standings are a complete, ordered field.
	const TArray<FWSRaceStanding>& Standings = Harness.GameMode->GetStandings();
	TestEqual(TEXT("eight runners"), Standings.Num(), AWSSprintTrack::LaneCount);
	int32 Finishers = 0;
	double PreviousTime = 0.0;
	for (const FWSRaceStanding& Standing : Standings)
	{
		if (Standing.bFalseStart)
		{
			continue;
		}
		++Finishers;
		TestTrue(TEXT("standings are ordered by time"),
			Standing.TimeSeconds >= PreviousTime - KINDA_SMALL_NUMBER);
		PreviousTime = Standing.TimeSeconds;
	}
	TestEqual(TEXT("all eight finished"), Finishers, AWSSprintTrack::LaneCount);
	TestTrue(TEXT("player has a position"),
		Harness.GameMode->GetPlayerPosition() >= 1 &&
		Harness.GameMode->GetPlayerPosition() <= AWSSprintTrack::LaneCount);

	// The race reached the result phase, where submission happens.
	TestTrue(TEXT("reached Result or later"),
		static_cast<int32>(Harness.GameMode->GetPhase()) >=
		static_cast<int32>(EWSEventPhase::Result));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRaceEventSelectionTest,
	"WorldSports.Race.EveryEventRacesEndToEnd",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRaceEventSelectionTest::RunTest(const FString&)
{
	// The Phase 5 checkpoint, as a test: selecting the 200m or the 400m must
	// produce a real race of THAT distance through the same game mode — no
	// new code path, and no 100m leaking through in the distance, the split
	// count, or the event code the result is filed under.
	for (const FWSSprintEventSpec& Spec : WSSprintEvents::All())
	{
		FRaceHarness Harness(/*bStartRace=*/false);
		if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
		{
			return false;
		}
		Harness.GameMode->SelectEvent(Spec.Code);
		TestEqual(TEXT("event selected"), Harness.GameMode->CurrentEvent().Code, Spec.Code);
		Harness.GameMode->StartQuickPlay();

		bool bHeld = false;
		bool bReleased = false;
		double NextTapAt = 0.0;
		// Long enough for a slow 400m, driven off the event's own distance
		// rather than a constant that would quietly truncate the lap.
		const double Budget = 30.0 + 0.9 * Spec.DistanceMetres;
		Harness.RunFor(Budget, 1.0 / 60.0, [&](double Clock)
		{
			if (!bHeld && Clock > -2.0)
			{
				Harness.GameMode->PlayerPress();
				bHeld = true;
			}
			if (!bReleased && Clock >= 0.17)
			{
				Harness.GameMode->PlayerRelease();
				bReleased = true;
				NextTapAt = Clock + 0.12;
			}
			if (bReleased)
			{
				PlayWell(Harness.GameMode, Clock, NextTapAt);
			}
		});

		AWSSprintRunner* Player = Harness.GameMode->GetPlayerRunner();
		if (!TestNotNull(TEXT("player runner exists"), Player))
		{
			return false;
		}
		TestTrue(FString::Printf(TEXT("%s: player finished"), *Spec.Code),
			Player->HasFinished());

		const FWSRaceOutcome Outcome = Player->GetOutcome();
		TestEqual(FString::Printf(TEXT("%s: split count"), *Spec.Code),
			Outcome.Splits.Num(), Spec.SplitCount);
		TestTrue(FString::Printf(
				TEXT("%s: time %.3f inside the server's plausible band"),
				*Spec.Code, Outcome.TimeSeconds),
			Outcome.TimeSeconds > Spec.MinPlausibleSeconds &&
				Outcome.TimeSeconds < Spec.MaxPlausibleSeconds);

		// The world the race is run in must match the race. On the first
		// emulator run a 400m runner went off the end of a 100m track into
		// black nothing, and the HUD labelled 50m splits as 10m/20m/30m —
		// neither of which any offline test was watching for.
		AWSSprintTrack* TrackActor = Harness.GameMode->GetTrack();
		if (TestNotNull(TEXT("track spawned"), TrackActor))
		{
			TestEqual(FString::Printf(TEXT("%s: finish line is at the event distance"),
					*Spec.Code),
				TrackActor->GetFinishLineX(),
				static_cast<float>(Spec.DistanceMetres) * 100.0f);

			// One marker per split boundary, the finish excluded.
			const TArray<float> Marks = TrackActor->GetVisibleMarkPositions();
			TestEqual(FString::Printf(TEXT("%s: marker count"), *Spec.Code),
				Marks.Num(), Spec.SplitCount - 1);
			const float Segment =
				static_cast<float>(Spec.DistanceMetres) * 100.0f / Spec.SplitCount;
			for (int32 Index = 0; Index < Marks.Num(); ++Index)
			{
				TestEqual(FString::Printf(TEXT("%s: marker %d on its split"),
						*Spec.Code, Index),
					Marks[Index], Segment * (Index + 1));
			}
		}

		TestEqual(FString::Printf(TEXT("%s: split segment reported to the HUD"),
				*Spec.Code),
			Player->GetSplitSegmentMetres(), Spec.DistanceMetres / Spec.SplitCount);

		// Barriers the simulation judges must be barriers the player can
		// SEE. The first hurdles race on device had ten of them in the
		// simulation and none on the track, leaving a numeric prompt as the
		// only cue that anything was in the way.
		if (TrackActor)
		{
			TestEqual(FString::Printf(TEXT("%s: standing barriers"), *Spec.Code),
				TrackActor->GetVisibleHurdleCount(), Spec.HurdleCount);
			const TArray<float> Barriers = TrackActor->GetVisibleHurdlePositions();
			for (int32 Index = 0; Index < Barriers.Num(); ++Index)
			{
				// A centimetre of tolerance: the track places barriers in
				// float and the simulation judges them in double, and the
				// difference is fractions of a micron. Demanding exact
				// equality tests the arithmetic, not the game.
				TestEqual(FString::Printf(TEXT("%s: barrier %d stands where the ")
						TEXT("simulation judges it"), *Spec.Code, Index),
					Barriers[Index],
					static_cast<float>(Spec.HurdleMetres(Index)) * 100.0f,
					/*Tolerance=*/1.0f);
			}
		}

		// The whole field ran the same distance — including the AI, which
		// runs this simulation rather than being handed a finish time.
		TestEqual(FString::Printf(TEXT("%s: full field"), *Spec.Code),
			Harness.GameMode->GetStandings().Num(), AWSSprintTrack::LaneCount);
		for (const AWSSprintRunner* Runner : Harness.GameMode->GetRunners())
		{
			TestEqual(FString::Printf(TEXT("%s: every runner ran the distance"), *Spec.Code),
				Runner->GetRaceDistance(), Spec.DistanceMetres);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSPaceRaceFlowTest,
	"WorldSports.Race.MiddleDistanceRacesEndToEnd",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSPaceRaceFlowTest::RunTest(const FString&)
{
	// The 800m and 1500m through the SAME game mode: no blocks to leave, no
	// rhythm to match, and a finish that has to arrive on its own. This is
	// the check that the events are playable and not merely simulated.
	for (const FWSPaceEventSpec& Spec : WSPaceEvents::All())
	{
		FRaceHarness Harness(/*bStartRace=*/false);
		if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
		{
			return false;
		}
		Harness.GameMode->SelectEvent(Spec.Code);
		TestTrue(FString::Printf(TEXT("%s is a paced event"), *Spec.Code),
			Harness.GameMode->IsPaceEvent());
		Harness.GameMode->StartQuickPlay();

		// Hold a steady effort, then kick once the closing stretch arrives.
		bool bKicked = false;
		const double Budget = 40.0 + 1.4 * Spec.CeilingAtZero;
		Harness.RunFor(Budget, 1.0 / 60.0, [&](double Clock)
		{
			AWSSprintRunner* Runner = Harness.GameMode->GetPlayerRunner();
			if (!Runner || Clock < 0.0)
			{
				return;
			}
			const FWSPaceState& Pace = Runner->GetPaceState();
			// Nudge effort toward what this athlete can sustain — the
			// judgement the event is about, played competently.
			if (Pace.Effort < Pace.SustainableEffort - 0.02)
			{
				Harness.GameMode->PlayerPress();
			}
			else if (Pace.Effort > Pace.SustainableEffort + 0.02)
			{
				Harness.GameMode->PlayerRelease();
			}
			if (!bKicked &&
				Pace.Distance >= Spec.DistanceMetres * Spec.KickWindowFraction)
			{
				Harness.GameMode->PlayerLean(); // the kick
				bKicked = true;
			}
		});

		AWSSprintRunner* Player = Harness.GameMode->GetPlayerRunner();
		if (!TestNotNull(TEXT("player runner exists"), Player))
		{
			return false;
		}
		TestTrue(FString::Printf(TEXT("%s: paced runner"), *Spec.Code),
			Player->IsPaceEvent());
		TestTrue(FString::Printf(TEXT("%s: player finished"), *Spec.Code),
			Player->HasFinished());
		TestFalse(FString::Printf(TEXT("%s: no false start without blocks"), *Spec.Code),
			Player->HasFalseStarted());

		const FWSRaceOutcome Outcome = Player->GetOutcome();
		TestEqual(FString::Printf(TEXT("%s: split count"), *Spec.Code),
			Outcome.Splits.Num(), Spec.SplitCount);
		// No blocks and no measured wind: both must be absent rather than
		// invented, because the server's row says this event has neither.
		TestEqual(FString::Printf(TEXT("%s: no reaction reported"), *Spec.Code),
			Outcome.ReactionMs, 0.0);
		TestEqual(FString::Printf(TEXT("%s: no wind reported"), *Spec.Code),
			Outcome.Wind, 0.0);
		TestTrue(FString::Printf(TEXT("%s: time %.2f is plausible"),
				*Spec.Code, Outcome.TimeSeconds),
			Outcome.TimeSeconds > Spec.MinPlausibleSeconds &&
				Outcome.TimeSeconds < Spec.MaxPlausibleSeconds);

		// The whole field ran it, and every rival ran the same distance
		// through the same simulation rather than to a handed-down time.
		TestEqual(FString::Printf(TEXT("%s: full field"), *Spec.Code),
			Harness.GameMode->GetStandings().Num(), AWSSprintTrack::LaneCount);
		for (const AWSSprintRunner* Runner : Harness.GameMode->GetRunners())
		{
			TestTrue(FString::Printf(TEXT("%s: rival is simulated, not scripted"),
					*Spec.Code),
				Runner->IsPaceEvent() && !Runner->IsScripted());
			TestEqual(FString::Printf(TEXT("%s: same distance for all"), *Spec.Code),
				Runner->GetRaceDistance(), Spec.DistanceMetres);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSJumpFlowTest,
	"WorldSports.Race.LongJumpPlaysAsASeries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSJumpFlowTest::RunTest(const FString&)
{
	// A field event is not a race, and this is the check that the app knows
	// it: one athlete on a runway, three attempts, a mark in metres, and a
	// board that ends an attempt whether it is hit or overrun.
	for (const FWSJumpEventSpec& Spec : WSJumpEvents::All())
	{
		FRaceHarness Harness(/*bStartRace=*/false);
		if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
		{
			return false;
		}
		Harness.GameMode->SelectEvent(Spec.Code);
		TestTrue(FString::Printf(TEXT("%s is a field event"), *Spec.Code),
			Harness.GameMode->IsJumpEvent());
		Harness.GameMode->StartQuickPlay();

		// The runway is dressed: a board to aim at and a pit to land in.
		AWSSprintTrack* TrackActor = Harness.GameMode->GetTrack();
		if (TestNotNull(TEXT("track spawned"), TrackActor))
		{
			TestEqual(FString::Printf(TEXT("%s: the board is at the runway's end"),
					*Spec.Code),
				TrackActor->GetBoardX(),
				static_cast<float>(Spec.RunwayMetres) * 100.0f, /*Tolerance=*/1.0f);
			TestEqual(FString::Printf(TEXT("%s: no barriers on a runway"), *Spec.Code),
				TrackActor->GetVisibleHurdleCount(), 0);
		}

		// Run the whole series: tap the approach, take off near the board.
		double NextTapAt = 0.0;
		Harness.RunFor(60.0, 1.0 / 60.0, [&](double Clock)
		{
			if (Clock < 0.0)
			{
				return;
			}
			if (Clock >= NextTapAt)
			{
				Harness.GameMode->PlayerPress();
				NextTapAt = Clock + 1.0 / 4.6;
			}
			// Leave the ground while there is still board left.
			if (Harness.GameMode->GetMetresToBoard() <= 0.35f &&
				Harness.GameMode->GetMetresToBoard() > 0.0f)
			{
				Harness.GameMode->PlayerTakeoff();
			}
		});

		TestEqual(FString::Printf(TEXT("%s: the whole series was taken"), *Spec.Code),
			Harness.GameMode->GetAttemptsTaken(), Spec.Attempts);
		// And the HUD never announces an attempt that does not exist.
		TestEqual(FString::Printf(TEXT("%s: no fourth attempt announced"), *Spec.Code),
			Harness.GameMode->GetJumpAttempt(), Spec.Attempts);
		TestTrue(FString::Printf(TEXT("%s: a mark was set (best %.2f m)"),
				*Spec.Code, Harness.GameMode->GetBestMark()),
			Harness.GameMode->GetBestMark() >= Spec.MinPlausibleMetres);
		TestTrue(FString::Printf(TEXT("%s: the mark is inside the plausible band"),
				*Spec.Code),
			Harness.GameMode->GetBestMark() <= Spec.MaxPlausibleMetres);
		TestTrue(FString::Printf(TEXT("%s: reached the result"), *Spec.Code),
			static_cast<int32>(Harness.GameMode->GetPhase()) >=
				static_cast<int32>(EWSEventPhase::Result));

		AddInfo(FString::Printf(TEXT("SERIES %s best %.2f m over %d attempts"),
			*Spec.Code, Harness.GameMode->GetBestMark(),
			Harness.GameMode->GetAttemptsTaken()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRaceFalseStartTest,
	"WorldSports.Race.FalseStartEndsTheRaceHonestly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRaceFalseStartTest::RunTest(const FString&)
{
	FRaceHarness Harness;
	if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
	{
		return false;
	}

	// Jump the gun: hold, then release while the clock is still negative.
	bool bHeld = false;
	bool bJumped = false;
	Harness.RunFor(12.0, 1.0 / 60.0, [&](double Clock)
	{
		if (!bHeld && Clock > -2.5)
		{
			Harness.GameMode->PlayerPress();
			bHeld = true;
		}
		if (!bJumped && Clock > -1.0)
		{
			Harness.GameMode->PlayerRelease();
			bJumped = true;
		}
	});

	AWSSprintRunner* Player = Harness.GameMode->GetPlayerRunner();
	TestTrue(TEXT("false start recorded"), Player->HasFalseStarted());
	TestFalse(TEXT("no finish"), Player->HasFinished());

	const FWSRaceOutcome Outcome = Player->GetOutcome();
	TestEqual(TEXT("no time is claimed"), Outcome.TimeSeconds, 0.0);
	TestEqual(TEXT("no splits are claimed"), Outcome.Splits.Num(), 0);

	// The standings must show the DQ rather than inventing a placing, and
	// the verdict must say so plainly instead of looking like a net error.
	bool bFoundDq = false;
	for (const FWSRaceStanding& Standing : Harness.GameMode->GetStandings())
	{
		if (Standing.bIsPlayer)
		{
			bFoundDq = Standing.bFalseStart;
			TestEqual(TEXT("no position for a DQ"), Standing.Position, 0);
		}
	}
	TestTrue(TEXT("player is listed as a false start"), bFoundDq);
	TestTrue(TEXT("verdict explains the DQ"),
		Harness.GameMode->GetServerVerdict().Contains(TEXT("False start")));

	// A false start recalls the race. Listing the seven athletes who are
	// still standing in their blocks as a finished field — with position 0
	// and a 0.00 time — would be a fabricated classification.
	TestEqual(TEXT("only the DQ is listed"),
		Harness.GameMode->GetStandings().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRaceLateFirstTouchTest,
	"WorldSports.Race.FirstTouchAfterTheGunStillRuns",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRaceLateFirstTouchTest::RunTest(const FString&)
{
	// A player whose FIRST touch lands after the gun must still be able to
	// leave the blocks. Gating the hold on the Ready phase stranded them:
	// the press became a tap, the release was ignored, and they sat in the
	// blocks until the auto-release rescued them at 1.5s.
	FRaceHarness Harness;
	if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
	{
		return false;
	}

	bool bPressed = false;
	bool bReleased = false;
	double NextTapAt = 0.0;
	Harness.RunFor(30.0, 1.0 / 60.0, [&](double Clock)
	{
		if (!bPressed && Clock >= 0.25) // first contact, AFTER the gun
		{
			Harness.GameMode->PlayerPress();
			bPressed = true;
		}
		if (bPressed && !bReleased && Clock >= 0.40)
		{
			Harness.GameMode->PlayerRelease();
			bReleased = true;
			NextTapAt = Clock + 0.12;
		}
		if (bReleased)
		{
			PlayWell(Harness.GameMode, Clock, NextTapAt);
		}
	});

	AWSSprintRunner* Player = Harness.GameMode->GetPlayerRunner();
	TestTrue(TEXT("late starter finished"), Player->HasFinished());
	const FWSRaceOutcome Outcome = Player->GetOutcome();
	// Their reaction is their own late release — not the 1500ms auto-release
	// that a stranded player would have been given.
	TestTrue(FString::Printf(TEXT("reaction %.0fms is the player's own release"),
			Outcome.ReactionMs),
		Outcome.ReactionMs > 350.0 && Outcome.ReactionMs < 600.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSAppShellTest,
	"WorldSports.Race.ShellOpensOnMenuAndReturnsToIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSAppShellTest::RunTest(const FString&)
{
	// The app must open on a menu — a player has to be able to sign in and
	// read a leaderboard without being dropped into a race first.
	FRaceHarness Harness(/*bStartRace=*/false);
	if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
	{
		return false;
	}
	TestEqual(TEXT("opens on the menu"),
		static_cast<int32>(Harness.GameMode->GetAppState()),
		static_cast<int32>(EWSAppState::Menu));
	TestFalse(TEXT("no race is running yet"),
		Harness.GameMode->GetPlayerRunner() != nullptr &&
		Harness.GameMode->GetPhase() == EWSEventPhase::Ready);

	Harness.GameMode->StartQuickPlay();
	TestEqual(TEXT("quick play races"),
		static_cast<int32>(Harness.GameMode->GetAppState()),
		static_cast<int32>(EWSAppState::Racing));
	Harness.RunFor(32.0);
	TestTrue(TEXT("race completed"), Harness.GameMode->GetPlayerRunner()->HasFinished());

	Harness.GameMode->ReturnToMenu();
	TestEqual(TEXT("back on the menu"),
		static_cast<int32>(Harness.GameMode->GetAppState()),
		static_cast<int32>(EWSAppState::Menu));

	// And the loop closes: menu → race → result → menu → race again.
	Harness.GameMode->StartQuickPlay();
	Harness.RunFor(32.0);
	TestTrue(TEXT("second race completed"),
		Harness.GameMode->GetPlayerRunner()->HasFinished());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRacePauseTest,
	"WorldSports.Race.PauseFreezesTheRaceWithoutAdvantage",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRacePauseTest::RunTest(const FString&)
{
	FRaceHarness Harness;
	if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
	{
		return false;
	}

	// Get into the race proper, then pause mid-run.
	bool bPressed = false;
	bool bReleased = false;
	double NextTapAt = 0.0;
	Harness.RunFor(8.0, 1.0 / 60.0, [&](double Clock)
	{
		if (!bPressed && Clock > -1.5) { Harness.GameMode->PlayerPress(); bPressed = true; }
		if (bPressed && !bReleased && Clock >= 0.18)
		{
			Harness.GameMode->PlayerRelease();
			bReleased = true;
			NextTapAt = Clock + 0.12;
		}
		if (bReleased) { PlayWell(Harness.GameMode, Clock, NextTapAt); }
	});

	AWSSprintRunner* Player = Harness.GameMode->GetPlayerRunner();
	TestTrue(TEXT("race is under way"), Player->GetState().Distance > 5.0);
	const double ClockAtPause = Harness.GameMode->GetRaceClock();
	const double DistanceAtPause = Player->GetState().Distance;

	Harness.GameMode->SetPaused(true);
	TestTrue(TEXT("paused"), Harness.GameMode->IsPaused());
	Harness.RunFor(3.0); // three seconds of real time pass

	// Nothing moved and no time was consumed: pause is not a rest, and it is
	// not a way to stop the clock while the athlete keeps running either.
	TestEqual(TEXT("race clock frozen"),
		Harness.GameMode->GetRaceClock(), ClockAtPause);
	TestEqual(TEXT("athlete frozen"),
		Player->GetState().Distance, DistanceAtPause);

	Harness.GameMode->SetPaused(false);
	Harness.RunFor(32.0, 1.0 / 60.0, [&](double Clock)
	{
		PlayWell(Harness.GameMode, Clock, NextTapAt);
	});
	TestTrue(TEXT("race resumes and finishes"), Player->HasFinished());
	TestTrue(TEXT("time is still plausible"),
		Player->GetOutcome().TimeSeconds > 9.5 &&
		Player->GetOutcome().TimeSeconds < 20.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRacePauseInputTest,
	"WorldSports.Race.PausedInputCannotDisqualify",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRacePauseInputTest::RunTest(const FString&)
{
	// Pausing during "Set" and then letting go of the screen — the natural
	// thing to do while paused — used to push a pre-gun Release into the
	// simulation and disqualify the player for pausing.
	FRaceHarness Harness;
	if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
	{
		return false;
	}

	bool bHeld = false;
	bool bPausedYet = false;
	bool bLetGo = false;
	double NextTapAt = 0.0;
	Harness.RunFor(35.0, 1.0 / 60.0, [&](double Clock)
	{
		if (!bHeld && Clock > -2.0)
		{
			Harness.GameMode->PlayerPress(); // settle into the blocks
			bHeld = true;
		}
		else if (bHeld && !bPausedYet)
		{
			Harness.GameMode->SetPaused(true);
			bPausedYet = true;
		}
		else if (bPausedYet && !bLetGo)
		{
			// Finger lifts while the race is frozen before the gun.
			Harness.GameMode->PlayerRelease();
			bLetGo = true;
			Harness.GameMode->SetPaused(false);
			NextTapAt = Clock;
		}
		else if (bLetGo)
		{
			AWSSprintRunner* Runner = Harness.GameMode->GetPlayerRunner();
			if (Runner && !Runner->GetState().bReleased && Clock >= 0.18)
			{
				Harness.GameMode->PlayerPress();
				Harness.GameMode->PlayerRelease();
				NextTapAt = Clock + 0.12;
			}
			PlayWell(Harness.GameMode, Clock, NextTapAt);
		}
	});

	AWSSprintRunner* Player = Harness.GameMode->GetPlayerRunner();
	TestFalse(TEXT("pausing did not disqualify the player"), Player->HasFalseStarted());
	TestTrue(TEXT("the race still finished"), Player->HasFinished());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRaceReplayTest,
	"WorldSports.Race.FinishReplayRewindsWithoutChangingTheResult",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRaceReplayTest::RunTest(const FString&)
{
	FRaceHarness Harness;
	if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
	{
		return false;
	}
	Harness.RunFor(35.0);

	AWSSprintRunner* Player = Harness.GameMode->GetPlayerRunner();
	TestTrue(TEXT("race finished"), Player->HasFinished());
	const FWSRaceOutcome Before = Player->GetOutcome();
	const FVector EndPosition = Player->GetActorLocation();

	Harness.GameMode->PlayFinishReplay();
	TestTrue(TEXT("replay started"), Harness.GameMode->IsReplaying());

	// Mid-replay the athletes are back down the track...
	Harness.RunFor(1.0);
	TestTrue(TEXT("replay rewound the athlete"),
		Player->GetActorLocation().X < EndPosition.X);

	Harness.RunFor(12.0);
	TestFalse(TEXT("replay ended"), Harness.GameMode->IsReplaying());

	// ...and the RESULT is untouched. A replay is presentation, never a
	// second chance to change what happened.
	const FWSRaceOutcome After = Player->GetOutcome();
	TestEqual(TEXT("time unchanged"), After.TimeSeconds, Before.TimeSeconds);
	TestEqual(TEXT("splits unchanged"), After.Splits.Num(), Before.Splits.Num());
	TestEqual(TEXT("standings unchanged"),
		Harness.GameMode->GetStandings().Num(), AWSSprintTrack::LaneCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRaceStaleSubmitTest,
	"WorldSports.Race.StaleSubmitCannotHijackTheNextRace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRaceStaleSubmitTest::RunTest(const FString&)
{
	// "Race again" while a submission is in flight: the late answer must not
	// stamp the previous run's verdict on the new race or shove it into the
	// Reward phase, which would leave it unplayable.
	FRaceHarness Harness;
	if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
	{
		return false;
	}
	Harness.RunFor(32.0);
	TestTrue(TEXT("first race reached a result"),
		static_cast<int32>(Harness.GameMode->GetPhase()) >=
		static_cast<int32>(EWSEventPhase::Result));

	Harness.GameMode->StartRace();
	// Deliver the PREVIOUS race's answer now.
	Harness.GameMode->DebugDeliverStaleSubmit();

	TestEqual(TEXT("new race is still in Ready"),
		static_cast<int32>(Harness.GameMode->GetPhase()),
		static_cast<int32>(EWSEventPhase::Ready));
	TestTrue(TEXT("no stale verdict leaked in"),
		Harness.GameMode->GetServerVerdict().IsEmpty());

	// And the new race still plays to completion.
	Harness.RunFor(32.0);
	TestTrue(TEXT("new race completes"),
		Harness.GameMode->GetPlayerRunner()->HasFinished());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRaceIdlePlayerTest,
	"WorldSports.Race.IdlePlayerStillGetsARace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRaceIdlePlayerTest::RunTest(const FString&)
{
	// A player who never touches the screen must still see a complete race
	// (auto-release, slow time, last place) — no soft lock, no empty result.
	FRaceHarness Harness;
	if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
	{
		return false;
	}
	Harness.RunFor(35.0);

	AWSSprintRunner* Player = Harness.GameMode->GetPlayerRunner();
	TestTrue(TEXT("idle player still finishes"), Player->HasFinished());
	TestEqual(TEXT("standings complete"),
		Harness.GameMode->GetStandings().Num(), AWSSprintTrack::LaneCount);
	TestEqual(TEXT("idle player comes last"),
		Harness.GameMode->GetPlayerPosition(), AWSSprintTrack::LaneCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSRaceRestartTest,
	"WorldSports.Race.RestartIsClean",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSRaceRestartTest::RunTest(const FString&)
{
	// "Race again" must fully reset: no leftover runners, standings or
	// verdict from the previous race leaking into the new one.
	FRaceHarness Harness;
	if (!TestNotNull(TEXT("game mode spawned"), Harness.GameMode))
	{
		return false;
	}
	Harness.RunFor(32.0);
	TestTrue(TEXT("first race produced standings"),
		Harness.GameMode->GetStandings().Num() > 0);

	Harness.GameMode->StartRace();
	TestEqual(TEXT("standings cleared"), Harness.GameMode->GetStandings().Num(), 0);
	TestTrue(TEXT("verdict cleared"), Harness.GameMode->GetServerVerdict().IsEmpty());
	TestEqual(TEXT("back to Ready"),
		static_cast<int32>(Harness.GameMode->GetPhase()),
		static_cast<int32>(EWSEventPhase::Ready));
	TestTrue(TEXT("clock reset before the gun"), Harness.GameMode->GetRaceClock() < 0.0);

	int32 RunnerCount = 0;
	for (TActorIterator<AWSSprintRunner> It(Harness.World); It; ++It)
	{
		++RunnerCount;
	}
	TestEqual(TEXT("exactly one field of runners"), RunnerCount, AWSSprintTrack::LaneCount);

	Harness.RunFor(32.0);
	TestTrue(TEXT("second race completes"),
		Harness.GameMode->GetPlayerRunner()->HasFinished());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
