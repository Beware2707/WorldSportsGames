#include "Framework/WSAthleteCharacter.h"

#include "Components/SkeletalMeshComponent.h"

AWSAthleteCharacter::AWSAthleteCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	OutfitMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("OutfitMesh"));
	OutfitMesh->SetupAttachment(GetMesh());
	OutfitMesh->SetLeaderPoseComponent(GetMesh());

	HairMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HairMesh"));
	HairMesh->SetupAttachment(GetMesh());
	HairMesh->SetLeaderPoseComponent(GetMesh());
}

void AWSAthleteCharacter::ApplyEffort(float NormalizedEffort)
{
	CurrentEffort = FMath::Clamp(NormalizedEffort, 0.0f, 1.0f);
}
