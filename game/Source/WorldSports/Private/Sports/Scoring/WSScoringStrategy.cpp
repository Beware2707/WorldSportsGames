#include "Sports/Scoring/WSScoringStrategy.h"

const UWSScoringStrategy* UWSScoringStrategy::Resolve(EWSValueKind Kind)
{
	switch (Kind)
	{
	case EWSValueKind::Time:
		return GetDefault<UWSTimeScoring>();
	case EWSValueKind::Distance:
		return GetDefault<UWSDistanceScoring>();
	case EWSValueKind::Points:
		return GetDefault<UWSPointsScoring>();
	default:
		checkNoEntry();
		return GetDefault<UWSTimeScoring>();
	}
}

FText UWSTimeScoring::FormatValue(double Seconds) const
{
	if (!FMath::IsFinite(Seconds) || Seconds < 0.0)
	{
		return FText::FromString(TEXT("--"));
	}
	// Truncate, don't round: 9.599 is NOT a 9.60 — a sprinter has not run
	// the faster time until the clock actually shows it.
	const int64 Hundredths = static_cast<int64>(Seconds * 100.0);
	const int64 Hours = Hundredths / 360000;
	const int64 Minutes = (Hundredths / 6000) % 60;
	const int64 Secs = (Hundredths / 100) % 60;
	const int64 Frac = Hundredths % 100;

	if (Hours > 0)
	{
		return FText::FromString(FString::Printf(TEXT("%lld:%02lld:%02lld.%02lld"), Hours, Minutes, Secs, Frac));
	}
	if (Minutes > 0)
	{
		return FText::FromString(FString::Printf(TEXT("%lld:%02lld.%02lld"), Minutes, Secs, Frac));
	}
	return FText::FromString(FString::Printf(TEXT("%lld.%02lld"), Secs, Frac));
}

FText UWSDistanceScoring::FormatValue(double Metres) const
{
	if (!FMath::IsFinite(Metres) || Metres < 0.0)
	{
		return FText::FromString(TEXT("--"));
	}
	// Field events truncate to the centimetre below (IAAF rule): 8.999 m is 8.99 m.
	const double Truncated = FMath::Floor(Metres * 100.0) / 100.0;
	return FText::FromString(FString::Printf(TEXT("%.2f m"), Truncated));
}

FText UWSPointsScoring::FormatValue(double Points) const
{
	if (!FMath::IsFinite(Points))
	{
		return FText::FromString(TEXT("--"));
	}
	return FText::FromString(FString::Printf(TEXT("%lld"), static_cast<int64>(Points)));
}
