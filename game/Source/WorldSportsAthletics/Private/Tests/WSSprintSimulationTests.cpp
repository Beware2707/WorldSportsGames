#include "Misc/AutomationTest.h"
#include "Simulation/WSSprintDifficulty.h"
#include "Simulation/WSSprintEvents.h"
#include "Simulation/WSSprintSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FWSSprintAttributes UniformAttributes(float Level)
{
	FWSSprintAttributes Attributes;
	Attributes.Reaction = Level;
	Attributes.Acceleration = Level;
	Attributes.MaxSpeed = Level;
	Attributes.StrideEfficiency = Level;
	Attributes.Stamina = Level;
	// Recovery too, or the 400m's governing mean sits below the level being
	// asserted and every margin on that event is measured against a ceiling
	// the athlete was never actually racing to.
	Attributes.Recovery = Level;
	Attributes.Technique = Level;
	return Attributes;
}

/** The server's exact ceiling formula (backend/app/services/career.py). */
double ServerCeiling(const FWSSprintEventSpec& Spec, double MeanAttr)
{
	return Spec.CeilingAtZero + (Spec.CeilingAtHundred - Spec.CeilingAtZero)
		* FMath::Clamp(MeanAttr, 0.0, 100.0) / 100.0;
}

const FWSSprintEventSpec& Hundred()
{
	return WSSprintEvents::Find(TEXT("sprint-100m"));
}

TArray<FWSSprintInputEvent> BestRealisticTrace(const FWSSprintAttributes& Attributes,
	uint32 Seed, const FWSSprintEventSpec& Spec)
{
	// Consistency 1.0 = a flawless human rhythm, and 101ms is the fastest
	// reaction the false-start rule permits: together, the true ceiling of
	// legal play. Calibrating against a comfortable 130ms would leave the
	// best players able to beat a ceiling the server then rejects them for.
	return FWSSprintSimulation::GenerateAITrace(Attributes, Seed, Seed, 101.0, 0.0, 1.0, Spec);
}

TArray<FWSSprintInputEvent> BestRealisticTrace(const FWSSprintAttributes& Attributes, uint32 Seed)
{
	return BestRealisticTrace(Attributes, Seed, Hundred());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSSprintDeterminismTest,
	"WorldSports.Sprint.DeterministicReplay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSSprintDeterminismTest::RunTest(const FString&)
{
	// Same attributes + seed + input trace MUST reproduce the identical
	// race. Server-side replay validation and the finish replay depend on
	// this being exact, not approximate.
	const FWSSprintAttributes Attributes = UniformAttributes(55.0f);
	const TArray<FWSSprintInputEvent> Trace =
		FWSSprintSimulation::GenerateAITrace(Attributes, 42u, 42u, 180.0, 25.0, 0.7);

	const FWSSprintOutcome First = FWSSprintSimulation::RunTrace(Attributes, 42u, Trace);
	const FWSSprintOutcome Second = FWSSprintSimulation::RunTrace(Attributes, 42u, Trace);

	TestTrue(TEXT("finished"), First.bFinished && Second.bFinished);
	TestEqual(TEXT("identical time"), First.TimeSeconds, Second.TimeSeconds);
	TestEqual(TEXT("identical reaction"), First.ReactionMs, Second.ReactionMs);
	TestEqual(TEXT("identical wind"), First.Wind, Second.Wind);
	TestEqual(TEXT("identical split count"), First.Splits.Num(), Second.Splits.Num());
	for (int32 Index = 0; Index < First.Splits.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("split %d identical"), Index),
			First.Splits[Index], Second.Splits[Index]);
	}

	// A different seed is a different race (wind, band drift).
	const FWSSprintOutcome Other = FWSSprintSimulation::RunTrace(Attributes, 43u, Trace);
	TestNotEqual(TEXT("different seed differs"), First.TimeSeconds, Other.TimeSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSSprintCeilingCalibrationTest,
	"WorldSports.Sprint.PerfectPlayRespectsServerCeiling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSSprintCeilingCalibrationTest::RunTest(const FString&)
{
	// THE integration guarantee: an honestly simulated result must never be
	// rejected by the server's attribute ceiling — and perfect play should
	// land close enough above it that mastery feels rewarded.
	// EVERY event in the table, not just the 100m: a 200m that simulated
	// under its own ceiling would produce honest runs the server rejects.
	//
	// And every seed of a WIDE sample, not a token three. Wind is seeded and
	// worth well over a second on a 400m, so a three-seed check can pass
	// while the strongest legal tailwind — which the server's ceiling does
	// NOT forgive — sails straight under the ceiling in the wild.
	for (const FWSSprintEventSpec& Spec : WSSprintEvents::All())
	{
		// Tolerance scales with distance — a 400m has four times the race to
		// accumulate the same proportional error as a 100m.
		const double NearBand = 1.2 * (Spec.DistanceMetres / 100.0);
		for (const float Level : {0.0f, 25.0f, 40.0f, 55.0f, 70.0f, 85.0f, 100.0f})
		{
			const FWSSprintAttributes Attributes = UniformAttributes(Level);
			const double Ceiling = ServerCeiling(Spec, Level);

			double WorstMargin = TNumericLimits<double>::Max();
			double WorstTime = 0.0;
			uint32 WorstSeed = 0;
			double WorstWind = 0.0;
			double SlowestTime = 0.0;
			for (uint32 Seed = 1; Seed <= 32; ++Seed)
			{
				const FWSSprintOutcome Outcome = FWSSprintSimulation::RunTrace(
					Attributes, Seed, BestRealisticTrace(Attributes, Seed, Spec), Spec);
				TestTrue(FString::Printf(TEXT("%s attrs %.0f seed %u finished"),
						*Spec.Code, Level, Seed),
					Outcome.bFinished);
				TestEqual(FString::Printf(TEXT("%s reports %d splits"),
						*Spec.Code, Spec.SplitCount),
					Outcome.Splits.Num(), Spec.SplitCount);
				if (Outcome.TimeSeconds - Ceiling < WorstMargin)
				{
					WorstMargin = Outcome.TimeSeconds - Ceiling;
					WorstTime = Outcome.TimeSeconds;
					WorstSeed = Seed;
					WorstWind = Outcome.Wind;
				}
				SlowestTime = FMath::Max(SlowestTime, Outcome.TimeSeconds);
			}

			// The calibration table, in the log: tuning the event constants
			// is guesswork without seeing where the margin actually is.
			AddInfo(FString::Printf(
				TEXT("CALIB %-12s attrs %3.0f  worst %7.3f (seed %2u, wind %+.1f)")
				TEXT("  ceiling %7.3f  margin %+.3f  slowest %7.3f"),
				*Spec.Code, Level, WorstTime, WorstSeed, WorstWind,
				Ceiling, WorstMargin, SlowestTime));

			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f seed %u (wind %+.1f): %.3f must be >= ceiling %.3f"),
					*Spec.Code, Level, WorstSeed, WorstWind, WorstTime, Ceiling),
				WorstMargin >= -0.009); // server allows -0.01
			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: slowest perfect run %.3f should stay near ceiling %.3f"),
					*Spec.Code, Level, SlowestTime, Ceiling),
				SlowestTime <= Ceiling + NearBand);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSSprintLopsidedCeilingTest,
	"WorldSports.Sprint.LopsidedAttributesRespectTheCeiling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSSprintLopsidedCeilingTest::RunTest(const FString&)
{
	// Training raises ONE attribute at a time, so real athletes are lopsided.
	// The server's ceiling uses the MEAN of the governing attributes, so a
	// simulation that read max_speed alone would let a specialist run a time
	// the server then rejects — the game calling an honest player a cheat.
	const TArray<FWSSprintAttributes> Lopsided = []
	{
		TArray<FWSSprintAttributes> All;
		FWSSprintAttributes Sprinter;   // all the training went into speed
		Sprinter.Reaction = 40.0f; Sprinter.Acceleration = 40.0f;
		Sprinter.MaxSpeed = 95.0f; Sprinter.StrideEfficiency = 40.0f;
		Sprinter.Stamina = 40.0f; Sprinter.Technique = 40.0f;
		All.Add(Sprinter);

		FWSSprintAttributes Starter;    // all of it into the start
		Starter.Reaction = 95.0f; Starter.Acceleration = 95.0f;
		Starter.MaxSpeed = 40.0f; Starter.StrideEfficiency = 40.0f;
		Starter.Stamina = 40.0f; Starter.Technique = 40.0f;
		All.Add(Starter);

		FWSSprintAttributes Grinder;    // one attribute maxed, rest untouched
		Grinder.Reaction = 40.0f; Grinder.Acceleration = 40.0f;
		Grinder.MaxSpeed = 99.0f; Grinder.StrideEfficiency = 40.0f;
		Grinder.Stamina = 99.0f; Grinder.Technique = 99.0f;
		All.Add(Grinder);

		// Deliberate single-attribute exploits. Each of these is one
		// attribute at the cap with the rest left low, which drags the
		// governing MEAN — and so the server's ceiling — right down while
		// handing the simulation its strongest input for one term. If any
		// term can outrun the mean, one of these finds it.
		auto Specialist = [](float FWSSprintAttributes::*Field)
		{
			FWSSprintAttributes Attributes;
			Attributes.Reaction = 20.0f; Attributes.Acceleration = 20.0f;
			Attributes.MaxSpeed = 20.0f; Attributes.StrideEfficiency = 20.0f;
			Attributes.Stamina = 20.0f; Attributes.Recovery = 20.0f;
			Attributes.Technique = 20.0f;
			Attributes.*Field = 100.0f;
			return Attributes;
		};
		All.Add(Specialist(&FWSSprintAttributes::Stamina));          // fatigue
		All.Add(Specialist(&FWSSprintAttributes::Acceleration));     // tau
		All.Add(Specialist(&FWSSprintAttributes::StrideEfficiency)); // band
		All.Add(Specialist(&FWSSprintAttributes::Technique));        // band
		All.Add(Specialist(&FWSSprintAttributes::MaxSpeed));         // top end
		return All;
	}();

	for (const FWSSprintEventSpec& Spec : WSSprintEvents::All())
	{
		for (const FWSSprintAttributes& Attributes : Lopsided)
		{
			// The governing SET differs per event (the 400m counts recovery),
			// so the mean is taken against this event's own list.
			const double Mean = Attributes.GoverningMean(Spec.GoverningAttributes);
			const double Ceiling = ServerCeiling(Spec, Mean);
			// Sweep, don't sample: the strongest legal tailwind is seeded and
			// is worth more than the margin being asserted.
			double WorstMargin = TNumericLimits<double>::Max();
			double WorstTime = 0.0;
			uint32 WorstSeed = 0;
			for (uint32 Seed = 1; Seed <= 32; ++Seed)
			{
				const FWSSprintOutcome Outcome = FWSSprintSimulation::RunTrace(
					Attributes, Seed, BestRealisticTrace(Attributes, Seed, Spec), Spec);
				TestTrue(TEXT("finished"), Outcome.bFinished);
				if (Outcome.TimeSeconds - Ceiling < WorstMargin)
				{
					WorstMargin = Outcome.TimeSeconds - Ceiling;
					WorstTime = Outcome.TimeSeconds;
					WorstSeed = Seed;
				}
			}
			AddInfo(FString::Printf(
				TEXT("LOPSIDED %-12s mean %5.1f  worst %7.3f (seed %2u)")
				TEXT("  ceiling %7.3f  margin %+.3f"),
				*Spec.Code, Mean, WorstTime, WorstSeed, Ceiling, WorstMargin));
			TestTrue(FString::Printf(
					TEXT("%s mean %.1f seed %u: %.3f must not beat ceiling %.3f"),
					*Spec.Code, Mean, WorstSeed, WorstTime, Ceiling),
				WorstMargin >= -0.009);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSSprintSkillDecidesTest,
	"WorldSports.Sprint.CadenceAccuracyDecidesTheRace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSSprintSkillDecidesTest::RunTest(const FString&)
{
	// Identical athletes, identical seed: the better rhythm must win by a
	// meaningful margin. "Attributes raise the ceiling; execution decides."
	const FWSSprintAttributes Attributes = UniformAttributes(50.0f);
	const FWSSprintOutcome Sharp = FWSSprintSimulation::RunTrace(Attributes, 77u,
		FWSSprintSimulation::GenerateAITrace(Attributes, 77u, 77u, 150.0, 0.0, 0.95));
	const FWSSprintOutcome Sloppy = FWSSprintSimulation::RunTrace(Attributes, 77u,
		FWSSprintSimulation::GenerateAITrace(Attributes, 77u, 77u, 150.0, 0.0, 0.10));

	TestTrue(TEXT("both finish"), Sharp.bFinished && Sloppy.bFinished);
	TestTrue(FString::Printf(TEXT("sharp %.3f beats sloppy %.3f by >= 0.35s"),
			Sharp.TimeSeconds, Sloppy.TimeSeconds),
		Sloppy.TimeSeconds - Sharp.TimeSeconds >= 0.35);

	// And a World Class athlete played terribly loses to a Regional athlete
	// played well — the anti-pay-to-win constraint, executable.
	const FWSSprintOutcome EliteBadly = FWSSprintSimulation::RunTrace(
		UniformAttributes(85.0f), 77u,
		FWSSprintSimulation::GenerateAITrace(UniformAttributes(85.0f), 77u, 77u, 150.0, 0.0, 0.05));
	const FWSSprintOutcome RegionalWell = FWSSprintSimulation::RunTrace(
		UniformAttributes(45.0f), 77u,
		FWSSprintSimulation::GenerateAITrace(UniformAttributes(45.0f), 77u, 77u, 150.0, 0.0, 0.98));
	TestTrue(FString::Printf(
			TEXT("regional played well (%.3f) beats elite played badly (%.3f)"),
			RegionalWell.TimeSeconds, EliteBadly.TimeSeconds),
		RegionalWell.TimeSeconds < EliteBadly.TimeSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSSprintFalseStartTest,
	"WorldSports.Sprint.FalseStartRules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSSprintFalseStartTest::RunTest(const FString&)
{
	const FWSSprintAttributes Attributes = UniformAttributes(50.0f);

	// Sub-100ms reaction: physically impossible as a reaction to the gun.
	{
		TArray<FWSSprintInputEvent> Trace;
		Trace.Add({-2.0, EWSSprintInputType::HoldStart});
		Trace.Add({0.060, EWSSprintInputType::Release});
		const FWSSprintOutcome Outcome = FWSSprintSimulation::RunTrace(Attributes, 5u, Trace);
		TestTrue(TEXT("60ms is a false start"), Outcome.bFalseStart);
		TestFalse(TEXT("no time for a false start"), Outcome.bFinished);
	}
	// Leaving before the gun.
	{
		TArray<FWSSprintInputEvent> Trace;
		Trace.Add({-2.0, EWSSprintInputType::HoldStart});
		Trace.Add({-0.3, EWSSprintInputType::Release});
		const FWSSprintOutcome Outcome = FWSSprintSimulation::RunTrace(Attributes, 5u, Trace);
		TestTrue(TEXT("pre-gun release is a false start"), Outcome.bFalseStart);
	}
	// 101ms is legal, if superhuman.
	{
		TArray<FWSSprintInputEvent> Trace;
		Trace.Add({-2.0, EWSSprintInputType::HoldStart});
		Trace.Add({0.101, EWSSprintInputType::Release});
		const FWSSprintOutcome Outcome = FWSSprintSimulation::RunTrace(Attributes, 5u, Trace);
		TestFalse(TEXT("101ms stands"), Outcome.bFalseStart);
		TestTrue(TEXT("race completes"), Outcome.bFinished);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSSprintSplitCoherenceTest,
	"WorldSports.Sprint.SplitsSatisfyServerValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSSprintSplitCoherenceTest::RunTest(const FString&)
{
	// Splits must pass the server's exact checks, for EVERY event: the
	// expected count, each split above that event's floor, and reaction +
	// splits recomposing the official time. We hold ourselves to half the
	// server's tolerance. A 400m checked against the 100m's 0.75s floor
	// proves nothing — the floors differ per event and so must the test.
	for (const FWSSprintEventSpec& Spec : WSSprintEvents::All())
	{
		for (const uint32 Seed : {11u, 222u, 3333u})
		{
			// A mid-table athlete playing loosely — the ordinary case the
			// validator sees most, not a calibration extreme.
			const FWSSprintAttributes Attributes = UniformAttributes(60.0f);
			const FWSSprintOutcome Outcome = FWSSprintSimulation::RunTrace(Attributes, Seed,
				FWSSprintSimulation::GenerateAITrace(
					Attributes, Seed, Seed, 170.0, 30.0, 0.8, Spec), Spec);
			TestTrue(TEXT("finished"), Outcome.bFinished);
			TestEqual(FString::Printf(TEXT("%s split count"), *Spec.Code),
				Outcome.Splits.Num(), Spec.SplitCount);

			double Sum = 0.0;
			for (const double Split : Outcome.Splits)
			{
				TestTrue(FString::Printf(TEXT("%s split %.3f >= server floor %.2f"),
						*Spec.Code, Split, Spec.MinSplitSeconds),
					Split >= Spec.MinSplitSeconds);
				Sum += Split;
			}
			const double Recomposed = Outcome.ReactionMs / 1000.0 + Sum;
			// The server's tolerance is max(0.05, 1% of the time), so the
			// half-tolerance we hold ourselves to scales with the event.
			const double Tolerance = FMath::Max(0.025, 0.005 * Outcome.TimeSeconds);
			TestTrue(FString::Printf(
					TEXT("%s reaction+splits (%.3f) matches time (%.3f)"),
					*Spec.Code, Recomposed, Outcome.TimeSeconds),
				FMath::Abs(Recomposed - Outcome.TimeSeconds) <= Tolerance);

			TestTrue(TEXT("wind in the generated band"),
				Outcome.Wind >= -1.5 && Outcome.Wind <= 2.0);
			TestTrue(TEXT("reaction is legal"), Outcome.ReactionMs >= 100.0);
			TestTrue(FString::Printf(
					TEXT("%s time %.3f inside the server's plausible band [%.1f, %.1f]"),
					*Spec.Code, Outcome.TimeSeconds,
					Spec.MinPlausibleSeconds, Spec.MaxPlausibleSeconds),
				Outcome.TimeSeconds > Spec.MinPlausibleSeconds &&
					Outcome.TimeSeconds < Spec.MaxPlausibleSeconds);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSSprintDifficultyLadderTest,
	"WorldSports.Sprint.DifficultyLadderOrders",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSSprintDifficultyLadderTest::RunTest(const FString&)
{
	// Averaged over seeds, each tier must beat the one below it — through
	// input quality alone, since that is all a tier is allowed to change.
	const TArray<FWSSprintDifficultyLevel>& Levels = WSSprintDifficulty::Levels();
	TestEqual(TEXT("five tiers"), Levels.Num(), 5);

	TArray<double> Means;
	for (const FWSSprintDifficultyLevel& Level : Levels)
	{
		double Total = 0.0;
		int32 Finished = 0;
		for (uint32 Seed = 100; Seed < 112; ++Seed)
		{
			const FWSSprintAttributes Attributes = Level.MakeAttributes();
			const FWSSprintOutcome Outcome = FWSSprintSimulation::RunTrace(
				Attributes, Seed,
				FWSSprintSimulation::GenerateAITrace(
				Attributes, Seed, Seed,
					Level.ReactionMeanMs, Level.ReactionSpreadMs, Level.Consistency));
			if (Outcome.bFinished)
			{
				Total += Outcome.TimeSeconds;
				++Finished;
			}
		}
		TestEqual(FString::Printf(TEXT("%s always finishes"), Level.Name), Finished, 12);
		Means.Add(Total / Finished);
	}
	for (int32 Index = 1; Index < Means.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("%s (%.3f) beats %s (%.3f)"),
				Levels[Index].Name, Means[Index],
				Levels[Index - 1].Name, Means[Index - 1]),
			Means[Index] < Means[Index - 1] - 0.1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSSprintDigestAndIdleTest,
	"WorldSports.Sprint.DigestStableAndIdleRaceCompletes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSSprintDigestAndIdleTest::RunTest(const FString&)
{
	const FWSSprintAttributes Attributes = UniformAttributes(50.0f);
	const TArray<FWSSprintInputEvent> Trace =
		FWSSprintSimulation::GenerateAITrace(Attributes, 9u, 9u, 180.0, 20.0, 0.7);

	const FString DigestA = FWSSprintSimulation::DigestTrace(Trace);
	const FString DigestB = FWSSprintSimulation::DigestTrace(Trace);
	TestEqual(TEXT("digest is stable"), DigestA, DigestB);
	TestEqual(TEXT("digest is 40 hex chars"), DigestA.Len(), 40);

	TArray<FWSSprintInputEvent> Altered = Trace;
	Altered.Last().TimeSeconds += 0.01;
	TestNotEqual(TEXT("altered trace has a different digest"),
		DigestA, FWSSprintSimulation::DigestTrace(Altered));

	// A player who never touches the screen still gets a race: auto-release
	// plus baseline "jog" accuracy — slow, legal, and it always ends.
	const FWSSprintOutcome Idle =
		FWSSprintSimulation::RunTrace(Attributes, 9u, {});
	TestTrue(TEXT("idle race finishes"), Idle.bFinished);
	TestTrue(TEXT("idle reaction is the auto-release"), Idle.ReactionMs >= 1000.0);
	TestTrue(FString::Printf(TEXT("idle time %.2f is slow but plausible"), Idle.TimeSeconds),
		Idle.TimeSeconds > 12.5 && Idle.TimeSeconds < 60.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
