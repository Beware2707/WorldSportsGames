#include "Analytics/WSAnalyticsSubsystem.h"

#include "Core/WSLog.h"
#include "Dom/JsonObject.h"
#include "Online/WSOnlineSubsystem.h"

namespace
{
const TCHAR* AnalyticsPath = TEXT("/api/v1/analytics/events");
constexpr int32 MaxBatch = 100;      // server-side cap per request
constexpr int32 MaxBuffer = 500;     // local cap; oldest dropped beyond this
constexpr float FlushIntervalSeconds = 30.0f;
}

void UWSAnalyticsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UWSOnlineSubsystem>();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UWSAnalyticsSubsystem::TickFlush),
		FlushIntervalSeconds);
}

void UWSAnalyticsSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	Flush(); // best effort on shutdown
	Super::Deinitialize();
}

void UWSAnalyticsSubsystem::Track(const FString& Name, const TMap<FString, FString>& Properties)
{
	FPendingEvent Event;
	Event.Name = Name;
	Event.Properties = Properties;
	Event.ClientTs = FDateTime::UtcNow();
	Buffer.Add(MoveTemp(Event));

	if (Buffer.Num() > MaxBuffer)
	{
		Buffer.RemoveAt(0, Buffer.Num() - MaxBuffer);
	}
	if (Buffer.Num() >= MaxBatch)
	{
		Flush();
	}
}

bool UWSAnalyticsSubsystem::TickFlush(float)
{
	Flush();
	return true; // keep ticking
}

void UWSAnalyticsSubsystem::Flush()
{
	if (bFlushInFlight || Buffer.Num() == 0)
	{
		return;
	}
	UWSOnlineSubsystem* Online =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>() : nullptr;
	if (!Online)
	{
		return;
	}

	const int32 BatchSize = FMath::Min(Buffer.Num(), MaxBatch);
	TArray<TSharedPtr<FJsonValue>> Events;
	Events.Reserve(BatchSize);
	for (int32 Index = 0; Index < BatchSize; ++Index)
	{
		const FPendingEvent& Event = Buffer[Index];
		TSharedPtr<FJsonObject> EventJson = MakeShared<FJsonObject>();
		EventJson->SetStringField(TEXT("name"), Event.Name);
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Pair : Event.Properties)
		{
			Props->SetStringField(Pair.Key, Pair.Value);
		}
		EventJson->SetObjectField(TEXT("properties"), Props);
		EventJson->SetStringField(TEXT("client_ts"), Event.ClientTs.ToIso8601());
		Events.Add(MakeShared<FJsonValueObject>(EventJson));
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("events"), Events);

	bFlushInFlight = true;
	Online->Request(TEXT("POST"), AnalyticsPath, Body,
		[this, BatchSize](const FWSHttpResult& Result)
		{
			bFlushInFlight = false;
			if (Result.bTransportOk)
			{
				// 202 or any definitive answer: these events are spent. A 422
				// would mean a client bug; resending the same batch would just
				// fail the same way.
				Buffer.RemoveAt(0, FMath::Min(BatchSize, Buffer.Num()));
				if (Result.Json.IsValid())
				{
					int32 Accepted = 0;
					if (Result.Json->TryGetNumberField(TEXT("accepted"), Accepted) &&
						Accepted < BatchSize)
					{
						UE_LOG(LogWorldSports, Warning,
							TEXT("Analytics: %d/%d events rejected by the server"),
							BatchSize - Accepted, BatchSize);
					}
				}
			}
			// Transport failure: keep the buffer; the next tick retries.
		});
}
