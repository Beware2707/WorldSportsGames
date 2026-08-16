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
 * Coordinates: X+ is the running direction (1 unreal unit = 1 cm;
 * 0..10000 = 100 m), Y spans the lanes.
 */
UCLASS()
class WORLDSPORTSATHLETICS_API AWSSprintTrack : public AActor
{
	GENERATED_BODY()

public:
	AWSSprintTrack();

	static constexpr int32 LaneCount = 8;
	static constexpr float LaneWidthCm = 122.0f;   // regulation 1.22 m
	static constexpr float TrackLengthCm = 10000.0f;
	static constexpr float ApronCm = 900.0f;       // run-out after the line

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

	UPROPERTY()
	TObjectPtr<class USkyAtmosphereComponent> Atmosphere;
};
