#include "Misc/AutomationTest.h"
#include "Sports/Scoring/WSScoringStrategy.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSTimeFormattingTest,
	"WorldSports.Scoring.TimeFormatting",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSTimeFormattingTest::RunTest(const FString&)
{
	const UWSScoringStrategy* Time = UWSScoringStrategy::Resolve(EWSValueKind::Time);
	TestEqual(TEXT("sprint"), Time->FormatValue(9.58).ToString(), TEXT("9.58"));
	// Truncation, not rounding: 9.599 has not broken 9.60.
	TestEqual(TEXT("truncates"), Time->FormatValue(9.599).ToString(), TEXT("9.59"));
	TestEqual(TEXT("middle distance"), Time->FormatValue(83.45).ToString(), TEXT("1:23.45"));
	TestEqual(TEXT("marathon"), Time->FormatValue(2.0 * 3600 + 62.03).ToString(), TEXT("2:01:02.03"));
	TestEqual(TEXT("invalid shows placeholder"), Time->FormatValue(-1.0).ToString(), TEXT("--"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSScoringDirectionTest,
	"WorldSports.Scoring.DirectionPerKind",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSScoringDirectionTest::RunTest(const FString&)
{
	// Never assume every sport scores the same way: a faster time wins, a
	// LONGER jump wins. One flipped comparison corrupts every ranking.
	const UWSScoringStrategy* Time = UWSScoringStrategy::Resolve(EWSValueKind::Time);
	TestTrue(TEXT("9.5s beats 10s"), Time->IsBetter(9.5, 10.0));
	TestFalse(TEXT("10s does not beat 9.5s"), Time->IsBetter(10.0, 9.5));
	TestTrue(TEXT("time: lower is better"), Time->LowerIsBetter());

	const UWSScoringStrategy* Distance = UWSScoringStrategy::Resolve(EWSValueKind::Distance);
	TestTrue(TEXT("8m beats 7m"), Distance->IsBetter(8.0, 7.0));
	TestFalse(TEXT("distance: lower is not better"), Distance->LowerIsBetter());
	TestEqual(TEXT("field truncation"), Distance->FormatValue(8.999).ToString(), TEXT("8.99 m"));

	const UWSScoringStrategy* Points = UWSScoringStrategy::Resolve(EWSValueKind::Points);
	TestTrue(TEXT("9000 beats 8500"), Points->IsBetter(9000.0, 8500.0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
