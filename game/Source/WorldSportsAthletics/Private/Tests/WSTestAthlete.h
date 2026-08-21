#pragma once

#include "Simulation/WSSprintSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Shared test fixture: an athlete with every attribute at one level.
 *
 * Shared rather than copied per test file because UE's adaptive unity build
 * concatenates translation units, and an anonymous-namespace helper with
 * the same signature in two files then collides. Adding the throws was
 * enough to regroup the unity files and break the build.
 */
namespace WSTestAthlete
{
inline FWSSprintAttributes Uniform(float Level)
{
	FWSSprintAttributes Attributes;
	Attributes.Reaction = Level;
	Attributes.Acceleration = Level;
	Attributes.MaxSpeed = Level;
	Attributes.StrideEfficiency = Level;
	Attributes.Stamina = Level;
	// Recovery too: without it an event whose governing set includes it
	// sits below the level being asserted, and every margin on that event
	// is measured against a ceiling the athlete was never racing to.
	Attributes.Recovery = Level;
	Attributes.Technique = Level;
	return Attributes;
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
