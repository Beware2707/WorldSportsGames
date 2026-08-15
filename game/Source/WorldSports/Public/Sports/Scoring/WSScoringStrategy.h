#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "WSScoringStrategy.generated.h"

/** How an event's number is interpreted. Never assume every sport scores the
 * same way — a time, a distance and a points total order differently and
 * format differently. */
UENUM(BlueprintType)
enum class EWSValueKind : uint8
{
	Time,      // seconds; lower is better
	Distance,  // metres; higher is better
	Points     // score; higher is better
};

/**
 * Display formatting and comparison for one value kind. Stateless — use the
 * CDO via Resolve(). This formats what the SERVER measured; the
 * authoritative value_text still comes from the backend, and this exists for
 * live in-race display where no server answer exists yet.
 */
UCLASS(Abstract)
class WORLDSPORTS_API UWSScoringStrategy : public UObject
{
	GENERATED_BODY()

public:
	virtual FText FormatValue(double Value) const PURE_VIRTUAL(UWSScoringStrategy::FormatValue, return FText::GetEmpty(););
	virtual bool IsBetter(double Candidate, double Incumbent) const PURE_VIRTUAL(UWSScoringStrategy::IsBetter, return false;);
	virtual bool LowerIsBetter() const PURE_VIRTUAL(UWSScoringStrategy::LowerIsBetter, return true;);

	static const UWSScoringStrategy* Resolve(EWSValueKind Kind);
};

UCLASS()
class WORLDSPORTS_API UWSTimeScoring : public UWSScoringStrategy
{
	GENERATED_BODY()

public:
	// "9.58" under a minute, "1:23.45" above, "1:02:03.45" above an hour.
	virtual FText FormatValue(double Seconds) const override;
	virtual bool IsBetter(double Candidate, double Incumbent) const override { return Candidate < Incumbent; }
	virtual bool LowerIsBetter() const override { return true; }
};

UCLASS()
class WORLDSPORTS_API UWSDistanceScoring : public UWSScoringStrategy
{
	GENERATED_BODY()

public:
	virtual FText FormatValue(double Metres) const override;
	virtual bool IsBetter(double Candidate, double Incumbent) const override { return Candidate > Incumbent; }
	virtual bool LowerIsBetter() const override { return false; }
};

UCLASS()
class WORLDSPORTS_API UWSPointsScoring : public UWSScoringStrategy
{
	GENERATED_BODY()

public:
	virtual FText FormatValue(double Points) const override;
	virtual bool IsBetter(double Candidate, double Incumbent) const override { return Candidate > Incumbent; }
	virtual bool LowerIsBetter() const override { return false; }
};
