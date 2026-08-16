#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/WSGameStateBase.h"
#include "Misc/AutomationTest.h"
#include "Race/WSSprintGameMode.h"
#include "Race/WSSprintRunner.h"
#include "Race/WSSprintTrack.h"
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

	explicit FRaceHarness()
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

	const FWSSprintOutcome Outcome = Player->GetOutcome();
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

	const FWSSprintOutcome Outcome = Player->GetOutcome();
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
	const FWSSprintOutcome Outcome = Player->GetOutcome();
	// Their reaction is their own late release — not the 1500ms auto-release
	// that a stranded player would have been given.
	TestTrue(FString::Printf(TEXT("reaction %.0fms is the player's own release"),
			Outcome.ReactionMs),
		Outcome.ReactionMs > 350.0 && Outcome.ReactionMs < 600.0);
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
