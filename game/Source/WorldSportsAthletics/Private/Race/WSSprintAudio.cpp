#include "Race/WSSprintAudio.h"

#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
constexpr int32 SampleRate = 22050; // plenty for cues; a quarter of the memory

/** Build a mono 16-bit sound from a per-sample generator. */
USoundWaveProcedural* MakeWave(int32 SampleCount,
	TFunctionRef<float(int32 Sample, float Time)> Generator)
{
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = static_cast<float>(SampleCount) / SampleRate;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;

	TArray<uint8> Pcm;
	Pcm.SetNumUninitialized(SampleCount * sizeof(int16));
	int16* Samples = reinterpret_cast<int16*>(Pcm.GetData());
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const float Time = static_cast<float>(Index) / SampleRate;
		const float Value = FMath::Clamp(Generator(Index, Time), -1.0f, 1.0f);
		Samples[Index] = static_cast<int16>(Value * 32000.0f);
	}
	Wave->QueueAudio(Pcm.GetData(), Pcm.Num());
	return Wave;
}

/** Exponential decay envelope — the shape of every percussive cue. */
float Decay(float Time, float RateHz)
{
	return FMath::Exp(-Time * RateHz);
}
}

namespace WSSprintAudio
{
USoundWaveProcedural* MakeGunshot()
{
	// Noise burst with a fast decay and a low thump under it: recognisable
	// as a start signal in under 40ms, which is what reaction demands.
	FRandomStream Noise(20260816);
	return MakeWave(SampleRate * 0.35f, [&Noise](int32, float Time)
	{
		const float Crack = (Noise.FRand() * 2.0f - 1.0f) * Decay(Time, 26.0f);
		const float Thump = FMath::Sin(2.0f * PI * 90.0f * Time) * Decay(Time, 11.0f) * 0.6f;
		return (Crack + Thump) * 0.9f;
	});
}

USoundWaveProcedural* MakeTone(float FrequencyHz, float DurationSeconds, float Volume)
{
	return MakeWave(static_cast<int32>(SampleRate * DurationSeconds),
		[FrequencyHz, DurationSeconds, Volume](int32, float Time)
		{
			// Soft attack and release so the tone reads as a call, not a beep.
			const float Envelope = FMath::Min(1.0f,
				FMath::Min(Time * 25.0f, (DurationSeconds - Time) * 12.0f));
			return FMath::Sin(2.0f * PI * FrequencyHz * Time) *
				FMath::Max(Envelope, 0.0f) * Volume;
		});
}

USoundWaveProcedural* MakeFootfall()
{
	FRandomStream Noise(913377);
	return MakeWave(SampleRate * 0.06f, [&Noise](int32, float Time)
	{
		return (Noise.FRand() * 2.0f - 1.0f) * Decay(Time, 90.0f) * 0.30f;
	});
}

USoundWaveProcedural* MakeFinishChime()
{
	return MakeWave(SampleRate * 0.55f, [](int32, float Time)
	{
		const float First = FMath::Sin(2.0f * PI * 660.0f * Time) * Decay(Time, 5.0f);
		const float Second = Time > 0.16f
			? FMath::Sin(2.0f * PI * 990.0f * (Time - 0.16f)) * Decay(Time - 0.16f, 5.0f)
			: 0.0f;
		return (First + Second) * 0.28f;
	});
}

void Play(UWorld* World, USoundWaveProcedural* Sound, float VolumeMultiplier)
{
	// A headless race (automation, replay validation) has no audio device;
	// the race must be identical with or without sound.
	if (!World || !Sound || !GEngine || !GEngine->UseSound())
	{
		return;
	}
	UGameplayStatics::PlaySound2D(World, Sound, VolumeMultiplier);
}
}
