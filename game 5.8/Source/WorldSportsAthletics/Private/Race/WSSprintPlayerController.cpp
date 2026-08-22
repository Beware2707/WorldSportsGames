#include "Race/WSSprintPlayerController.h"

#include "Components/InputComponent.h"
#include "Framework/WSEventPhase.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Race/WSSprintGameMode.h"
#include "Race/WSSprintRunner.h"

AWSSprintPlayerController::AWSSprintPlayerController()
{
	bShowMouseCursor = false;
	bEnableTouchEvents = true;
	bEnableClickEvents = true;
}

void AWSSprintPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SetInputMode(FInputModeGameAndUI());
}

void AWSSprintPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}
	// Raw bindings rather than Enhanced Input assets: the slice must run
	// from a source checkout with no content to author or migrate.
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this,
		&AWSSprintPlayerController::HandlePressed);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Released, this,
		&AWSSprintPlayerController::HandleReleased);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this,
		&AWSSprintPlayerController::HandlePressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this,
		&AWSSprintPlayerController::HandleReleased);
	InputComponent->BindTouch(IE_Pressed, this,
		&AWSSprintPlayerController::HandleTouchPressed);
	InputComponent->BindTouch(IE_Released, this,
		&AWSSprintPlayerController::HandleTouchReleased);
	InputComponent->BindTouch(IE_Repeat, this,
		&AWSSprintPlayerController::HandleSwipeCheck);
}

AWSSprintGameMode* AWSSprintPlayerController::Sprint() const
{
	return GetWorld() ? GetWorld()->GetAuthGameMode<AWSSprintGameMode>() : nullptr;
}

void AWSSprintPlayerController::HandlePressed()
{
	AWSSprintGameMode* GameMode = Sprint();
	if (!GameMode)
	{
		return;
	}
	if (GameMode->GetPhase() == EWSEventPhase::Ready)
	{
		GameMode->PlayerHold();
	}
	else
	{
		GameMode->PlayerTap();
	}
}

void AWSSprintPlayerController::HandleReleased()
{
	if (AWSSprintGameMode* GameMode = Sprint())
	{
		// Releasing only matters out of the blocks; mid-race it is just the
		// end of a tap.
		if (GameMode->GetPhase() == EWSEventPhase::Ready ||
			!GameMode->GetPlayerRunner() ||
			!GameMode->GetPlayerRunner()->GetState().bReleased)
		{
			GameMode->PlayerRelease();
		}
	}
}

void AWSSprintPlayerController::HandleTouchPressed(ETouchIndex::Type, FVector Location)
{
	TouchStart = Location;
	bSwipeConsumed = false;
	HandlePressed();
}

void AWSSprintPlayerController::HandleSwipeCheck(ETouchIndex::Type, FVector Location)
{
	if (bSwipeConsumed)
	{
		return;
	}
	// A downward drag past a clear threshold is the lean. The threshold is
	// generous so it never fires from a slightly sloppy tap.
	if (Location.Y - TouchStart.Y > 90.0f)
	{
		bSwipeConsumed = true;
		if (AWSSprintGameMode* GameMode = Sprint())
		{
			GameMode->PlayerLean();
		}
	}
}

void AWSSprintPlayerController::HandleTouchReleased(ETouchIndex::Type, FVector)
{
	HandleReleased();
}
