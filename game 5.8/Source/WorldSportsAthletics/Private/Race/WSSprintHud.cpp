#include "Race/WSSprintHud.h"

#include "Framework/WSEventPhase.h"
#include "Race/WSSprintGameMode.h"
#include "Race/WSSprintRunner.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WSSprintHud"

namespace
{
FSlateFontInfo Font(int32 Size, bool bBold = false)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}

FText FormatSeconds(double Seconds)
{
	if (Seconds <= 0.0)
	{
		return FText::FromString(TEXT("--.--"));
	}
	// Truncate: a clock never shows a time the athlete has not yet run.
	const int32 Hundredths = static_cast<int32>(Seconds * 100.0);
	return FText::FromString(FString::Printf(
		TEXT("%d.%02d"), Hundredths / 100, Hundredths % 100));
}
}

/** The whole HUD as one native panel, polled per paint from the game mode. */
class SWSSprintHudPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWSSprintHudPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AWSSprintGameMode>, GameMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		GameMode = InArgs._GameMode;

		ChildSlot
		[
			SNew(SOverlay)

			// --- Top strip: clock, position, reaction --------------------
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill).VAlign(VAlign_Top)
			.Padding(24.0f, 18.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Font(Font(34, true))
					.ColorAndOpacity(FLinearColor::White)
					.Text(this, &SWSSprintHudPanel::GetClockText)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Font(Font(20))
					.ColorAndOpacity(FLinearColor(0.85f, 0.9f, 1.0f))
					.Text(this, &SWSSprintHudPanel::GetSpeedText)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Font(Font(20))
					.ColorAndOpacity(FLinearColor(0.85f, 0.9f, 1.0f))
					.Text(this, &SWSSprintHudPanel::GetReactionText)
				]
			]

			// --- Centre: the instruction that changes with the phase -----
			+ SOverlay::Slot()
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(Font(46, true))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(this, &SWSSprintHudPanel::GetPromptColor)
				.Text(this, &SWSSprintHudPanel::GetPromptText)
			]

			// --- Cadence band: position IS the signal, plus a label ------
			+ SOverlay::Slot()
			.HAlign(HAlign_Center).VAlign(VAlign_Bottom)
			.Padding(0.0f, 0.0f, 0.0f, 108.0f)
			[
				SNew(SBox)
				.WidthOverride(520.0f)
				.HeightOverride(58.0f)
				.Visibility(this, &SWSSprintHudPanel::GetBandVisibility)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SImage).ColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f))
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Left).VAlign(VAlign_Fill)
					[
						// Marker slides with cadence accuracy; centre = on beat.
						SNew(SBox)
						.WidthOverride(14.0f)
						.Padding(this, &SWSSprintHudPanel::GetMarkerPadding)
						[
							SNew(SImage)
							.ColorAndOpacity(this, &SWSSprintHudPanel::GetBandColor)
						]
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(Font(16, true))
						.ColorAndOpacity(FLinearColor::White)
						.Text(this, &SWSSprintHudPanel::GetBandLabel)
					]
				]
			]

			// --- Bottom: stamina + splits -------------------------------
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill).VAlign(VAlign_Bottom)
			.Padding(24.0f, 0.0f, 24.0f, 24.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Font(Font(18))
					.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))
					.Text(this, &SWSSprintHudPanel::GetStaminaText)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Font(Font(18))
					.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))
					.Text(this, &SWSSprintHudPanel::GetSplitsText)
				]
			]

			// --- Result panel -------------------------------------------
			+ SOverlay::Slot()
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(760.0f)
				.Visibility(this, &SWSSprintHudPanel::GetResultVisibility)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SImage).ColorAndOpacity(FLinearColor(0.02f, 0.03f, 0.06f, 0.92f))
					]
					+ SOverlay::Slot()
					.Padding(28.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
						[
							SNew(STextBlock)
							.Font(Font(30, true))
							.ColorAndOpacity(FLinearColor::White)
							.Text(this, &SWSSprintHudPanel::GetResultHeadline)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
						[
							SNew(STextBlock)
							.Font(Font(17))
							.ColorAndOpacity(FLinearColor(0.75f, 0.85f, 1.0f))
							.Text(this, &SWSSprintHudPanel::GetServerVerdictText)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
						[
							SNew(STextBlock)
							.Font(Font(16))
							.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.9f))
							.Text(this, &SWSSprintHudPanel::GetStandingsText)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(SBox).WidthOverride(280.0f).HeightOverride(64.0f)
							[
								SNew(SButton)
								.HAlign(HAlign_Center).VAlign(VAlign_Center)
								.OnClicked(this, &SWSSprintHudPanel::OnRaceAgain)
								[
									SNew(STextBlock)
									.Font(Font(22, true))
									.Text(LOCTEXT("RaceAgain", "Race again"))
								]
							]
						]
					]
				]
			]
		];
	}

private:
	AWSSprintGameMode* Mode() const { return GameMode.Get(); }

	const FWSSprintState* PlayerState() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		AWSSprintRunner* Runner = GameModePtr ? GameModePtr->GetPlayerRunner() : nullptr;
		return Runner ? &Runner->GetState() : nullptr;
	}

	FText GetClockText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr)
		{
			return FText::GetEmpty();
		}
		const double Clock = GameModePtr->GetRaceClock();
		if (Clock < 0.0)
		{
			return FText::FromString(TEXT("0.00"));
		}
		const FWSSprintState* State = PlayerState();
		if (State && State->bFinished)
		{
			AWSSprintRunner* Runner = GameModePtr->GetPlayerRunner();
			return FormatSeconds(Runner->GetOutcome().TimeSeconds);
		}
		return FormatSeconds(Clock);
	}

	FText GetSpeedText() const
	{
		const FWSSprintState* State = PlayerState();
		if (!State || !State->bReleased || State->bFinished)
		{
			return FText::GetEmpty();
		}
		return FText::FromString(FString::Printf(
			TEXT("%.1f m/s · %.0f m"), State->Speed, State->Distance));
	}

	FText GetReactionText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		AWSSprintRunner* Runner = GameModePtr ? GameModePtr->GetPlayerRunner() : nullptr;
		if (!Runner)
		{
			return FText::GetEmpty();
		}
		const FWSSprintOutcome Outcome = Runner->GetOutcome();
		if (Outcome.bFalseStart)
		{
			return LOCTEXT("FalseStartTag", "FALSE START");
		}
		if (!Runner->GetState().bReleased)
		{
			return FText::GetEmpty();
		}
		// Milliseconds, honestly, every race — a design pillar.
		return FText::FromString(FString::Printf(
			TEXT("RT %.0f ms"), Outcome.ReactionMs));
	}

	FText GetPromptText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr)
		{
			return FText::GetEmpty();
		}
		const FWSSprintState* State = PlayerState();
		if (State && State->bFalseStart)
		{
			return LOCTEXT("FalseStartPrompt", "FALSE START");
		}
		switch (GameModePtr->GetPhase())
		{
		case EWSEventPhase::Ready:
		{
			const double Clock = GameModePtr->GetRaceClock();
			if (!State || !State->bReleased)
			{
				if (Clock < -1.6)
				{
					return LOCTEXT("OnYourMarks", "On your marks");
				}
				if (Clock < -0.15)
				{
					return LOCTEXT("Set", "Set — hold");
				}
				return LOCTEXT("Wait", "…");
			}
			return FText::GetEmpty();
		}
		case EWSEventPhase::Active:
			return State && !State->bReleased
				? LOCTEXT("Go", "GO!")
				: FText::GetEmpty();
		default:
			return FText::GetEmpty();
		}
	}

	FSlateColor GetPromptColor() const
	{
		const FWSSprintState* State = PlayerState();
		if (State && State->bFalseStart)
		{
			return FSlateColor(FLinearColor(1.0f, 0.35f, 0.3f));
		}
		return FSlateColor(FLinearColor::White);
	}

	EVisibility GetBandVisibility() const
	{
		const FWSSprintState* State = PlayerState();
		return State && State->bReleased && !State->bFinished && !State->bFalseStart
			? EVisibility::HitTestInvisible
			: EVisibility::Collapsed;
	}

	/** Marker offset encodes cadence error: left = slow, right = fast. */
	FMargin GetMarkerPadding() const
	{
		const FWSSprintState* State = PlayerState();
		if (!State || State->TargetCadenceHz <= 0.0)
		{
			return FMargin(253.0f, 0.0f, 0.0f, 0.0f);
		}
		const double Error = State->ActualCadenceHz - State->TargetCadenceHz;
		const double Normalized = FMath::Clamp(Error / 1.2, -1.0, 1.0);
		return FMargin(253.0f + static_cast<float>(Normalized) * 240.0f, 0.0f, 0.0f, 0.0f);
	}

	FSlateColor GetBandColor() const
	{
		const FWSSprintState* State = PlayerState();
		const float Accuracy = State ? static_cast<float>(State->CadenceAccuracy) : 0.0f;
		// Colour reinforces; position and the label carry the meaning.
		return FSlateColor(FMath::Lerp(
			FLinearColor(1.0f, 0.55f, 0.2f), FLinearColor(0.3f, 1.0f, 0.45f), Accuracy));
	}

	FText GetBandLabel() const
	{
		const FWSSprintState* State = PlayerState();
		if (!State || State->TargetCadenceHz <= 0.0)
		{
			return FText::GetEmpty();
		}
		const double Error = State->ActualCadenceHz - State->TargetCadenceHz;
		if (FMath::Abs(Error) < 0.25)
		{
			return LOCTEXT("OnRhythm", "ON RHYTHM");
		}
		return Error < 0.0 ? LOCTEXT("Faster", "FASTER →") : LOCTEXT("Slower", "← SLOWER");
	}

	FText GetStaminaText() const
	{
		const FWSSprintState* State = PlayerState();
		if (!State || !State->bReleased || State->bFinished)
		{
			return FText::GetEmpty();
		}
		const int32 Percent = FMath::RoundToInt((1.0 - State->Fatigue) * 100.0);
		return FText::FromString(FString::Printf(TEXT("Stamina %d%%"), Percent));
	}

	FText GetSplitsText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		AWSSprintRunner* Runner = GameModePtr ? GameModePtr->GetPlayerRunner() : nullptr;
		if (!Runner)
		{
			return FText::GetEmpty();
		}
		const TArray<double>& Splits = Runner->GetOutcome().Splits;
		if (Splits.Num() == 0)
		{
			return FText::GetEmpty();
		}
		const int32 First = FMath::Max(0, Splits.Num() - 3);
		FString Text = TEXT("Splits");
		for (int32 Index = First; Index < Splits.Num(); ++Index)
		{
			Text += FString::Printf(TEXT("  %dm %.2f"), (Index + 1) * 10, Splits[Index]);
		}
		return FText::FromString(Text);
	}

	EVisibility GetResultVisibility() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr)
		{
			return EVisibility::Collapsed;
		}
		const EWSEventPhase Phase = GameModePtr->GetPhase();
		return (Phase == EWSEventPhase::Result || Phase == EWSEventPhase::Submit ||
				Phase == EWSEventPhase::Reward)
			? EVisibility::SelfHitTestInvisible
			: EVisibility::Collapsed;
	}

	FText GetResultHeadline() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		AWSSprintRunner* Runner = GameModePtr ? GameModePtr->GetPlayerRunner() : nullptr;
		if (!Runner)
		{
			return FText::GetEmpty();
		}
		const FWSSprintOutcome Outcome = Runner->GetOutcome();
		if (Outcome.bFalseStart)
		{
			return LOCTEXT("DQ", "Disqualified — false start");
		}
		return FText::FromString(FString::Printf(TEXT("%d%s   %s   (%+.1f m/s)"),
			GameModePtr->GetPlayerPosition(),
			*FString(GameModePtr->GetPlayerPosition() == 1 ? TEXT("st") :
				GameModePtr->GetPlayerPosition() == 2 ? TEXT("nd") :
				GameModePtr->GetPlayerPosition() == 3 ? TEXT("rd") : TEXT("th")),
			*FormatSeconds(Outcome.TimeSeconds).ToString(),
			Outcome.Wind));
	}

	FText GetServerVerdictText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr)
		{
			return FText::GetEmpty();
		}
		if (GameModePtr->IsAwaitingServer())
		{
			return LOCTEXT("Verifying", "Verifying with the server…");
		}
		return FText::FromString(GameModePtr->GetServerVerdict());
	}

	FText GetStandingsText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr)
		{
			return FText::GetEmpty();
		}
		FString Text;
		for (const FWSRaceStanding& Standing : GameModePtr->GetStandings())
		{
			if (Standing.bFalseStart)
			{
				Text += FString::Printf(TEXT("DQ   %s   false start\n"), *Standing.Name);
				continue;
			}
			Text += FString::Printf(TEXT("%d.  %-12s  %s   RT %.0f ms%s\n"),
				Standing.Position, *Standing.Name,
				*FormatSeconds(Standing.TimeSeconds).ToString(),
				Standing.ReactionMs,
				Standing.bIsPlayer ? TEXT("   ←") : TEXT(""));
		}
		return FText::FromString(Text);
	}

	FReply OnRaceAgain()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->StartRace();
		}
		return FReply::Handled();
	}

	TWeakObjectPtr<AWSSprintGameMode> GameMode;
};

void UWSSprintHud::BindGameMode(AWSSprintGameMode* InGameMode)
{
	GameMode = InGameMode;
	if (Panel.IsValid())
	{
		Panel.Reset();
		RebuildWidget();
	}
}

TSharedRef<SWidget> UWSSprintHud::RebuildWidget()
{
	Panel = SNew(SWSSprintHudPanel).GameMode(GameMode);
	return Panel.ToSharedRef();
}

void UWSSprintHud::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Panel.Reset();
}

#undef LOCTEXT_NAMESPACE
