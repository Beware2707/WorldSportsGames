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

		// Hurdles: the same kind, plus barriers. Technique governs them on
		// the server, so it governs them here — and the takeoff window it
		// buys is the only place a hurdler's technique shows up.
		FWSSprintEventSpec Hurdles110;
		Hurdles110.Code = TEXT("hurdles-110m");
		Hurdles110.DisplayName = TEXT("110m Hurdles");
		Hurdles110.DistanceMetres = 110.0;
		Hurdles110.SplitCount = 10;          // 11m segments
		Hurdles110.MinSplitSeconds = 0.85;
		Hurdles110.MinPlausibleSeconds = 12.5;
		Hurdles110.MaxPlausibleSeconds = 90.0;
		Hurdles110.CeilingAtZero = 19.50;
		Hurdles110.CeilingAtHundred = 12.90;
		Hurdles110.DriveEndFraction = 0.125;  // 13.72m, the first barrier
		Hurdles110.FatigueStartFraction = 0.85;
		Hurdles110.FatigueScale = 1.15;
		// A hurdler cannot run at flat-sprint speed: the stride pattern
		// between barriers is fixed at three strides, which caps the top
		// end regardless of how fast the athlete is on the flat.
		Hurdles110.TopSpeedScale = 0.810;
		Hurdles110.HurdleCount = 10;
		Hurdles110.FirstHurdleMetres = 13.72; // regulation
		Hurdles110.HurdleSpacingMetres = 9.14;
		Hurdles110.GoverningAttributes = {
			TEXT("reaction"), TEXT("acceleration"), TEXT("max_speed"),
			TEXT("stride_efficiency"), TEXT("technique")};
		Table.Add(Hurdles110);

		FWSSprintEventSpec Hurdles400;
		Hurdles400.Code = TEXT("hurdles-400m");
		Hurdles400.DisplayName = TEXT("400m Hurdles");
		Hurdles400.DistanceMetres = 400.0;
		Hurdles400.SplitCount = 8;
		Hurdles400.MinSplitSeconds = 4.2;
		Hurdles400.MinPlausibleSeconds = 45.0;
		Hurdles400.MaxPlausibleSeconds = 300.0;
		Hurdles400.CeilingAtZero = 72.0;
		Hurdles400.CeilingAtHundred = 46.10;
		Hurdles400.DriveEndFraction = 0.075;
		Hurdles400.FatigueStartFraction = 0.55;
		Hurdles400.TopSpeedCurve = 1.43;
		Hurdles400.TopSpeedScale = 0.884;
		Hurdles400.FatigueScale = 2.6;
		Hurdles400.FatigueDepthScale = 1.254;
		Hurdles400.FatigueStaminaSpread = 0.345;
		Hurdles400.HurdleCount = 10;
		Hurdles400.FirstHurdleMetres = 45.0;  // regulation
		Hurdles400.HurdleSpacingMetres = 35.0;
		Hurdles400.GoverningAttributes = {
			TEXT("acceleration"), TEXT("max_speed"), TEXT("stamina"),
			TEXT("technique"), TEXT("recovery")};
		Table.Add(Hurdles400);

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
