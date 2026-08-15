#include "Save/WSSaveSubsystem.h"

#include "Core/WSLog.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "Online/WSOnlineSubsystem.h"
#include "Save/WSLocalSave.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
const TCHAR* SaveSlotName = TEXT("WSCareerSave");
const TCHAR* SavePath = TEXT("/api/v1/career/save");

TSharedPtr<FJsonObject> ParseObject(const FString& Json)
{
	TSharedPtr<FJsonObject> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Parsed))
	{
		return nullptr;
	}
	return Parsed;
}

FString WriteObject(const TSharedPtr<FJsonObject>& Json)
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
	return Out;
}
}

void UWSSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The online subsystem owns connectivity; sync happens after sign-in.
	Collection.InitializeDependency<UWSOnlineSubsystem>();

	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		LocalSave = Cast<UWSLocalSave>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	}
	if (!LocalSave)
	{
		LocalSave = Cast<UWSLocalSave>(
			UGameplayStatics::CreateSaveGameObject(UWSLocalSave::StaticClass()));
	}

	if (UWSOnlineSubsystem* OnlineSubsystem = Online())
	{
		OnlineSubsystem->OnAuthChanged.AddDynamic(this, &ThisClass::HandleAuthChanged);
	}
}

void UWSSaveSubsystem::HandleAuthChanged(bool bSignedIn)
{
	if (bSignedIn)
	{
		CloudPull();
	}
}

FString UWSSaveSubsystem::GetPayloadJson() const
{
	return LocalSave ? LocalSave->PayloadJson : TEXT("{}");
}

int32 UWSSaveSubsystem::GetSyncedVersion() const
{
	return LocalSave ? LocalSave->SyncedVersion : 0;
}

bool UWSSaveSubsystem::IsDirty() const
{
	return LocalSave && LocalSave->bDirty;
}

bool UWSSaveSubsystem::SetPayloadJson(const FString& PayloadJson)
{
	if (!ParseObject(PayloadJson).IsValid())
	{
		UE_LOG(LogWorldSports, Error, TEXT("Refusing to store non-JSON save payload"));
		return false;
	}
	LocalSave->PayloadJson = PayloadJson;
	LocalSave->bDirty = true;
	PersistLocal();
	return true;
}

void UWSSaveSubsystem::CloudPull(FWSSyncCallback Callback)
{
	UWSOnlineSubsystem* OnlineSubsystem = Online();
	if (!OnlineSubsystem || !OnlineSubsystem->IsSignedIn())
	{
		if (Callback)
		{
			Callback(false, TEXT("Not signed in"));
		}
		return;
	}

	if (LocalSave->bDirty)
	{
		// Local edits exist: pushing lets the server arbitrate (200 or a 409
		// with a merge), which a plain pull cannot do.
		CloudPush(MoveTemp(Callback));
		return;
	}

	OnlineSubsystem->Request(TEXT("GET"), SavePath, nullptr,
		[this, Callback](const FWSHttpResult& Result)
		{
			if (!Result.IsSuccess() || !Result.Json.IsValid())
			{
				if (Callback)
				{
					Callback(false, Result.bTransportOk
						? FString::Printf(TEXT("Save pull failed (%d)"), Result.StatusCode)
						: Result.ErrorText);
				}
				return;
			}
			const TSharedPtr<FJsonObject>* ServerPayload = nullptr;
			int32 ServerVersion = 0;
			Result.Json->TryGetObjectField(TEXT("payload"), ServerPayload);
			Result.Json->TryGetNumberField(TEXT("version"), ServerVersion);

			LocalSave->PayloadJson =
				ServerPayload ? WriteObject(*ServerPayload) : TEXT("{}");
			LocalSave->SyncedVersion = ServerVersion;
			LocalSave->bDirty = false;
			PersistLocal();
			OnSaveSynced.Broadcast(ServerVersion);
			if (Callback)
			{
				Callback(true, FString());
			}
		});
}

void UWSSaveSubsystem::CloudPush(FWSSyncCallback Callback)
{
	PushInternal(MoveTemp(Callback), /*bRetryOnConflict=*/true);
}

void UWSSaveSubsystem::PushInternal(FWSSyncCallback Callback, bool bRetryOnConflict)
{
	UWSOnlineSubsystem* OnlineSubsystem = Online();
	if (!OnlineSubsystem || !OnlineSubsystem->IsSignedIn())
	{
		if (Callback)
		{
			Callback(false, TEXT("Not signed in"));
		}
		return;
	}

	const TSharedPtr<FJsonObject> Payload = ParseObject(LocalSave->PayloadJson);
	if (!Payload.IsValid())
	{
		if (Callback)
		{
			Callback(false, TEXT("Local save payload is corrupt"));
		}
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("payload"), Payload);
	Body->SetNumberField(TEXT("base_version"), LocalSave->SyncedVersion);

	OnlineSubsystem->Request(TEXT("PUT"), SavePath, Body,
		[this, Callback, bRetryOnConflict](const FWSHttpResult& Result)
		{
			if (Result.IsSuccess() && Result.Json.IsValid())
			{
				int32 NewVersion = LocalSave->SyncedVersion;
				Result.Json->TryGetNumberField(TEXT("version"), NewVersion);
				LocalSave->SyncedVersion = NewVersion;
				LocalSave->bDirty = false;
				PersistLocal();
				OnSaveSynced.Broadcast(NewVersion);
				if (Callback)
				{
					Callback(true, FString());
				}
				return;
			}

			if (Result.bTransportOk && Result.StatusCode == 409 && Result.Json.IsValid())
			{
				// detail: { message, server: {payload, version}, suggested_merge }
				const TSharedPtr<FJsonObject>* Detail = nullptr;
				const TSharedPtr<FJsonObject>* Server = nullptr;
				const TSharedPtr<FJsonObject>* Merge = nullptr;
				int32 ServerVersion = 0;
				if (Result.Json->TryGetObjectField(TEXT("detail"), Detail) &&
					(*Detail)->TryGetObjectField(TEXT("server"), Server) &&
					(*Detail)->TryGetObjectField(TEXT("suggested_merge"), Merge) &&
					(*Server)->TryGetNumberField(TEXT("version"), ServerVersion))
				{
					// Adopt the server-computed additive merge (numeric max,
					// list union — nothing monotonic is ever lost) and move
					// our base to the server's version.
					LocalSave->PayloadJson = WriteObject(*Merge);
					LocalSave->SyncedVersion = ServerVersion;
					LocalSave->bDirty = true;
					PersistLocal();
					if (bRetryOnConflict)
					{
						PushInternal(Callback, /*bRetryOnConflict=*/false);
						return;
					}
				}
				if (Callback)
				{
					Callback(false, TEXT("Save conflict could not be resolved"));
				}
				return;
			}

			if (Callback)
			{
				Callback(false, Result.bTransportOk
					? FString::Printf(TEXT("Save push failed (%d)"), Result.StatusCode)
					: Result.ErrorText);
			}
		});
}

void UWSSaveSubsystem::PersistLocal()
{
	if (!UGameplayStatics::SaveGameToSlot(LocalSave, SaveSlotName, 0))
	{
		UE_LOG(LogWorldSports, Error, TEXT("Could not write local save slot"));
	}
}

UWSOnlineSubsystem* UWSSaveSubsystem::Online() const
{
	return GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>()
		: nullptr;
}
