#include "Misc/AutomationTest.h"
#include "Race/WSSprintAudio.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWSSprintAudioCueTest,
	"WorldSports.Audio.CuesAreReplayablePcm",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FWSSprintAudioCueTest::RunTest(const FString&)
{
	// A cue is PCM BYTES, not a sound object. Holding a single
	// USoundWaveProcedural made every cue silent after one playback — its
	// queue is drained by the audio render thread — which silenced the start
	// gun from the second race onwards. Bytes can be re-queued forever.
	const FWSSoundCue Gun = WSSprintAudio::MakeGunshot();
	TestTrue(TEXT("gun has audio"), Gun.IsValid());
	TestTrue(TEXT("gun is short and sharp"),
		Gun.DurationSeconds > 0.1f && Gun.DurationSeconds < 0.6f);

	// The bytes survive being read: playing does not consume the cue.
	const TArray<uint8> Before = Gun.Pcm;
	WSSprintAudio::Play(nullptr, Gun); // no world: safely does nothing
	TestEqual(TEXT("playing does not consume the cue"), Gun.Pcm.Num(), Before.Num());
	TestTrue(TEXT("bytes unchanged"), Gun.Pcm == Before);

	// 16-bit mono: an odd byte count would mean a truncated sample.
	TestEqual(TEXT("whole samples"), Gun.Pcm.Num() % 2, 0);

	// The gun must actually be loud enough to react to — a cue of silence
	// would pass every structural check and still be useless.
	const int16* Samples = reinterpret_cast<const int16*>(Gun.Pcm.GetData());
	int32 Peak = 0;
	for (int32 Index = 0; Index < Gun.Pcm.Num() / 2; ++Index)
	{
		Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(Samples[Index])));
	}
	TestTrue(FString::Printf(TEXT("gun peaks audibly (%d)"), Peak), Peak > 8000);

	for (const FWSSoundCue& Cue : {WSSprintAudio::MakeFootfall(),
			WSSprintAudio::MakeFinishChime(), WSSprintAudio::MakeTone(440.0f, 0.3f)})
	{
		TestTrue(TEXT("cue has audio"), Cue.IsValid());
		TestEqual(TEXT("whole samples"), Cue.Pcm.Num() % 2, 0);
		TestTrue(TEXT("finite duration"), Cue.DurationSeconds > 0.0f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
