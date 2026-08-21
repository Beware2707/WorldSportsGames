#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "WSSprintTrack.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class UStaticMeshComponent;

/**
 * Procedural 8-lane straight built from engine primitives — the honest
 * placeholder until authored stadium art exists. Geometry is generated in
 * code so the whole slice runs from a repository checkout with zero binary
 * content, and so lane/track dimensions live in one reviewable place.
 *
 * Coordinates: X+ is the running direction (1 unreal unit = 1 cm), Y spans
 * the lanes.
 *
 * The straight is built once at the LONGEST distance any event runs, and
 * the finish line and distance markers move to suit the race. Building it
 * at 100m was fine while the 100m was the only event; on a 400m the runner
 * simply ran off the end of the world into black nothing, which is what
 * the first emulator run showed.
 */
UCLASS()
class WORLDSPORTSATHLETICS_API AWSSprintTrack : public AActor
{
	GENERATED_BODY()

public:
	AWSSprintTrack();

	static constexpr int32 LaneCount = 8;
	static constexpr float LaneWidthCm = 122.0f;   // regulation 1.22 m
	/**
	 * The straight is built this long regardless of the event, and it must
	 * cover the LONGEST event in any table — not the longest that existed
	 * when the constant was written. Set to 400m before middle distance
	 * arrived, an 800m ran off the end into black nothing, which is the
	 * same defect the 400m had against a 100m track.
	 * WSSprintTrackTests asserts this against both event tables.
	 */
	static constexpr float MaxTrackLengthCm = 150000.0f; // 1500 m
	static constexpr float ApronCm = 900.0f;       // run-out after the line
	/** How many distance markers exist; the race uses as many as it needs. */
	static constexpr int32 MaxDistanceMarks = 10;
	/** Barriers per lane, for the hurdles events. */
	static constexpr int32 MaxHurdles = 10;

	/**
	 * Move the finish line and the distance markers to suit this event.
	 * Markers sit on the SPLIT boundaries, so what a player sees on the
	 * infield is exactly where their splits were taken.
	 */
	void SetRaceDistance(float DistanceMetres, int32 SplitCount);

	/**
	 * Stand the barriers up for a hurdles race, or take them all away for a
	 * flat one.
	 *
	 * A hurdles race with no visible hurdles is not a hurdles race: the
	 * simulation was judging takeoffs against barriers the player could not
	 * see, leaving a numeric prompt as the only cue that anything was there.
	 */
	void SetHurdles(int32 Count, float FirstMetres, float SpacingMetres);

	/**
	 * Dress the straight as a jumping runway: a takeoff board at the given
	 * distance and a sand pit beyond it. Passing 0 puts it back to a track.
	 *
	 * The board is the one thing in a long jump the player is aiming at, so
	 * it has to be visible from the runway rather than implied by a number.
	 */
	void SetJumpPit(float BoardMetres, float PitLengthMetres);

	/** Where the board is, in cm; 0 when this is not a jumping event. */
	float GetBoardX() const;

	/** How many barriers are currently standing, for tests. */
	int32 GetVisibleHurdleCount() const;

	/** Where each standing barrier is, in cm, nearest first. */
	TArray<float> GetVisibleHurdlePositions() const;

	/** Where the finish line currently sits, in cm. Test-visible so the
	 * track's length can be asserted rather than eyeballed. */
	float GetFinishLineX() const;

	/** Infield marker positions in cm, in order; hidden ones excluded. */
	TArray<float> GetVisibleMarkPositions() const;

	/** World Y centre of a lane (0-based). */
	static float LaneCenterY(int32 LaneIndex)
	{
		return (LaneIndex - (LaneCount - 1) * 0.5f) * LaneWidthCm;
	}

private:
	UStaticMeshComponent* MakeBox(const TCHAR* Name, const FVector& Center,
		const FVector& Extent, const FLinearColor& Color);

	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> SunLight;

	UPROPERTY()
	TObjectPtr<USkyLightComponent> SkyLight;

	/** Movable, because they move per event. Movable children on a static
	 * parent are legal; the reverse is what the engine drops. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> FinishLine;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> DistanceMarks;

	/** MaxHurdles barriers per lane, laid out row-major (barrier, lane). */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Hurdles;

	int32 StandingHurdles = 0;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> TakeoffBoard;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SandPit;
};
