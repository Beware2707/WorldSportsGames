#include "Misc/AutomationTest.h"
#include "Simulation/WSJumpSimulation.h"

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
	Attributes.Recovery = Level;
	Attributes.Technique = Level;
	return Attributes;
}

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
	const FWSSprintAttributes Attributes = UniformAttributes(60.0f);
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
		for (const float Level : {0.0f, 25.0f, 40.0f, 55.0f, 70.0f, 85.0f, 100.0f})
		{
			const FWSSprintAttributes Attributes = UniformAttributes(Level);
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
			TestTrue(FString::Printf(
					TEXT("%s attrs %.0f: best %.2f m should approach the ceiling %.2f m"),
					*Spec.Code, Level, BestMark, Ceiling),
				BestMark >= Ceiling - 0.75);
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
	const FWSSprintAttributes Attributes = UniformAttributes(70.0f);

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
	const FWSSprintAttributes Attributes = UniformAttributes(70.0f);

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
	const FWSSprintAttributes Modest = UniformAttributes(55.0f);
	const FWSSprintAttributes Strong = UniformAttributes(80.0f);

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

#endif // WITH_DEV_AUTOMATION_TESTS
