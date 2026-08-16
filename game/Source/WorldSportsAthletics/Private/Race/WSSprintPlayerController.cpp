#include "Race/WSSprintPlayerController.h"

#include "Components/InputComponent.h"
#include "Framework/WSEventPhase.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Race/WSSprintGameMode.h"
#include "Race/WSSprintRunner.h"

AWSSprintPlayerController::AWSSprintPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = false;
	bEnableTouchEvents = true;
	bEnableClickEvents = true;
}

void AWSSprintPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyInputModeForScreen();
}

void AWSSprintPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyInputModeForScreen();
}

void AWSSprintPlayerController::ApplyInputModeForScreen()
{
	AWSSprintGameMode* GameMode = Sprint();
	if (!GameMode)
	{
		return;
	}
	// On a menu the UI owns input: with GameAndUI, touches were consumed as
	// game input on device and the buttons never responded.
	const bool bWantUi =
		GameMode->GetAppState() != EWSAppState::Racing || GameMode->IsPaused();
	if (bInputModeApplied && bWantUi == bUiInputMode)
	{
		return;
	}
	bUiInputMode = bWantUi;
	bInputModeApplied = true;

	if (bWantUi)
	{
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		bShowMouseCursor = true; // harmless on touch, needed for desktop testing
	}
	else
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		bShowMouseCursor = false;
	}
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
	// The controller reports raw input; the race rules decide whether this
	// press is a block hold or a rhythm tap.
	if (AWSSprintGameMode* GameMode = Sprint())
	{
		GameMode->PlayerPress();
	}
}

void AWSSprintPlayerController::HandleReleased()
{
	if (AWSSprintGameMode* GameMode = Sprint())
	{
		GameMode->PlayerRelease();
	}
}

void AWSSprintPlayerController::HandleTouchPressed(ETouchIndex::Type Finger, FVector Location)
{
	// Only the first finger down drives the race. A second finger landing
	// (or lifting) must not be able to release the blocks — that turned an
	// ordinary two-thumb grip into a false start.
	if (HoldFinger != ETouchIndex::CursorPointerIndex)
	{
		return;
	}
	HoldFinger = Finger;
	TouchStart = Location;
	bSwipeConsumed = false;
	HandlePressed();
}

void AWSSprintPlayerController::HandleSwipeCheck(ETouchIndex::Type Finger, FVector Location)
{
	if (bSwipeConsumed || Finger != HoldFinger)
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

void AWSSprintPlayerController::HandleTouchReleased(ETouchIndex::Type Finger, FVector)
{
	if (Finger != HoldFinger)
	{
		return;
	}
	HoldFinger = ETouchIndex::CursorPointerIndex;
	HandleReleased();
}
