#include "Progression/WSProgressionSubsystem.h"

#include "Online/WSOnlineSubsystem.h"

void UWSProgressionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UWSOnlineSubsystem>();
	if (UWSOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>())
	{
		Online->OnResultSubmitted.AddDynamic(this, &ThisClass::HandleResultSubmitted);
	}
}

void UWSProgressionSubsystem::HandleResultSubmitted(const FWSResultResponse& Response, bool bAccepted)
{
	LastResult = Response;
	if (bAccepted)
	{
		// The response carries the authoritative totals — adopt, don't add.
		TotalXp = Response.total_xp;
		CareerStage = Response.career_stage;
	}
	OnProgressionChanged.Broadcast();
}
