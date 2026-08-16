#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "UI/WSHudWidgetBase.h"

#include "WSSprintHud.generated.h"

class AWSSprintGameMode;
class SWSSprintHudPanel;

/**
 * The race HUD. Built as a native Slate panel rather than a UMG asset so
 * the whole vertical slice lives in reviewable source — no binary widget
 * blueprint, no content migration, and it can be unit-reasoned about.
 *
 * Accessibility (GAME_DESIGN.md §7): the cadence band uses POSITION and a
 * text label, never colour alone; the reaction and splits are shown as real
 * numbers because honest feedback is a design pillar.
 */
UCLASS()
class WORLDSPORTSATHLETICS_API UWSSprintHud : public UWSHudWidgetBase
{
	GENERATED_BODY()

public:
	void BindGameMode(AWSSprintGameMode* InGameMode);
	AWSSprintGameMode* GetGameMode() const { return GameMode.Get(); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TWeakObjectPtr<AWSSprintGameMode> GameMode;
	TSharedPtr<SWSSprintHudPanel> Panel;
};
