#pragma once

#include "CoreMinimal.h"

class USoundWaveProcedural;
class UWorld;

/**
 * Procedurally synthesised race audio.
 *
 * There is no licensed audio content in the repository, and the start gun
 * is not decoration — it is the signal a player's reaction time is measured
 * against, and GAME_DESIGN.md §7 requires timing cues that do not depend on
 * sight. So the cues are generated as PCM in code: honest, tiny, and
 * reviewable in a diff, to be replaced by authored sound when it exists.
 *
 * A cue is stored as its PCM BYTES, not as a sound object. USoundWaveProcedural
 * holds a consumable FIFO — the audio render thread pops the queued bytes and
 * then returns silence — so a single shared wave plays exactly once and every
 * later race is silent. Each playback therefore gets a freshly queued wave.
 */
struct WORLDSPORTSATHLETICS_API FWSSoundCue
{
	TArray<uint8> Pcm;
	float DurationSeconds = 0.0f;

	bool IsValid() const { return Pcm.Num() > 0; }
};

namespace WSSprintAudio
{
/** Sharp broadband crack — the starter's pistol. */
WORLDSPORTSATHLETICS_API FWSSoundCue MakeGunshot();

/** Short tone; used for the "on your marks" / "set" calls. */
WORLDSPORTSATHLETICS_API FWSSoundCue MakeTone(
	float FrequencyHz, float DurationSeconds, float Volume = 0.35f);

/** Soft click for a stride landing on the beat. */
WORLDSPORTSATHLETICS_API FWSSoundCue MakeFootfall();

/** Rising two-note flourish for crossing the line. */
WORLDSPORTSATHLETICS_API FWSSoundCue MakeFinishChime();

/** Play a cue 2D. Safe with no audio device — a headless race is silent. */
WORLDSPORTSATHLETICS_API void Play(UWorld* World, const FWSSoundCue& Cue,
	float VolumeMultiplier = 1.0f);
}
