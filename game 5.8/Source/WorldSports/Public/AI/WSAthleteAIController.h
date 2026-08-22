#pragma once

#include "AIController.h"
#include "CoreMinimal.h"

#include "WSAthleteAIController.generated.h"

class UWSDifficultyProfile;

/** Drives an AWSAthleteCharacter through ApplyEffort — the same channel the
 * player uses. AI must never write times or positions directly. */
UCLASS()
class WORLDSPORTS_API AWSAthleteAIController : public AAIController
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSports|AI")
	TObjectPtr<UWSDifficultyProfile> Difficulty;
};
