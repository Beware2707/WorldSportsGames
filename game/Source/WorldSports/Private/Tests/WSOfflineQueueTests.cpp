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

#endif // WITH_DEV_AUTOMATION_TESTS
