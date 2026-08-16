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
 */
namespace WSSprintAudio
{
/** Sharp broadband crack — the starter's pistol. */
WORLDSPORTSATHLETICS_API USoundWaveProcedural* MakeGunshot();

/** Short tone; used for the "on your marks" / "set" calls. */
WORLDSPORTSATHLETICS_API USoundWaveProcedural* MakeTone(
	float FrequencyHz, float DurationSeconds, float Volume = 0.35f);

/** Soft click for a stride landing on the beat. */
WORLDSPORTSATHLETICS_API USoundWaveProcedural* MakeFootfall();

/** Rising two-note flourish for crossing the line. */
WORLDSPORTSATHLETICS_API USoundWaveProcedural* MakeFinishChime();

/** Play a generated cue 2D. Safe to call with no audio device. */
WORLDSPORTSATHLETICS_API void Play(UWorld* World, USoundWaveProcedural* Sound,
	float VolumeMultiplier = 1.0f);
}
