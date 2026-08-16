#include "Misc/AutomationTest.h"
#include "Race/WSSprintTrack.h"
#include "Simulation/WSPaceSimulation.h"
#include "Simulation/WSSprintEvents.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSTrackCoversEveryEventTest,
	"WorldSports.Race.TrackCoversEveryEvent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSTrackCoversEveryEventTest::RunTest(const FString&)
{
	// The straight is built once at a fixed length, so that length has to
	// cover the longest event in ANY table. This has now been wrong twice:
	// a 100m track when the 400m arrived, and a 400m track when the 800m
	// did. Both times the runner left the world and the race carried on in
	// black nothing. Adding an event without lengthening the track fails
	// here rather than on a player's screen.
	double Longest = 0.0;
	FString LongestCode;
	for (const FWSSprintEventSpec& Spec : WSSprintEvents::All())
	{
		if (Spec.DistanceMetres > Longest)
		{
			Longest = Spec.DistanceMetres;
			LongestCode = Spec.Code;
		}
	}
	for (const FWSPaceEventSpec& Spec : WSPaceEvents::All())
	{
		if (Spec.DistanceMetres > Longest)
		{
			Longest = Spec.DistanceMetres;
			LongestCode = Spec.Code;
		}
	}

	TestTrue(FString::Printf(
			TEXT("the track (%.0fm) must cover the longest event, %s (%.0fm)"),
			AWSSprintTrack::MaxTrackLengthCm / 100.0, *LongestCode, Longest),
		AWSSprintTrack::MaxTrackLengthCm >= Longest * 100.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
