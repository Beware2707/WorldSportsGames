#pragma once

#include "CoreMinimal.h"
#include "Framework/WSPlayerController.h"

#include "WSSprintPlayerController.generated.h"

/**
 * Touch/mouse/key input for the sprint, routed to the game mode.
 *
 * One-thumb rule (GAME_DESIGN.md §1): press-and-hold to set, release on the
 * gun, then tap to the rhythm. Every touch is the same gesture, so nothing
 * needs a second hand or a precise target.
 */
UCLASS()
class WORLDSPORTSATHLETICS_API AWSSprintPlayerController : public AWSPlayerController
{
	GENERATED_BODY()

public:
	AWSSprintPlayerController();

protected:
	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Menus need UI-only input; the race needs game input. Applying the
	 * wrong one leaves the menu buttons unresponsive to touch, which is
	 * exactly what happened on device with GameAndUI everywhere. */
	void ApplyInputModeForScreen();

private:
	void HandlePressed();
	void HandleReleased();
	void HandleTouchPressed(ETouchIndex::Type Finger, FVector Location);
	void HandleTouchReleased(ETouchIndex::Type Finger, FVector Location);
	void HandleSwipeCheck(ETouchIndex::Type Finger, FVector Location);

	class AWSSprintGameMode* Sprint() const;

	FVector TouchStart = FVector::ZeroVector;
	/** The finger currently driving the race; CursorPointerIndex = none. */
	ETouchIndex::Type HoldFinger = ETouchIndex::CursorPointerIndex;
	bool bSwipeConsumed = false;
	bool bUiInputMode = false;
	bool bInputModeApplied = false;
};
