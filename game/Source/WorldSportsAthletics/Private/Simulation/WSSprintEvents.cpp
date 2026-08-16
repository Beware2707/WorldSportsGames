#include "Simulation/WSSprintEvents.h"

namespace WSSprintEvents
{
const TArray<FWSSprintEventSpec>& All()
{
	static const TArray<FWSSprintEventSpec> Events = []
	{
		TArray<FWSSprintEventSpec> Table;

		FWSSprintEventSpec Sprint100;
		Sprint100.Code = TEXT("sprint-100m");
		Sprint100.DisplayName = TEXT("100m");
		Sprint100.DistanceMetres = 100.0;
		Sprint100.SplitCount = 10;
		Sprint100.MinSplitSeconds = 0.75;
		Sprint100.MinPlausibleSeconds = 9.0;
		Sprint100.MaxPlausibleSeconds = 60.0;
		Sprint100.CeilingAtZero = 13.5;
		Sprint100.CeilingAtHundred = 9.55;
		Sprint100.DriveEndFraction = 0.30;
		Sprint100.FatigueStartFraction = 0.85;
		Sprint100.FatigueScale = 1.0;
		Sprint100.GoverningAttributes = {
			TEXT("reaction"), TEXT("acceleration"), TEXT("max_speed"),
			TEXT("stride_efficiency"), TEXT("stamina")};
		Table.Add(Sprint100);

		// Same code path, different numbers. The 200m holds top speed far
		// longer, so its drive phase is a smaller FRACTION of the race and
		// fatigue bites harder over the distance.
		FWSSprintEventSpec Sprint200;
		Sprint200.Code = TEXT("sprint-200m");
		Sprint200.DisplayName = TEXT("200m");
		Sprint200.DistanceMetres = 200.0;
		Sprint200.SplitCount = 10;          // 20m segments
		Sprint200.MinSplitSeconds = 1.55;
		Sprint200.MinPlausibleSeconds = 19.0;
		Sprint200.MaxPlausibleSeconds = 120.0;
		Sprint200.CeilingAtZero = 28.0;
		Sprint200.CeilingAtHundred = 19.30;
		Sprint200.DriveEndFraction = 0.15;  // 30m, as in the 100m
		Sprint200.FatigueStartFraction = 0.70;
		Sprint200.FatigueScale = 2.4;
		Sprint200.GoverningAttributes = {
			TEXT("reaction"), TEXT("acceleration"), TEXT("max_speed"),
			TEXT("stride_efficiency"), TEXT("stamina")};
		Table.Add(Sprint200);

		// The one-lap event, where RECOVERY finally governs something —
		// matching the server's governing set for it.
		FWSSprintEventSpec Sprint400;
		Sprint400.Code = TEXT("sprint-400m");
		Sprint400.DisplayName = TEXT("400m");
		Sprint400.DistanceMetres = 400.0;
		Sprint400.SplitCount = 8;           // 50m segments
		Sprint400.MinSplitSeconds = 4.0;
		Sprint400.MinPlausibleSeconds = 42.5;
		Sprint400.MaxPlausibleSeconds = 240.0;
		Sprint400.CeilingAtZero = 64.0;
		Sprint400.CeilingAtHundred = 43.20;
		Sprint400.DriveEndFraction = 0.075; // 30m
		Sprint400.FatigueStartFraction = 0.55;
		Sprint400.TopSpeedCurve = 1.27;
		Sprint400.TopSpeedScale = 0.940;
		Sprint400.FatigueScale = 2.6;
		Sprint400.FatigueDepthScale = 1.254;
		Sprint400.FatigueStaminaSpread = 0.212;
		Sprint400.GoverningAttributes = {
			TEXT("reaction"), TEXT("acceleration"), TEXT("max_speed"),
			TEXT("stamina"), TEXT("recovery")};
		Table.Add(Sprint400);

		return Table;
	}();
	return Events;
}

const FWSSprintEventSpec& Find(const FString& Code)
{
	for (const FWSSprintEventSpec& Spec : All())
	{
		if (Spec.Code == Code)
		{
			return Spec;
		}
	}
	// An unknown code is a contract break, not a reason to invent an event.
	return All()[0];
}
}
