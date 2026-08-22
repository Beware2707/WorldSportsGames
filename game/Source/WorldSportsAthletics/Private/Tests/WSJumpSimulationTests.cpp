#include "Misc/AutomationTest.h"
#include "Tests/WSTestAthlete.h"
#include "Simulation/WSJumpSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
/**
 * The server's ceiling formula. Note the direction: for a distance event
 * the ceiling is a MAXIMUM, so a legal mark is at or BELOW it — the
 * opposite of every timed event in this project.
 */
double ServerCeiling(const FWSJumpEventSpec& Spec, double MeanAttr)
{
	return Spec.CeilingAtZero + (Spec.CeilingAtHundred - Spec.CeilingAtZero)
		* FMath::Clamp(MeanAttr, 0.0, 100.0) / 100.0;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSJumpDeterminismTest,
	"WorldSports.Jump.DeterministicReplay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSJumpDeterminismTest::RunTest(const FString&)
{
	const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(60.0f);
	const FWSJumpEventSpec& Spec = WSJumpEvents::Find(TEXT("jump-long"));
	const TArray<FWSJumpInputEvent> Trace =
		FWSJumpSimulation::GenerateAITrace(Attributes, 42u, 42u, 0.8, Spec);

	const FWSJumpOutcome First =
		FWSJumpSimulation::RunTrace(Attributes, 42u, Trace, Spec);
	const FWSJumpOutcome Second =
		FWSJumpSimulation::RunTrace(Attributes, 42u, Trace, Spec);

	TestTrue(TEXT("finished"), First.bFinished && Second.bFinished);
	TestEqual(TEXT("identical mark"), First.DistanceMetres, Second.DistanceMetres);
	TestEqual(TEXT("identical board gap"), First.BoardGapMetres, Second.BoardGapMetres);
	TestEqual(TEXT("identical wind"), First.Wind, Second.Wind);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSJumpCeilingTest,
	"WorldSports.Jump.PerfectJumpRespectsServerCeiling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSJumpCeilingTest::RunTest(const FString&)
{
	// The integration guarantee, in the OTHER direction. For every timed
	// event so far the danger was running too fast; here it is jumping too
	// far. A mark above the ceiling is one the server refuses, and the
	// player is told their honest jump does not count.
	for (const FWSJumpEventSpec& Spec : WSJumpEvents::All())
	{
		if (Spec.bVertical)
		{
			continue; // a bar is cleared or not; its own test covers that
		}
		for (const float Level : {0.0f, 25.0f, 40.0f, 55.0f, 70.0f, 85.0f, 100.0f})
		{
			const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(Level);
			const double Ceiling = ServerCeiling(Spec, Level);

			double BestMark = 0.0;
			uint32 BestSeed = 0;
			double ShortestMark = TNumericLimits<double>::Max();
			for (uint32 Seed = 1; Seed <= 32; ++Seed)
			{
				// Consistency 1.0: a flawless approach onto the board, which
				// is the best a player can legally do.
				const TArray<FWSJumpInputEvent> Trace =
					FWSJumpSimulation::GenerateAITrace(Attributes, Seed, Seed, 1.0, Spec);
				const FWSJumpOutcome Outcome =
					FWSJumpSimulation::RunTrace(Attributes, Seed, Trace, Spec);
				TestTrue(FString::Printf(TEXT("%s attrs %.0f seed %u finished"),
						*Spec.Code, Level, Seed),
					Outcome.bFinished);
				TestFalse(FString::Printf(
						TEXT("%s attrs %.0f seed %u: a perfect approach does not foul"),
						*Spec.Code, Level, Seed),
					Outcome.bFoul);
				// Every mark the client reports must be one the server can
				// accept: above its plausible minimum, below its maximum.
				TestTrue(FString::Printf(
						TEXT("%s attrs %.0f seed %u: %.2f m is a mark the server accepts"),
						*Spec.Code, Level, Seed, Outcome.DistanceMetres),
					Outcome.DistanceMetres >= Spec.MinPlausibleMetres &&
						Outcome.DistanceMetres <= Spec.MaxPlausibleMetres);
				if (Outcome.DistanceMetres > BestMark)
				{
					BestMark = Outcome.DistanceMetres;
					BestSeed = Seed;
				}
				ShortestMark = FMath::Min(ShortestMark, Outcome.DistanceMetres);
			}

			AddInfo(FString::Printf(
				TEXT("JUMP %-10s attrs %3.0f  best %5.2f m (seed %2u)")
				TEXT("  ceiling %5.2f m  margin %+.2f  shortest %5.2f"),
				*Spec.Code, Level, BestMark, BestSeed, Ceiling,
				Ceiling - BestMark, ShortestMark));

			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f seed %u: %.2f m must NOT exceed ceiling %.2f m"),
					*Spec.Code, Level, BestSeed, BestMark, Ceiling),
				BestMark <= Ceiling + 0.009);
			// And it has to be worth jumping: a ceiling nobody can approach
			// makes every attribute point meaningless.
			//
			// PROPORTIONAL, not a fixed 0.75m. A long jump spans five
			// metres between its endpoints and a triple jump nearly twelve,
			// so one absolute tolerance is either vacuous for the short
			// event or impossible for the long one. Eight per cent says the
			// same thing about both.
			const double Reach = FMath::Max(0.35, Ceiling * 0.08);
			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: best %.2f m should come within %.2f m "
						 "of the ceiling %.2f m"),
					*Spec.Code, Level, BestMark, Reach, Ceiling),
				BestMark >= Ceiling - Reach);
			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: %.2f m is inside the server's plausible band"),
					*Spec.Code, Level, ShortestMark),
				ShortestMark >= Spec.MinPlausibleMetres &&
					BestMark <= Spec.MaxPlausibleMetres);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSJumpBoardDecidesTest,
	"WorldSports.Jump.HittingTheBoardDecidesTheJump",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSJumpBoardDecidesTest::RunTest(const FString&)
{
	// The mark is measured from the BOARD, so a jump that leaves the ground
	// half a metre early is half a metre shorter — however good the flight
	// was. This is the whole tension of the event and it must be real.
	const FWSJumpEventSpec& Spec = WSJumpEvents::Find(TEXT("jump-long"));
	const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(70.0f);

	for (const uint32 Seed : {6u, 77u, 808u})
	{
		// Take the approach of a perfect jumper, then move ONLY the takeoff
		// earlier: same speed, same rhythm, same everything but the board.
		const TArray<FWSJumpInputEvent> Perfect =
			FWSJumpSimulation::GenerateAITrace(Attributes, Seed, Seed, 1.0, Spec);
		const FWSJumpOutcome OnBoard =
			FWSJumpSimulation::RunTrace(Attributes, Seed, Perfect, Spec);

		TArray<FWSJumpInputEvent> Early;
		for (const FWSJumpInputEvent& Event : Perfect)
		{
			if (Event.Type == EWSJumpInputType::Takeoff)
			{
				FWSJumpInputEvent Moved = Event;
				Moved.TimeSeconds -= 0.06; // ~0.6 m at approach speed
				Early.Add(Moved);
				continue;
			}
			Early.Add(Event);
		}
		const FWSJumpOutcome Short =
			FWSJumpSimulation::RunTrace(Attributes, Seed, Early, Spec);

		TestTrue(TEXT("both jumps happened"), OnBoard.bFinished && Short.bFinished);
		TestFalse(TEXT("neither fouled"), OnBoard.bFoul || Short.bFoul);
		TestTrue(FString::Printf(
				TEXT("seed %u: leaving early costs the gap (%.2f m on the board, ")
				TEXT("%.2f m from %.2f m short)"),
				Seed, OnBoard.DistanceMetres, Short.DistanceMetres,
				Short.BoardGapMetres),
			Short.DistanceMetres < OnBoard.DistanceMetres - 0.20);
		TestTrue(TEXT("the gap is what was lost"),
			Short.BoardGapMetres > OnBoard.BoardGapMetres);

		AddInfo(FString::Printf(
			TEXT("BOARD seed %4u  on the board %5.2f m (gap %.2f)  early %5.2f m (gap %.2f)"),
			Seed, OnBoard.DistanceMetres, OnBoard.BoardGapMetres,
			Short.DistanceMetres, Short.BoardGapMetres));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSJumpFoulTest,
	"WorldSports.Jump.OversteppingTheBoardIsNoMark",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSJumpFoulTest::RunTest(const FString&)
{
	// A centimetre past the board is a foul and NO MARK — not a shorter
	// jump, not a penalty. The asymmetry against taking off early is the
	// reason hitting the board is a nerve-holding skill rather than a
	// button press.
	const FWSJumpEventSpec& Spec = WSJumpEvents::Find(TEXT("jump-long"));
	const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(70.0f);

	for (const uint32 Seed : {6u, 77u, 808u})
	{
		const TArray<FWSJumpInputEvent> Perfect =
			FWSJumpSimulation::GenerateAITrace(Attributes, Seed, Seed, 1.0, Spec);

		TArray<FWSJumpInputEvent> Late;
		for (const FWSJumpInputEvent& Event : Perfect)
		{
			if (Event.Type == EWSJumpInputType::Takeoff)
			{
				FWSJumpInputEvent Moved = Event;
				Moved.TimeSeconds += 0.25; // well past the board
				Late.Add(Moved);
				continue;
			}
			Late.Add(Event);
		}
		const FWSJumpOutcome Fouled =
			FWSJumpSimulation::RunTrace(Attributes, Seed, Late, Spec);

		TestTrue(FString::Printf(TEXT("seed %u: overstepping is a foul"), Seed),
			Fouled.bFoul);
		TestTrue(FString::Printf(TEXT("seed %u: and the reason is the overstep"), Seed),
			Fouled.bOverstepped);
		TestEqual(FString::Printf(TEXT("seed %u: a foul has no mark"), Seed),
			Fouled.DistanceMetres, 0.0);
	}

	// A takeoff so early the jump never reaches the pit is a FAILED
	// attempt, not a jump of zero metres. Recording 0.00m claimed a
	// measurement nobody took, and the server would refuse it anyway for
	// being under the plausible minimum.
	{
		TArray<FWSJumpInputEvent> FarTooEarly;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			FWSJumpInputEvent Tap;
			Tap.TimeSeconds = 0.12 + Index * 0.22;
			Tap.Type = EWSJumpInputType::Tap;
			FarTooEarly.Add(Tap);
		}
		FWSJumpInputEvent Early;
		Early.TimeSeconds = 0.30; // barely off the top of the runway
		Early.Type = EWSJumpInputType::Takeoff;
		FarTooEarly.Add(Early);

		const FWSJumpOutcome ShortOfPit =
			FWSJumpSimulation::RunTrace(Attributes, 9u, FarTooEarly, Spec);
		TestTrue(TEXT("a jump short of the pit is no mark"), ShortOfPit.bFoul);
		// The cut is the SERVER's plausible minimum, so the client can never
		// measure a mark the server would refuse as implausible.
		TestTrue(TEXT("no mark is recorded below the server's minimum"),
			ShortOfPit.DistanceMetres == 0.0);
		TestFalse(TEXT("and it was not an overstep"), ShortOfPit.bOverstepped);
		TestEqual(TEXT("no mark means no measurement"),
			ShortOfPit.DistanceMetres, 0.0);
	}

	// Never taking off at all runs through the pit, which is also no mark.
	TArray<FWSJumpInputEvent> NeverJumps;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		FWSJumpInputEvent Tap;
		Tap.TimeSeconds = 0.12 + Index * 0.22;
		Tap.Type = EWSJumpInputType::Tap;
		NeverJumps.Add(Tap);
	}
	const FWSJumpOutcome RanThrough =
		FWSJumpSimulation::RunTrace(Attributes, 11u, NeverJumps, Spec);
	TestTrue(TEXT("running through the pit is a foul"), RanThrough.bFoul);
	TestTrue(TEXT("recorded as going over the board"), RanThrough.bOverstepped);
	TestEqual(TEXT("and has no mark"), RanThrough.DistanceMetres, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSJumpSkillOverAttributesTest,
	"WorldSports.Jump.ExecutionDecidesTheJump",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSJumpSkillOverAttributesTest::RunTest(const FString&)
{
	// Attributes raise the ceiling; execution decides the jump. A modest
	// jumper who hits the board must beat a far better one who does not.
	const FWSJumpEventSpec& Spec = WSJumpEvents::Find(TEXT("jump-long"));
	const FWSSprintAttributes Modest = WSTestAthlete::Uniform(55.0f);
	const FWSSprintAttributes Strong = WSTestAthlete::Uniform(80.0f);

	for (const uint32 Seed : {3u, 44u, 515u})
	{
		const FWSJumpOutcome ModestOnBoard = FWSJumpSimulation::RunTrace(
			Modest, Seed,
			FWSJumpSimulation::GenerateAITrace(Modest, Seed, Seed, 1.0, Spec), Spec);
		const FWSJumpOutcome StrongSloppy = FWSJumpSimulation::RunTrace(
			Strong, Seed,
			FWSJumpSimulation::GenerateAITrace(Strong, Seed, Seed, 0.15, Spec), Spec);

		TestFalse(TEXT("the judged jump stands"), ModestOnBoard.bFoul);
		TestTrue(FString::Printf(
				TEXT("seed %u: a judged %.2f m must beat a sloppy %.2f m"),
				Seed, ModestOnBoard.DistanceMetres, StrongSloppy.DistanceMetres),
			ModestOnBoard.DistanceMetres > StrongSloppy.DistanceMetres);
	}

	// And the converse: with the board hit equally well, attributes decide.
	for (const uint32 Seed : {3u, 44u, 515u})
	{
		const FWSJumpOutcome ModestRun = FWSJumpSimulation::RunTrace(
			Modest, Seed,
			FWSJumpSimulation::GenerateAITrace(Modest, Seed, Seed, 1.0, Spec), Spec);
		const FWSJumpOutcome StrongRun = FWSJumpSimulation::RunTrace(
			Strong, Seed,
			FWSJumpSimulation::GenerateAITrace(Strong, Seed, Seed, 1.0, Spec), Spec);
		TestTrue(TEXT("equal execution: attributes decide"),
			StrongRun.DistanceMetres > ModestRun.DistanceMetres);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSTripleJumpTest,
	"WorldSports.Jump.TheRhythmIsTheTripleJump",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSTripleJumpTest::RunTest(const FString&)
{
	// A triple jump is a hop, a step and a jump, and the two takeoffs
	// between them are the event. This is the check that they MATTER: the
	// same athlete off the same board, timing the transitions or not
	// timing them, must not measure the same.
	for (const FWSJumpEventSpec& Spec : WSJumpEvents::All())
	{
		if (Spec.PhaseCount <= 1)
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: a phase share for every phase"), *Spec.Code),
			Spec.PhaseShares.Num(), Spec.PhaseCount);
		double ShareTotal = 0.0;
		for (const double Share : Spec.PhaseShares)
		{
			ShareTotal += Share;
		}
		// If the shares did not sum to one, the jump would measure more (or
		// less) than the athlete actually covered.
		TestEqual(FString::Printf(TEXT("%s: the phases are the whole jump"), *Spec.Code),
			ShareTotal, 1.0, /*Tolerance=*/1.0e-9);

		const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(55.0f);
		for (uint32 Seed = 1; Seed <= 8; ++Seed)
		{
			// A jumper who times both transitions.
			const TArray<FWSJumpInputEvent> Rhythmic =
				FWSJumpSimulation::GenerateAITrace(Attributes, Seed, Seed, 1.0, Spec);
			const FWSJumpOutcome OnRhythm =
				FWSJumpSimulation::RunTrace(Attributes, Seed, Rhythmic, Spec);

			// The SAME jumper off the same board who never takes off again:
			// strip every takeoff after the first and nothing else changes.
			TArray<FWSJumpInputEvent> Stumbling;
			bool bTookOff = false;
			for (const FWSJumpInputEvent& Event : Rhythmic)
			{
				if (Event.Type == EWSJumpInputType::Takeoff)
				{
					if (bTookOff)
					{
						continue;
					}
					bTookOff = true;
				}
				Stumbling.Add(Event);
			}
			const FWSJumpOutcome Stumbled =
				FWSJumpSimulation::RunTrace(Attributes, Seed, Stumbling, Spec);

			if (OnRhythm.bFoul || Stumbled.bFoul)
			{
				continue; // a fouled approach says nothing about the rhythm
			}
			TestTrue(FString::Printf(
					TEXT("%s seed %u: timing the hop and the step (%.2f m) must beat "
						 "stumbling through them (%.2f m)"),
					*Spec.Code, Seed, OnRhythm.DistanceMetres, Stumbled.DistanceMetres),
				OnRhythm.DistanceMetres > Stumbled.DistanceMetres + 0.10);
			// Both takeoffs are recorded, and the FIRST one is not one of
			// them: the board takeoff is judged by the gap to the board.
			TestEqual(FString::Printf(TEXT("%s: one error per transition"), *Spec.Code),
				OnRhythm.PhaseErrors.Num(), Spec.PhaseCount - 1);
			if (Seed == 1)
			{
				AddInfo(FString::Printf(
					TEXT("RHYTHM %-11s on-rhythm %5.2f m   stumbled %5.2f m   cost %.2f m"),
					*Spec.Code, OnRhythm.DistanceMetres, Stumbled.DistanceMetres,
					OnRhythm.DistanceMetres - Stumbled.DistanceMetres));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSHighJumpTest,
	"WorldSports.Jump.TheBarIsClearedOrItIsNot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSHighJumpTest::RunTest(const FString&)
{
	// A vertical jump asks a different question from a horizontal one: not
	// how far, but whether the bar survived — and the MARK is the bar, not
	// the arc. Clearing 1.80m by a foot still records 1.80m.
	for (const FWSJumpEventSpec& Base : WSJumpEvents::All())
	{
		if (!Base.bVertical)
		{
			continue;
		}
		for (const float Level : {25.0f, 55.0f, 85.0f})
		{
			const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(Level);
			const double Ceiling = ServerCeiling(Base, Level);

			// Walk the bar up until the athlete can no longer clear it —
			// which is exactly what the competition does.
			double HighestCleared = 0.0;
			for (double Bar = Base.StartBarMetres; Bar <= Base.MaxPlausibleMetres;
				Bar += Base.BarIncrementMetres)
			{
				FWSJumpEventSpec Spec = Base;
				Spec.BarMetres = Bar;
				bool bClearedAny = false;
				for (uint32 Seed = 1; Seed <= 6; ++Seed)
				{
					const FWSJumpOutcome Outcome = FWSJumpSimulation::RunTrace(
						Attributes, Seed,
						FWSJumpSimulation::GenerateAITrace(Attributes, Seed, Seed, 1.0, Spec),
						Spec);
					if (Outcome.bCleared)
					{
						bClearedAny = true;
						// The mark is the BAR, not how high the arc went.
						TestEqual(TEXT("a cleared bar records the bar"),
							Outcome.DistanceMetres, Bar);
					}
					else
					{
						// Failing to clear is NOT a foul in the sense of a
						// disqualification — the athlete may try again.
						TestEqual(TEXT("a missed bar records no mark"),
							Outcome.DistanceMetres, 0.0);
					}
				}
				if (!bClearedAny)
				{
					break;
				}
				HighestCleared = Bar;
			}

			AddInfo(FString::Printf(
				TEXT("BAR %-10s attrs %3.0f  highest cleared %.2f m  ceiling %.2f m"),
				*Base.Code, Level, HighestCleared, Ceiling));

			// The same integration guarantee as every other event, in the
			// direction a distance event runs: never above the ceiling, and
			// close enough to it that attributes are worth training.
			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: %.2f m must NOT exceed ceiling %.2f m"),
					*Base.Code, Level, HighestCleared, Ceiling),
				HighestCleared <= Ceiling + 0.009);
			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: %.2f m should approach the ceiling %.2f m"),
					*Base.Code, Level, HighestCleared, Ceiling),
				HighestCleared >= Ceiling - FMath::Max(0.10, Ceiling * 0.10));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSTripleJumpPenaltyTest,
	"WorldSports.Jump.AMissCostsThePhaseItLaunches",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSTripleJumpPenaltyTest::RunTest(const FString&)
{
	// A mistimed takeoff costs the phase it LAUNCHES, and only that phase.
	//
	// The penalty used to be subtracted from the whole remaining jump, so
	// it landed at a fraction of its size AND shortened every later phase:
	// a completely missed hop-to-step takeoff took 3% off the JUMP, which
	// had been timed perfectly.
	//
	// The traces are built CLOSED-LOOP, tapping each transition on its own
	// landing except the one being missed. Deleting a takeoff from a fixed
	// trace does not isolate the model: a shortened phase lands earlier, so
	// the next takeoff — still stamped at its original time — is late too,
	// and the two effects cannot be told apart.
	for (const FWSJumpEventSpec& Spec : WSJumpEvents::All())
	{
		if (Spec.PhaseCount < 3)
		{
			continue;
		}
		TestTrue(TEXT("the jump phase is longer than the step"),
			Spec.PhaseShares[2] > Spec.PhaseShares[1]);

		const FWSSprintAttributes Attributes = WSTestAthlete::Uniform(55.0f);

		// Tap the approach and the board exactly as a good jumper does,
		// then tap every transition ON its landing except SkipPhase.
		auto TraceMissing = [&Attributes, &Spec](uint32 Seed, int32 SkipPhase)
		{
			TArray<FWSJumpInputEvent> Trace;
			FWSJumpSimulation Shadow(Attributes, Seed, Spec);
			double NextTapAt = 0.12;
			bool bJumped = false;
			int32 TappedPhase = 0;
			while (Shadow.Step())
			{
				const FWSJumpState& Live = Shadow.GetState();
				while (NextTapAt <= Live.RaceTime && !bJumped)
				{
					FWSJumpInputEvent Tap;
					Tap.TimeSeconds = NextTapAt;
					Tap.Type = EWSJumpInputType::Tap;
					Trace.Add(Tap);
					Shadow.AddInput(Tap);
					NextTapAt += 1.0 /
						FMath::Max(Shadow.TargetCadenceAt(Live.Distance), 0.5);
				}
				if (!bJumped && Live.MetresToBoard <= 0.03 + Live.Speed *
					FWSJumpSimulation::StepDt)
				{
					FWSJumpInputEvent Takeoff;
					Takeoff.TimeSeconds = Live.RaceTime;
					Takeoff.Type = EWSJumpInputType::Takeoff;
					Trace.Add(Takeoff);
					Shadow.AddInput(Takeoff);
					bJumped = true;
				}
				// The transition INTO Live.Phase + 1, timed on the landing
				// of the phase in the air — unless this is the one missed.
				if (Live.bPhaseWindowOpen && Live.Phase != TappedPhase &&
					Live.PhaseTimeRemaining <= 0.0)
				{
					TappedPhase = Live.Phase;
					if (Live.Phase + 1 != SkipPhase)
					{
						FWSJumpInputEvent Phase;
						Phase.TimeSeconds = Live.RaceTime;
						Phase.Type = EWSJumpInputType::Takeoff;
						Trace.Add(Phase);
						Shadow.AddInput(Phase);
					}
				}
			}
			return Trace;
		};

		for (uint32 Seed = 1; Seed <= 6; ++Seed)
		{
			const FWSJumpOutcome OnRhythm = FWSJumpSimulation::RunTrace(
				Attributes, Seed, TraceMissing(Seed, /*SkipPhase=*/0), Spec);
			const FWSJumpOutcome MissedStep = FWSJumpSimulation::RunTrace(
				Attributes, Seed, TraceMissing(Seed, /*SkipPhase=*/2), Spec);
			const FWSJumpOutcome MissedJump = FWSJumpSimulation::RunTrace(
				Attributes, Seed, TraceMissing(Seed, /*SkipPhase=*/3), Spec);
			if (OnRhythm.bFoul || MissedStep.bFoul || MissedJump.bFoul)
			{
				continue;
			}

			const double StepCost = OnRhythm.DistanceMetres - MissedStep.DistanceMetres;
			const double JumpCost = OnRhythm.DistanceMetres - MissedJump.DistanceMetres;

			// Each miss is charged to its own phase, so the cost is that
			// phase's share of the jump times the penalty. The jump phase is
			// longer than the step, so missing it costs more.
			TestTrue(FString::Printf(
					TEXT("%s seed %u: missing the takeoff into the JUMP (%.2f m) must "
						 "cost more than missing the one into the STEP (%.2f m), "
						 "because the jump is the longer phase"),
					*Spec.Code, Seed, JumpCost, StepCost),
				JumpCost > StepCost);

			for (int32 Phase = 1; Phase <= 2; ++Phase)
			{
				const double Cost = Phase == 1 ? StepCost : JumpCost;
				const double Expected = OnRhythm.DistanceMetres *
					Spec.PhaseShares[Phase] * Spec.PhaseMissPenalty;
				TestTrue(FString::Printf(
						TEXT("%s seed %u: missing phase %d costs %.2f m, which must be "
							 "about the %.0f%% of that phase it is charged (%.2f m)"),
						*Spec.Code, Seed, Phase + 1, Cost,
						Spec.PhaseMissPenalty * 100.0, Expected),
					Cost > Expected * 0.80 && Cost < Expected * 1.30);
			}
			if (Seed == 1)
			{
				AddInfo(FString::Printf(
					TEXT("PHASECOST %-11s clean %.2f m   missed step -%.2f m")
					TEXT("   missed jump -%.2f m"),
					*Spec.Code, OnRhythm.DistanceMetres, StepCost, JumpCost));
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
