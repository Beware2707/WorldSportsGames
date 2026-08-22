#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "WSAthleteCharacter.generated.h"

class USkeletalMeshComponent;

/**
 * Modular athlete pawn, driven by a player or an AI controller through the
 * SAME movement/simulation interface — an AI athlete that could bypass the
 * simulation could also bypass its limits (brief §36 forbids exactly that).
 *
 * The base body is the inherited Mesh(); outfit and hair follow it via
 * leader pose so appearance parts can swap without touching animation.
 */
UCLASS()
class WORLDSPORTS_API AWSAthleteCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AWSAthleteCharacter();

	UFUNCTION(BlueprintPure, Category = "WorldSports|Athlete")
	USkeletalMeshComponent* GetOutfitMesh() const { return OutfitMesh; }

	UFUNCTION(BlueprintPure, Category = "WorldSports|Athlete")
	USkeletalMeshComponent* GetHairMesh() const { return HairMesh; }

	/** 0..1 normalized effort input for this frame; the ONLY channel through
	 * which any controller (player or AI) drives the athlete. */
	UFUNCTION(BlueprintCallable, Category = "WorldSports|Athlete")
	virtual void ApplyEffort(float NormalizedEffort);

	UFUNCTION(BlueprintPure, Category = "WorldSports|Athlete")
	float GetCurrentEffort() const { return CurrentEffort; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorldSports|Athlete")
	TObjectPtr<USkeletalMeshComponent> OutfitMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorldSports|Athlete")
	TObjectPtr<USkeletalMeshComponent> HairMesh;

	float CurrentEffort = 0.0f;
};
