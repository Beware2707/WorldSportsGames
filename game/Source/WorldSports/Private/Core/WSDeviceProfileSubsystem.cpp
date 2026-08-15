#include "Core/WSDeviceProfileSubsystem.h"

#include "Core/WSLog.h"
#include "HAL/PlatformMemory.h"
#include "Scalability.h"

void UWSDeviceProfileSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	const float PhysicalGB =
		static_cast<float>(Stats.TotalPhysical) / (1024.0f * 1024.0f * 1024.0f);
	ApplyTier(DetectTierFromMemoryGB(PhysicalGB));
}

EWSDeviceTier UWSDeviceProfileSubsystem::DetectTierFromMemoryGB(float PhysicalGB)
{
	// RAM is the coarse but honest proxy available everywhere. Per-GPU
	// refinement belongs in Android device profiles, not code.
	if (PhysicalGB < 3.5f)
	{
		return EWSDeviceTier::Low;
	}
	if (PhysicalGB < 6.5f)
	{
		return EWSDeviceTier::Mid;
	}
	return EWSDeviceTier::High;
}

void UWSDeviceProfileSubsystem::OverrideTier(EWSDeviceTier NewTier)
{
	ApplyTier(NewTier);
}

void UWSDeviceProfileSubsystem::ApplyTier(EWSDeviceTier NewTier)
{
	Tier = NewTier;
	Scalability::FQualityLevels Levels;
	Levels.SetFromSingleQualityLevel(static_cast<int32>(NewTier));
	Scalability::SetQualityLevels(Levels);
	UE_LOG(LogWorldSports, Log, TEXT("Device tier applied: %d"), static_cast<int32>(NewTier));
}
