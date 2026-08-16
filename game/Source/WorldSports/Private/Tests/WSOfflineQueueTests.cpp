#include "Misc/AutomationTest.h"
#include "Online/WSOnlineSubsystem.h"
#include "Sports/Results/WSEventResult.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSOfflineQueueRoundTripTest,
	"WorldSports.Online.OfflineQueueSurvivesSerialization",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSOfflineQueueRoundTripTest::RunTest(const FString&)
{
	// The queue is the "never lose a result" guarantee; its disk round trip
	// must preserve results exactly, in order.
	TArray<FWSEventResult> Queue;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FWSEventResult Result;
		Result.EventCode = TEXT("sprint-100m");
		Result.ValueNum = 10.0 + Index * 0.1;
		Result.bHasReactionMs = true;
		Result.ReactionMs = 150.0 + Index;
		Result.Splits = {1.9, 1.0, 0.95};
		Result.RngSeed = FString::Printf(TEXT("seed-%d"), Index);
		// The idempotency key MUST survive the disk round trip — a replayed
		// submission with a fresh ref would defeat server-side dedupe.
		Result.ClientRef = FString::Printf(TEXT("ref-%d"), Index);
		Queue.Add(Result);
	}

	const TArray<FString> Serialized = UWSOnlineSubsystem::SerializeQueue(Queue);
	TestEqual(TEXT("every entry serialized"), Serialized.Num(), 3);

	const TArray<FWSEventResult> Restored = UWSOnlineSubsystem::DeserializeQueue(Serialized);
	TestEqual(TEXT("every entry restored"), Restored.Num(), 3);
	for (int32 Index = 0; Index < Restored.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("entry %d intact"), Index),
			Queue[Index].EquivalentTo(Restored[Index]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSOfflineQueueCorruptEntryTest,
	"WorldSports.Online.OfflineQueueDropsCorruptEntriesOnly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSOfflineQueueCorruptEntryTest::RunTest(const FString&)
{
	// A corrupt save file entry must not take the healthy entries with it.
	TArray<FString> Serialized;
	Serialized.Add(TEXT("{\"event\":\"sprint-100m\",\"value_num\":10.5}"));
	Serialized.Add(TEXT("this is not json"));
	Serialized.Add(TEXT("{\"value_num\":9.9}")); // missing event
	Serialized.Add(TEXT("{\"event\":\"long-jump\",\"value_num\":7.9}"));

	const TArray<FWSEventResult> Restored = UWSOnlineSubsystem::DeserializeQueue(Serialized);
	TestEqual(TEXT("only the two valid entries survive"), Restored.Num(), 2);
	if (Restored.Num() == 2)
	{
		TestEqual(TEXT("first"), Restored[0].EventCode, TEXT("sprint-100m"));
		TestEqual(TEXT("second"), Restored[1].EventCode, TEXT("long-jump"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSSubmitDispositionTest,
	"WorldSports.Online.SubmitStatusDisposition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSSubmitDispositionTest::RunTest(const FString&)
{
	using E = EWSSubmitDisposition;
	// The review found the original code dropping queued results on 401/5xx —
	// statuses where the server never recorded anything. This table is the
	// "never lose a result" guarantee in executable form.
	TestTrue(TEXT("201 definitive"), UWSOnlineSubsystem::ClassifyResultStatus(201) == E::Definitive);
	TestTrue(TEXT("401 keeps the result"), UWSOnlineSubsystem::ClassifyResultStatus(401) == E::AuthExpired);
	TestTrue(TEXT("403 keeps the result"), UWSOnlineSubsystem::ClassifyResultStatus(403) == E::AuthExpired);
	TestTrue(TEXT("429 retries later"), UWSOnlineSubsystem::ClassifyResultStatus(429) == E::Retryable);
	TestTrue(TEXT("500 retries later"), UWSOnlineSubsystem::ClassifyResultStatus(500) == E::Retryable);
	TestTrue(TEXT("502 retries later"), UWSOnlineSubsystem::ClassifyResultStatus(502) == E::Retryable);
	TestTrue(TEXT("408 retries later"), UWSOnlineSubsystem::ClassifyResultStatus(408) == E::Retryable);
	// 404 is the career endpoints' "no athlete yet" answer. Treating it as
	// fatal silently discarded every race a new player ever ran, so it now
	// creates the athlete and retries instead.
	TestTrue(TEXT("404 asks for an athlete"),
		UWSOnlineSubsystem::ClassifyResultStatus(404) == E::NeedsAthlete);
	// Only statuses proving the submission itself can never succeed may drop.
	TestTrue(TEXT("422 is fatal"), UWSOnlineSubsystem::ClassifyResultStatus(422) == E::Fatal);
	TestTrue(TEXT("400 is fatal"), UWSOnlineSubsystem::ClassifyResultStatus(400) == E::Fatal);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
