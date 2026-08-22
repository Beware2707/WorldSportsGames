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
const TCHAR* AnonSlotName = TEXT("WSCareerSave_anon");
const TCHAR* SavePath = TEXT("/api/v1/career/save");
// Mirror of the backend's _MAX_JSON_BYTES: an oversize payload would 422 on
// every push forever, so it must be refused at the door.
constexpr int32 MaxPayloadBytes = 8192;

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

	LoadSlot(AnonSlotName);

	if (UWSOnlineSubsystem* OnlineSubsystem = Online())
	{
		OnlineSubsystem->OnAuthChanged.AddDynamic(this, &ThisClass::HandleAuthChanged);
	}
}

void UWSSaveSubsystem::LoadSlot(const FString& SlotName)
{
	ActiveSlotName = SlotName;
	LocalSave = nullptr;
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		LocalSave = Cast<UWSLocalSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	}
	if (!LocalSave)
	{
		LocalSave = Cast<UWSLocalSave>(
			UGameplayStatics::CreateSaveGameObject(UWSLocalSave::StaticClass()));
	}
	++EditGeneration; // invalidate any in-flight pull/push against the old slot
}

void UWSSaveSubsystem::HandleAuthChanged(bool bSignedIn)
{
	UWSOnlineSubsystem* OnlineSubsystem = Online();
	if (bSignedIn && OnlineSubsystem)
	{
		// Each account gets its own slot. Signing in must never sync the
		// anonymous slot or a previous account's leftovers.
		const FString UserSlot = FString::Printf(
			TEXT("WSCareerSave_u%d"), OnlineSubsystem->GetSignedInUser().id);
		if (UserSlot != ActiveSlotName)
		{
			LoadSlot(UserSlot);
		}
		CloudPull();
	}
	// Sign-out keeps the current slot loaded read-only; sync is gated on
	// IsSignedIn, and the next sign-in switches slots.
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
	if (FTCHARToUTF8(*PayloadJson).Length() > MaxPayloadBytes)
	{
		UE_LOG(LogWorldSports, Error,
			TEXT("Refusing save payload over the server's %d-byte cap"), MaxPayloadBytes);
		return false;
	}
	LocalSave->PayloadJson = PayloadJson;
	LocalSave->bDirty = true;
	++EditGeneration;
	return PersistLocal();
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

	const uint64 GenerationAtStart = EditGeneration;
	OnlineSubsystem->Request(TEXT("GET"), SavePath, nullptr,
		[this, Callback, GenerationAtStart](const FWSHttpResult& Result)
		{
			if (EditGeneration != GenerationAtStart)
			{
				// Someone saved (or the slot changed) while the GET was in
				// flight; adopting this response would overwrite them.
				CloudPull(Callback);
				return;
			}
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
		// A corrupt local payload must not wedge sync forever: surrender the
		// local state and recover the server's. (Corruption already lost the
		// local data; refusing to sync would just add a second loss.)
		UE_LOG(LogWorldSports, Error,
			TEXT("Local save payload corrupt; recovering from the server"));
		LocalSave->PayloadJson = TEXT("{}");
		LocalSave->bDirty = false;
		++EditGeneration;
		PersistLocal();
		CloudPull(MoveTemp(Callback));
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("payload"), Payload);
	Body->SetNumberField(TEXT("base_version"), LocalSave->SyncedVersion);

	const uint64 GenerationAtStart = EditGeneration;
	OnlineSubsystem->Request(TEXT("PUT"), SavePath, Body,
		[this, Callback, bRetryOnConflict, GenerationAtStart](const FWSHttpResult& Result)
		{
			const bool bEditedMidFlight = EditGeneration != GenerationAtStart;

			if (Result.IsSuccess() && Result.Json.IsValid())
			{
				int32 NewVersion = LocalSave->SyncedVersion;
				Result.Json->TryGetNumberField(TEXT("version"), NewVersion);
				LocalSave->SyncedVersion = NewVersion;
				if (bEditedMidFlight)
				{
					// The server has the snapshot; the newer local edits are
					// still pending. Clearing bDirty here would strand them.
					PersistLocal();
					PushInternal(Callback, /*bRetryOnConflict=*/true);
					return;
				}
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
				if (bEditedMidFlight)
				{
					// The merge was computed from a stale snapshot; adopting
					// it would destroy the newer edit. Re-push fresh: the
					// server will 409 again with a merge of the CURRENT state.
					PushInternal(Callback, bRetryOnConflict);
					return;
				}
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
					++EditGeneration;
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

bool UWSSaveSubsystem::PersistLocal()
{
	if (!UGameplayStatics::SaveGameToSlot(LocalSave, ActiveSlotName, 0))
	{
		UE_LOG(LogWorldSports, Error, TEXT("Could not write local save slot %s"), *ActiveSlotName);
		return false;
	}
	return true;
}

UWSOnlineSubsystem* UWSSaveSubsystem::Online() const
{
	return GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWSOnlineSubsystem>()
		: nullptr;
}
