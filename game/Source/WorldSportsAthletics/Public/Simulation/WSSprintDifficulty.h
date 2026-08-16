#pragma once

#include "CoreMinimal.h"
#include "Simulation/WSSprintSimulation.h"

/**
 * The five AI difficulty tiers. These shape the AI's INPUT quality —
 * reaction distribution, rhythm consistency, attribute level — never its
 * output time. Times emerge from the shared simulation.
 */
struct WORLDSPORTSATHLETICS_API FWSSprintDifficultyLevel
{
	const TCHAR* Name;
	double ReactionMeanMs;
	double ReactionSpreadMs;
	double Consistency;      // 0..1 rhythm quality
	float AttributeLevel;    // uniform across the sprint attributes

	FWSSprintAttributes MakeAttributes() const
	{
		FWSSprintAttributes Attributes;
		Attributes.Reaction = AttributeLevel;
		Attributes.Acceleration = AttributeLevel;
		Attributes.MaxSpeed = AttributeLevel;
		Attributes.StrideEfficiency = AttributeLevel;
		Attributes.Stamina = AttributeLevel;
		Attributes.Recovery = AttributeLevel;
		Attributes.Technique = AttributeLevel;
		return Attributes;
	}
};

namespace WSSprintDifficulty
{
inline const TArray<FWSSprintDifficultyLevel>& Levels()
{
	static const TArray<FWSSprintDifficultyLevel> All = {
		{TEXT("Rookie"),   245.0, 60.0, 0.30, 30.0f},
		{TEXT("Amateur"),  215.0, 50.0, 0.50, 40.0f},
		{TEXT("Pro"),      185.0, 40.0, 0.68, 55.0f},
		{TEXT("Elite"),    165.0, 30.0, 0.82, 70.0f},
		{TEXT("Champion"), 150.0, 20.0, 0.93, 85.0f},
	};
	return All;
}
}
