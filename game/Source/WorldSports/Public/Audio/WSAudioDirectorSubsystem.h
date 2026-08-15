#pragma once

#include "CoreMinimal.h"
#include "Framework/WSEventPhase.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WSAudioDirectorSubsystem.generated.h"

/** Central music/crowd/announcer state. Levels feed it phase changes; sound
 * assets bind in Blueprint (audio content is a Phase 2 deliverable — this is
 * the contract they bind to). */
UCLASS()
class WORLDSPORTS_API UWSAudioDirectorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "WorldSports|Audio")
	void NotifyPhase(EWSEventPhase Phase);

	UFUNCTION(BlueprintPure, Category = "WorldSports|Audio")
	EWSEventPhase GetCurrentPhase() const { return CurrentPhase; }

	/** 0..1 crowd excitement, decays toward a floor; spikes on big moments. */
	UFUNCTION(BlueprintCallable, Category = "WorldSports|Audio")
	void AddCrowdExcitement(float Amount);

	UFUNCTION(BlueprintPure, Category = "WorldSports|Audio")
	float GetCrowdExcitement() const { return CrowdExcitement; }

private:
	EWSEventPhase CurrentPhase = EWSEventPhase::Load;
	float CrowdExcitement = 0.2f;
};
