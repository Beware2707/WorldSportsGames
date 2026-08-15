#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "WSDeviceProfileSubsystem.generated.h"

UENUM(BlueprintType)
enum class EWSDeviceTier : uint8
{
	Low,    // 30 fps target, minimal effects
	Mid,    // 30 fps target, standard effects
	High    // 60 fps target
};

/** Detects a coarse device tier and applies matching scalability. The user
 * can override in settings; the override wins and persists via CVars. */
UCLASS()
class WORLDSPORTS_API UWSDeviceProfileSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintPure, Category = "WorldSports|Device")
	EWSDeviceTier GetTier() const { return Tier; }

	UFUNCTION(BlueprintCallable, Category = "WorldSports|Device")
	void OverrideTier(EWSDeviceTier NewTier);

	static EWSDeviceTier DetectTierFromMemoryGB(float PhysicalGB);

private:
	void ApplyTier(EWSDeviceTier NewTier);

	EWSDeviceTier Tier = EWSDeviceTier::Mid;
};
