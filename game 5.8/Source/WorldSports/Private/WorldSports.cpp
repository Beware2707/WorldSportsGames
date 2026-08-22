#include "WorldSports.h"

#include "Core/WSLog.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogWorldSports);

void FWorldSportsModule::StartupModule()
{
	UE_LOG(LogWorldSports, Log, TEXT("WorldSports module started"));
}

void FWorldSportsModule::ShutdownModule()
{
}

IMPLEMENT_PRIMARY_GAME_MODULE(FWorldSportsModule, WorldSports, "WorldSports");
