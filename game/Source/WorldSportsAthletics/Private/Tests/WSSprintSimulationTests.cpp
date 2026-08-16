#include "Misc/AutomationTest.h"
#include "Simulation/WSSprintDifficulty.h"
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
	Attributes.Technique = Level;
	return Attributes;
}

/** The server's exact ceiling formula (backend/app/services/career.py). */
double ServerCeiling(double MeanAttr)
{
	return 13.5 + (9.55 - 13.5) * FMath::Clamp(MeanAttr, 0.0, 100.0) / 100.0;
}

TArray<FWSSprintInputEvent> BestRealisticTrace(const FWSSprintAttributes& Attributes, uint32 Seed)
{
	// Consistency 1.0 = a flawless human rhythm, the ceiling of play.
	return FWSSprintSimulation::GenerateAITrace(Attributes, Seed, Seed, 130.0, 0.0, 1.0);
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
	for (const float Level : {0.0f, 40.0f, 70.0f, 100.0f})
	{
		const FWSSprintAttributes Attributes = UniformAttributes(Level);
		const double Ceiling = ServerCeiling(Level);
		for (const uint32 Seed : {7u, 1234u, 999983u})
		{
			const FWSSprintOutcome Outcome = FWSSprintSimulation::RunTrace(
				Attributes, Seed, BestRealisticTrace(Attributes, Seed));
			TestTrue(FString::Printf(TEXT("attrs %.0f seed %u finished"), Level, Seed),
				Outcome.bFinished);
			TestTrue(FString::Printf(
					TEXT("attrs %.0f seed %u: %.3f must be >= ceiling %.3f"),
					Level, Seed, Outcome.TimeSeconds, Ceiling),
				Outcome.TimeSeconds >= Ceiling - 0.009); // server allows -0.01
			TestTrue(FString::Printf(
					TEXT("attrs %.0f seed %u: %.3f should be near ceiling %.3f"),
					Level, Seed, Outcome.TimeSeconds, Ceiling),
				Outcome.TimeSeconds <= Ceiling + 1.2);
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
	// Splits must pass the server's exact checks: 10 of them, each >= 0.75s,
	// reaction + sum within max(0.05, 1%) of the official time. We hold
	// ourselves to half the server's tolerance.
	for (const uint32 Seed : {11u, 222u, 3333u})
	{
		const FWSSprintAttributes Attributes = UniformAttributes(60.0f);
		const FWSSprintOutcome Outcome = FWSSprintSimulation::RunTrace(Attributes, Seed,
			FWSSprintSimulation::GenerateAITrace(Attributes, Seed, Seed, 170.0, 30.0, 0.8));
		TestTrue(TEXT("finished"), Outcome.bFinished);
		TestEqual(TEXT("ten splits"), Outcome.Splits.Num(), 10);

		double Sum = 0.0;
		for (const double Split : Outcome.Splits)
		{
			TestTrue(FString::Printf(TEXT("split %.3f >= server floor 0.75"), Split),
				Split >= 0.75);
			Sum += Split;
		}
		const double Recomposed = Outcome.ReactionMs / 1000.0 + Sum;
		TestTrue(FString::Printf(
				TEXT("reaction+splits (%.3f) matches time (%.3f)"),
				Recomposed, Outcome.TimeSeconds),
			FMath::Abs(Recomposed - Outcome.TimeSeconds) <= 0.025);

		TestTrue(TEXT("wind in the generated band"),
			Outcome.Wind >= -1.5 && Outcome.Wind <= 2.0);
		TestTrue(TEXT("reaction is legal"), Outcome.ReactionMs >= 100.0);
		TestTrue(TEXT("plausible time"),
			Outcome.TimeSeconds > 9.0 && Outcome.TimeSeconds < 60.0);
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
