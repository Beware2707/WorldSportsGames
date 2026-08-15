#pragma once

#include "CoreMinimal.h"
#include "Framework/WSEventPhase.h"
#include "GameFramework/Actor.h"

#include "WSCameraDirector.generated.h"

class ACameraActor;

/** Maps lifecycle phases to named camera rigs placed in the level. Sport
 * levels tag their cameras; the director cuts between them on phase change.
 * Sequenced/cinematic shots come later — this is the runtime backbone. */
UCLASS()
class WORLDSPORTS_API AWSCameraDirector : public AActor
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	/** Blend to the rig registered for this phase, if any. */
	UFUNCTION(BlueprintCallable, Category = "WorldSports|Camera")
	void CutToPhase(EWSEventPhase Phase);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports|Camera")
	TMap<EWSEventPhase, TObjectPtr<ACameraActor>> PhaseCameras;

	UPROPERTY(EditAnywhere, Category = "WorldSports|Camera", meta = (ClampMin = "0"))
	float BlendSeconds = 0.75f;

private:
	UFUNCTION()
	void HandlePhaseChanged(EWSEventPhase OldPhase, EWSEventPhase NewPhase);
};
