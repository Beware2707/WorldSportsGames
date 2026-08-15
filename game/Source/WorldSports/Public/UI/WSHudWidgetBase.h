#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Framework/WSEventPhase.h"

#include "WSHudWidgetBase.generated.h"

/** Base for event HUDs: auto-subscribes to phase changes so widget
 * Blueprints implement OnEventPhaseChanged instead of polling. */
UCLASS(Abstract)
class WORLDSPORTS_API UWSHudWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "WorldSports|HUD")
	void OnEventPhaseChanged(EWSEventPhase OldPhase, EWSEventPhase NewPhase);

private:
	UFUNCTION()
	void HandlePhaseChanged(EWSEventPhase OldPhase, EWSEventPhase NewPhase);
};
