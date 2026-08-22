#include "Misc/AutomationTest.h"
#include "Sports/Results/WSEventResult.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FWSEventResult MakeSprintResult()
{
	FWSEventResult Result;
	Result.EventCode = TEXT("sprint-100m");
	Result.ValueNum = 10.42;
	Result.bHasReactionMs = true;
	Result.ReactionMs = 154.0;
	Result.Splits = {1.86, 1.02, 0.95, 0.92, 0.91, 0.90, 0.91, 0.92, 0.94, 1.09};
	Result.bHasWind = true;
	Result.Wind = 0.8;
	Result.RngSeed = TEXT("seed-123");
	Result.InputDigest = TEXT("abcdef");
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSEventResultRoundTripTest,
	"WorldSports.Results.JsonRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSEventResultRoundTripTest::RunTest(const FString&)
{
	const FWSEventResult Original = MakeSprintResult();
	const TSharedPtr<FJsonObject> Json = Original.ToRequestJson();
	TestTrue(TEXT("serializes"), Json.IsValid());

	FWSEventResult Restored;
	TestTrue(TEXT("parses back"), FWSEventResult::FromRequestJson(Json, Restored));
	TestTrue(TEXT("round trip preserves everything"), Original.EquivalentTo(Restored));
	TestEqual(TEXT("wire field is snake_case"),
		Json->GetNumberField(TEXT("value_num")), 10.42);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSEventResultOptionalsTest,
	"WorldSports.Results.OptionalsOmittedNotDefaulted",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSEventResultOptionalsTest::RunTest(const FString&)
{
	FWSEventResult Result;
	Result.EventCode = TEXT("long-jump");
	Result.ValueNum = 7.85;

	const TSharedPtr<FJsonObject> Json = Result.ToRequestJson();
	TestTrue(TEXT("serializes"), Json.IsValid());
	// An absent reaction must be ABSENT on the wire. Sending 0.0 would mean
	// "reacted in 0 ms" — an impossible claim the server rightly rejects.
	TestFalse(TEXT("no reaction field"), Json->HasField(TEXT("reaction_ms")));
	TestFalse(TEXT("no wind field"), Json->HasField(TEXT("wind")));
	TestFalse(TEXT("no splits field"), Json->HasField(TEXT("splits")));
	TestFalse(TEXT("no seed field"), Json->HasField(TEXT("rng_seed")));

	FWSEventResult Restored;
	TestTrue(TEXT("parses back"), FWSEventResult::FromRequestJson(Json, Restored));
	TestFalse(TEXT("reaction still absent"), Restored.bHasReactionMs);
	TestFalse(TEXT("wind still absent"), Restored.bHasWind);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSEventResultNonFiniteTest,
	"WorldSports.Results.NonFiniteRefused",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSEventResultNonFiniteTest::RunTest(const FString&)
{
	// The backend forbids NaN/Infinity (NaN compares false to everything and
	// laundered a false start once). The client must refuse to build such a
	// request at all, not rely on the server to bounce it.
	FWSEventResult Result = MakeSprintResult();
	Result.ValueNum = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("NaN value refused"), Result.ToRequestJson().IsValid());

	Result = MakeSprintResult();
	Result.ReactionMs = std::numeric_limits<double>::infinity();
	TestFalse(TEXT("Inf reaction refused"), Result.ToRequestJson().IsValid());

	Result = MakeSprintResult();
	Result.Splits[3] = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("NaN split refused"), Result.ToRequestJson().IsValid());

	Result = MakeSprintResult();
	Result.EventCode.Empty();
	TestFalse(TEXT("empty event refused"), Result.ToRequestJson().IsValid());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
