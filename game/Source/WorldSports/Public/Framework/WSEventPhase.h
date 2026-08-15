#pragma once

#include "CoreMinimal.h"

#include "WSEventPhase.generated.h"

/** The sport-agnostic event lifecycle (architecture §5). Every sport maps
 * its own phases onto these; camera, HUD and audio subscribe to changes. */
UENUM(BlueprintType)
enum class EWSEventPhase : uint8
{
	Load,       // level streaming in
	Present,    // intro presentation, athlete introductions
	Ready,      // waiting for the start (100m: Ready/Set live here)
	Active,     // competition running
	Finishing,  // outcome determined, finish presentation playing
	Result,     // local result on screen
	Submit,     // awaiting server validation
	Reward      // authoritative XP/PB/rank shown
};
