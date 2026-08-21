#include "Race/WSSprintHud.h"

#include "Core/WSDeviceProfileSubsystem.h"
#include "Framework/WSEventPhase.h"
#include "Race/WSSprintGameMode.h"
#include "Race/WSSprintRunner.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
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

/** A big, thumb-sized button — every touch target is at least 64px tall. */
TSharedRef<SWidget> MenuButton(const FText& Label, FOnClicked OnClicked, float Width = 380.0f)
{
	return SNew(SBox)
		.WidthOverride(Width)
		.HeightOverride(72.0f)
		.Padding(FMargin(0.0f, 6.0f))
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(OnClicked)
			[
				SNew(STextBlock).Font(Font(22, true)).Text(Label)
			]
		];
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
						// Marker slides with cadence accuracy; centre = on
						// beat. The offset is a bound spacer width — padding
						// inside the 14-wide box squeezed the marker to zero
						// width, so it never rendered at all.
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).WidthOverride(this, &SWSSprintHudPanel::GetMarkerOffset)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(14.0f)
							[
								SNew(SImage)
								.ColorAndOpacity(this, &SWSSprintHudPanel::GetBandColor)
							]
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

			// --- The jump: board countdown, series, and TAKE OFF ---------
			+ SOverlay::Slot()
			.HAlign(HAlign_Center).VAlign(VAlign_Bottom)
			// Above the cadence band, which the approach still uses: at the
			// same height the two drew over each other.
			.Padding(0.0f, 0.0f, 0.0f, 172.0f)
			[
				SNew(STextBlock).Font(Font(30, true))
				.ColorAndOpacity(FLinearColor(1.0f, 0.86f, 0.35f))
				.Visibility(this, &SWSSprintHudPanel::GetJumpVisibility)
				.Text(this, &SWSSprintHudPanel::GetJumpText)
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Left).VAlign(VAlign_Top)
			.Padding(24.0f, 96.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Font(Font(20))
				.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))
				.Visibility(this, &SWSSprintHudPanel::GetJumpSeriesVisibility)
				.Text(this, &SWSSprintHudPanel::GetJumpSeriesText)
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Right).VAlign(VAlign_Bottom)
			.Padding(0.0f, 0.0f, 48.0f, 120.0f)
			[
				SNew(SBox)
				.WidthOverride(300.0f).HeightOverride(160.0f)
				.Visibility(this, &SWSSprintHudPanel::GetJumpButtonVisibility)
				[
					SNew(SButton)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.OnClicked(this, &SWSSprintHudPanel::OnTakeoff)
					[
						SNew(STextBlock).Font(Font(34, true))
						.Text(LOCTEXT("TakeoffButton", "TAKE OFF"))
					]
				]
			]

			// --- The takeoff button, armed only near a barrier -----------
			// A downward swipe is how the dip at the line is asked for, and
			// it is fine for one action in a race. It is NOT fine for ten:
			// a swipe is slow, and its own first touch also lands as a
			// cadence tap, so hurdling by swipe fought the rhythm the same
			// race was judging. A button that only exists when a barrier is
			// close is unmissable and costs one thumb press.
			+ SOverlay::Slot()
			.HAlign(HAlign_Right).VAlign(VAlign_Bottom)
			.Padding(0.0f, 0.0f, 48.0f, 120.0f)
			[
				SNew(SBox)
				.WidthOverride(280.0f).HeightOverride(150.0f)
				.Visibility(this, &SWSSprintHudPanel::GetHurdleButtonVisibility)
				[
					SNew(SButton)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.OnClicked(this, &SWSSprintHudPanel::OnHurdle)
					[
						SNew(STextBlock).Font(Font(34, true))
						.Text(LOCTEXT("HurdleButton", "HURDLE"))
					]
				]
			]

			// --- The hurdler's takeoff prompt, above the cadence band ----
			+ SOverlay::Slot()
			.HAlign(HAlign_Center).VAlign(VAlign_Bottom)
			.Padding(0.0f, 0.0f, 0.0f, 168.0f)
			[
				SNew(STextBlock).Font(Font(30, true))
				.ColorAndOpacity(FLinearColor(1.0f, 0.86f, 0.35f))
				.Visibility(this, &SWSSprintHudPanel::GetHurdleVisibility)
				.Text(this, &SWSSprintHudPanel::GetHurdleText)
			]

			// --- The paced events' readout, where the cadence band sits --
			+ SOverlay::Slot()
			.HAlign(HAlign_Center).VAlign(VAlign_Bottom)
			.Padding(0.0f, 0.0f, 0.0f, 96.0f)
			[
				SNew(STextBlock).Font(Font(24, true))
				.ColorAndOpacity(FLinearColor(0.92f, 0.92f, 0.96f))
				.Visibility(this, &SWSSprintHudPanel::GetPaceVisibility)
				.Text(this, &SWSSprintHudPanel::GetPaceText)
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

			// --- Pause button, only while a race is live -----------------
			+ SOverlay::Slot()
			.HAlign(HAlign_Right).VAlign(VAlign_Top)
			.Padding(0.0f, 76.0f, 24.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(120.0f).HeightOverride(64.0f)
				.Visibility(this, &SWSSprintHudPanel::GetPauseButtonVisibility)
				[
					SNew(SButton)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.OnClicked(this, &SWSSprintHudPanel::OnPause)
					[
						SNew(STextBlock).Font(Font(18, true))
						.Text(LOCTEXT("Pause", "II"))
					]
				]
			]

			// --- Screens: menu / sign-in / leaderboard / settings -------
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[
				SNew(SOverlay)
				.Visibility(this, &SWSSprintHudPanel::GetScreenVisibility)

				+ SOverlay::Slot()
				[
					SNew(SImage).ColorAndOpacity(FLinearColor(0.02f, 0.03f, 0.06f, 0.97f))
				]

				// Menu
				+ SOverlay::Slot()
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					.Visibility(this, &SWSSprintHudPanel::GetMenuVisibility)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 6)
					[
						SNew(STextBlock).Font(Font(44, true))
						.ColorAndOpacity(FLinearColor::White)
						.Text(this, &SWSSprintHudPanel::GetTitleText)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 20)
					[
						SNew(STextBlock).Font(Font(16))
						.ColorAndOpacity(FLinearColor(0.7f, 0.78f, 0.9f))
						.Text(this, &SWSSprintHudPanel::GetAccountLine)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							MenuButton(LOCTEXT("PrevEvent", "◀"),
								FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnPrevEvent),
								86.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							// Wide enough for the longest event name in the
							// table: "110m Hurdles" ran under the ▶ button
							// at the width the 100m needed.
							SNew(SBox).WidthOverride(300.0f).HeightOverride(72.0f)
							.Padding(FMargin(0.0f, 6.0f))
							.HAlign(HAlign_Center).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Font(Font(22, true))
								.ColorAndOpacity(FLinearColor::White)
								.Justification(ETextJustify::Center)
								.Text(this, &SWSSprintHudPanel::GetEventText)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							MenuButton(LOCTEXT("NextEvent", "▶"),
								FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnNextEvent),
								86.0f)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						MenuButton(LOCTEXT("QuickPlay", "Quick Play"),
							FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnQuickPlay))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						MenuButton(LOCTEXT("Career", "Career"),
							FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnCareer))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						MenuButton(LOCTEXT("Tournament", "Tournament"),
							FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnTournament))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						MenuButton(LOCTEXT("Leaderboard", "Leaderboard"),
							FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnLeaderboard))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						MenuButton(LOCTEXT("Account", "Account"),
							FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnAccount))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						MenuButton(LOCTEXT("Settings", "Settings"),
							FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnSettings))
					]
				]

				// Sign in / register
				+ SOverlay::Slot()
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(560.0f)
					.Visibility(this, &SWSSprintHudPanel::GetSignInVisibility)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
						[
							SNew(STextBlock).Font(Font(30, true))
							.ColorAndOpacity(FLinearColor::White)
							.Text(LOCTEXT("AccountTitle", "Account"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
						[
							SNew(STextBlock).Font(Font(15))
							.ColorAndOpacity(FLinearColor(0.7f, 0.78f, 0.9f))
							.Text(LOCTEXT("AccountWhy",
								"Sign in to record results, earn XP and enter leaderboards.\n"
								"Results you run offline are kept and submitted later."))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
						[
							SAssignNew(EmailBox, SEditableTextBox)
							.Font(Font(20))
							.HintText(LOCTEXT("Email", "Email"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
						[
							SAssignNew(PasswordBox, SEditableTextBox)
							.Font(Font(20))
							.IsPassword(true)
							.HintText(LOCTEXT("Password", "Password (8+ characters)"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
						[
							SAssignNew(NameBox, SEditableTextBox)
							.Font(Font(20))
							.HintText(LOCTEXT("DisplayName", "Display name (new accounts)"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("SignIn", "Sign in"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnSignIn), 260.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
							[
								MenuButton(LOCTEXT("Register", "Create account"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnRegister), 280.0f)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 10)
						[
							SNew(STextBlock).Font(Font(16))
							.ColorAndOpacity(FLinearColor(0.85f, 0.9f, 1.0f))
							.AutoWrapText(true)
							.Text(this, &SWSSprintHudPanel::GetAccountStatus)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("SignOut", "Sign out"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnSignOut), 220.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
							[
								MenuButton(LOCTEXT("Back", "Back"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnBackToMenu), 220.0f)
							]
						]
					]
				]

				// Leaderboard
				+ SOverlay::Slot()
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(680.0f).HeightOverride(560.0f)
					.Visibility(this, &SWSSprintHudPanel::GetLeaderboardVisibility)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
						[
							SNew(STextBlock).Font(Font(30, true))
							.ColorAndOpacity(FLinearColor::White)
							.Text(this, &SWSSprintHudPanel::GetBoardTitle)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
						[
							SNew(STextBlock).Font(Font(16))
							.ColorAndOpacity(FLinearColor(0.85f, 0.9f, 1.0f))
							.Text(this, &SWSSprintHudPanel::GetLeaderboardStatus)
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SNew(STextBlock).Font(Font(18))
								.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.92f))
								.Text(this, &SWSSprintHudPanel::GetLeaderboardText)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
						[
							MenuButton(LOCTEXT("Back", "Back"),
								FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnBackToMenu), 260.0f)
						]
					]
				]

				// Career: athlete, attributes, records
				+ SOverlay::Slot()
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(760.0f).HeightOverride(560.0f)
					.Visibility(this, &SWSSprintHudPanel::GetCareerVisibility)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
						[
							SNew(STextBlock).Font(Font(30, true))
							.ColorAndOpacity(FLinearColor::White)
							.Text(LOCTEXT("CareerTitle", "Career"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
						[
							SNew(STextBlock).Font(Font(16))
							.ColorAndOpacity(FLinearColor(0.85f, 0.9f, 1.0f))
							.AutoWrapText(true)
							.Text(this, &SWSSprintHudPanel::GetCareerStatus)
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SNew(STextBlock).Font(Font(17))
								.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.92f))
								.Text(this, &SWSSprintHudPanel::GetCareerSummary)
							]
							+ SScrollBox::Slot()
							[
								SNew(STextBlock).Font(Font(16))
								.ColorAndOpacity(FLinearColor(0.75f, 0.85f, 1.0f))
								.Text(this, &SWSSprintHudPanel::GetRecordsText)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("Train", "Train"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnTrain), 200.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
							[
								MenuButton(LOCTEXT("CreateAthleteBtn", "New athlete"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnShowCreateAthlete), 240.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("Back", "Back"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnBackToMenu), 180.0f)
							]
						]
					]
				]

				// Create athlete
				+ SOverlay::Slot()
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(560.0f)
					.Visibility(this, &SWSSprintHudPanel::GetCreateAthleteVisibility)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
						[
							SNew(STextBlock).Font(Font(30, true))
							.ColorAndOpacity(FLinearColor::White)
							.Text(LOCTEXT("NewAthlete", "New athlete"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
						[
							SNew(STextBlock).Font(Font(15))
							.ColorAndOpacity(FLinearColor(0.7f, 0.78f, 0.9f))
							.AutoWrapText(true)
							.Text(LOCTEXT("NewAthleteWhy",
								"Every attribute starts at 40 and rises only through training the "
								"server validates. Your athlete name appears on leaderboards."))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
						[
							SAssignNew(AthleteNameBox, SEditableTextBox)
							.Font(Font(20))
							.HintText(LOCTEXT("AthleteName", "Athlete name"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 4)
						[
							SNew(STextBlock).Font(Font(16))
							.ColorAndOpacity(FLinearColor(0.8f, 0.85f, 0.95f))
							.Text(LOCTEXT("PickCategory", "Pick a category to create:"))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("GenderF", "Women"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnGenderF), 170.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
							[
								MenuButton(LOCTEXT("GenderM", "Men"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnGenderM), 170.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("GenderX", "Open"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnGenderX), 170.0f)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 10)
						[
							SNew(STextBlock).Font(Font(16))
							.ColorAndOpacity(FLinearColor(0.85f, 0.9f, 1.0f))
							.AutoWrapText(true)
							.Text(this, &SWSSprintHudPanel::GetCareerStatus)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							MenuButton(LOCTEXT("Back", "Back"),
								FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnCareer), 220.0f)
						]
					]
				]

				// Training: the reaction drill
				+ SOverlay::Slot()
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(700.0f)
					.Visibility(this, &SWSSprintHudPanel::GetTrainingVisibility)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)
						[
							SNew(STextBlock).Font(Font(30, true))
							.ColorAndOpacity(FLinearColor::White)
							.Text(LOCTEXT("ReactionDrill", "Reaction Drill"))
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 18)
						[
							SNew(STextBlock).Font(Font(20))
							.ColorAndOpacity(FLinearColor(0.85f, 0.9f, 1.0f))
							.Justification(ETextJustify::Center)
							.AutoWrapText(true)
							.Text(this, &SWSSprintHudPanel::GetDrillPrompt)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 16)
						[
							SNew(SBox).WidthOverride(420.0f).HeightOverride(150.0f)
							[
								SNew(SButton)
								.HAlign(HAlign_Center).VAlign(VAlign_Center)
								.OnPressed(FSimpleDelegate::CreateSP(this, &SWSSprintHudPanel::OnDrillPress))
								.OnReleased(FSimpleDelegate::CreateSP(this, &SWSSprintHudPanel::OnDrillRelease))
								[
									SNew(STextBlock).Font(Font(24, true))
									.Text(LOCTEXT("HoldHere", "HOLD"))
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 16)
						[
							SNew(STextBlock).Font(Font(18))
							.ColorAndOpacity(FLinearColor(0.9f, 0.95f, 1.0f))
							.Justification(ETextJustify::Center)
							.AutoWrapText(true)
							.Text(this, &SWSSprintHudPanel::GetDrillResult)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("AgainDrill", "Again"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnTrain), 200.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
							[
								MenuButton(LOCTEXT("BackCareer", "Back"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnCareer), 200.0f)
							]
						]
					]
				]

				// Tournament: the bracket, the current draw, the next round
				+ SOverlay::Slot()
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(760.0f).HeightOverride(600.0f)
					.Visibility(this, &SWSSprintHudPanel::GetTournamentVisibility)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
						[
							SNew(STextBlock).Font(Font(30, true))
							.ColorAndOpacity(FLinearColor::White)
							.Text(LOCTEXT("TournamentTitle", "Tournament"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
						[
							SNew(STextBlock).Font(Font(16))
							.ColorAndOpacity(FLinearColor(0.85f, 0.9f, 1.0f))
							.AutoWrapText(true)
							.Text(this, &SWSSprintHudPanel::GetTournamentStatus)
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SNew(STextBlock).Font(Font(17))
								.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.92f))
								.Text(this, &SWSSprintHudPanel::GetTournamentSummary)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("EnterTourney", "Enter"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnEnterTournament), 200.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
							[
								MenuButton(LOCTEXT("RaceRound", "Race round"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnRaceRound), 240.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("BackT", "Back"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnBackToMenu), 180.0f)
							]
						]
					]
				]

				// Settings
				+ SOverlay::Slot()
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					.Visibility(this, &SWSSprintHudPanel::GetSettingsVisibility)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 12)
					[
						SNew(STextBlock).Font(Font(30, true))
						.ColorAndOpacity(FLinearColor::White)
						.Text(LOCTEXT("SettingsTitle", "Settings"))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)
					[
						SNew(STextBlock).Font(Font(17))
						.ColorAndOpacity(FLinearColor(0.75f, 0.82f, 0.95f))
						.Text(this, &SWSSprintHudPanel::GetQualityText)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							MenuButton(LOCTEXT("QualityLow", "Low"),
								FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnQualityLow), 180.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
						[
							MenuButton(LOCTEXT("QualityMid", "Medium"),
								FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnQualityMid), 180.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							MenuButton(LOCTEXT("QualityHigh", "High"),
								FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnQualityHigh), 180.0f)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 18, 0, 0)
					[
						MenuButton(LOCTEXT("Back", "Back"),
							FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnBackToMenu), 260.0f)
					]
				]
			]

			// --- Pause overlay ------------------------------------------
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[
				SNew(SOverlay)
				.Visibility(this, &SWSSprintHudPanel::GetPauseOverlayVisibility)
				+ SOverlay::Slot()
				[
					// A button, not a bare image: an SImage lets the press
					// bubble through to the viewport, where it becomes race
					// input. The race also ignores input while paused, so this
					// is belt and braces — but a pause screen that leaks
					// touches to the athlete is indefensible either way.
					SNew(SButton)
					.ButtonColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
					.OnClicked(FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnSwallowClick))
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 16)
					[
						SNew(STextBlock).Font(Font(34, true))
						.ColorAndOpacity(FLinearColor::White)
						.Text(LOCTEXT("Paused", "Paused"))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						MenuButton(LOCTEXT("Resume", "Resume"),
							FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnResume))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						MenuButton(LOCTEXT("RestartRace", "Restart race"),
							FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnRaceAgain))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						MenuButton(LOCTEXT("QuitToMenu", "Quit to menu"),
							FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnQuitToMenu))
					]
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
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("RaceAgain", "Race again"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnRaceAgain), 240.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
							[
								SNew(SBox)
								// A jump has no finish line to replay, so the
								// button that offers one is not shown for it.
								.Visibility(this, &SWSSprintHudPanel::GetReplayButtonVisibility)
								[
									MenuButton(LOCTEXT("Replay", "Replay finish"),
										FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnReplay), 240.0f)
								]
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MenuButton(LOCTEXT("Menu", "Menu"),
									FOnClicked::CreateSP(this, &SWSSprintHudPanel::OnQuitToMenu), 180.0f)
							]
						]
					]
				]
			]
		];
	}

private:
	AWSSprintGameMode* Mode() const { return GameMode.Get(); }

	const FWSRaceState* PlayerState() const
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
		const FWSRaceState* State = PlayerState();
		if (State && State->bFinished)
		{
			AWSSprintRunner* Runner = GameModePtr->GetPlayerRunner();
			return FormatSeconds(Runner->GetOutcome().TimeSeconds);
		}
		return FormatSeconds(Clock);
	}

	FText GetSpeedText() const
	{
		const FWSRaceState* State = PlayerState();
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
		const FWSRaceOutcome Outcome = Runner->GetOutcome();
		if (Outcome.bFalseStart)
		{
			return LOCTEXT("FalseStartTag", "FALSE START");
		}
		if (!Runner->GetState().bReleased)
		{
			return FText::GetEmpty();
		}
		if (Runner->IsPaceEvent() || Runner->IsFieldEvent())
		{
			// No blocks, so no reaction was measured. Printing "RT 0 ms"
			// would report a measurement that was never taken — the same
			// mistake as labelling a 400m's splits every 10m.
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
		const FWSRaceState* State = PlayerState();
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
		const FWSRaceState* State = PlayerState();
		if (State && State->bFalseStart)
		{
			return FSlateColor(FLinearColor(1.0f, 0.35f, 0.3f));
		}
		return FSlateColor(FLinearColor::White);
	}

	EVisibility GetBandVisibility() const
	{
		// A paced event has no cadence to match. Showing a rhythm band there
		// would be telling the player to do something the simulation does
		// not measure.
		AWSSprintGameMode* GameModePtr = Mode();
		if (GameModePtr && GameModePtr->IsPaceEvent())
		{
			return EVisibility::Collapsed;
		}
		const FWSRaceState* State = PlayerState();
		return State && State->bReleased && !State->bFinished && !State->bFalseStart
			? EVisibility::HitTestInvisible
			: EVisibility::Collapsed;
	}

	/** What the paced events show instead: effort against the pace the
	 * athlete can actually hold, and what is left in the tank. */
	EVisibility GetPaceVisibility() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr || !GameModePtr->IsPaceEvent())
		{
			return EVisibility::Collapsed;
		}
		const FWSRaceState* State = PlayerState();
		return State && !State->bFinished
			? EVisibility::HitTestInvisible
			: EVisibility::Collapsed;
	}

	/** "HURDLE" when a barrier is close enough to take off for. */
	FText GetHurdleText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		AWSSprintRunner* Runner = GameModePtr ? GameModePtr->GetPlayerRunner() : nullptr;
		if (!Runner || Runner->IsPaceEvent())
		{
			return FText::GetEmpty();
		}
		const double ToHurdle = Runner->MetresToNextHurdle();
		if (ToHurdle < 0.0 || ToHurdle > 7.0)
		{
			return FText::GetEmpty();
		}
		// Counting down in metres, because the takeoff is judged in metres:
		// the player is learning where to leave the ground, not when.
		return FText::FromString(FString::Printf(TEXT("HURDLE  %.1f m"), ToHurdle));
	}

	EVisibility GetHurdleVisibility() const
	{
		return GetHurdleText().IsEmpty() ? EVisibility::Collapsed
			: EVisibility::HitTestInvisible;
	}

	/** The button itself has to accept the touch, so it is Visible rather
	 * than HitTestInvisible like the countdown text above it. */
	EVisibility GetHurdleButtonVisibility() const
	{
		return GetHurdleText().IsEmpty() ? EVisibility::Collapsed
			: EVisibility::Visible;
	}

	FReply OnHurdle()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			// PlayerLean is the one contextual action: with a barrier armed
			// it is the takeoff, otherwise the dip at the line.
			GameModePtr->PlayerLean();
		}
		return FReply::Handled();
	}

	/** The board countdown: what a jumper is actually reading. */
	FText GetJumpText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr || !GameModePtr->IsJumpEvent())
		{
			return FText::GetEmpty();
		}
		const float ToBoard = GameModePtr->GetMetresToBoard();
		if (ToBoard > 12.0f)
		{
			return FText::FromString(FString::Printf(
				TEXT("ATTEMPT %d   build your speed"), GameModePtr->GetJumpAttempt()));
		}
		if (ToBoard < 0.0f)
		{
			return LOCTEXT("PastBoard", "PAST THE BOARD");
		}
		// Metres, because the mark is measured in metres from the board and
		// the takeoff is judged the same way.
		return FText::FromString(FString::Printf(TEXT("BOARD  %.1f m"), ToBoard));
	}

	EVisibility GetJumpVisibility() const
	{
		// Gated on a LIVE attempt, not merely on the event being selected:
		// the board readout was showing through the main menu, and it stayed
		// on screen reading "PAST THE BOARD" after the series was over.
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr && GameModePtr->IsJumpEvent()
			&& GameModePtr->GetAppState() == EWSAppState::Racing
			&& GameModePtr->GetPhase() == EWSEventPhase::Active
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}

	/** The series is worth seeing while racing AND on the result. */
	EVisibility GetJumpSeriesVisibility() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr && GameModePtr->IsJumpEvent()
			&& GameModePtr->GetAppState() == EWSAppState::Racing
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}

	EVisibility GetReplayButtonVisibility() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr && GameModePtr->IsJumpEvent()
			? EVisibility::Collapsed : EVisibility::Visible;
	}

	EVisibility GetJumpButtonVisibility() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		// Armed for the whole approach: taking off too early is a legal
		// decision that simply costs distance, so the button must let the
		// player make it — and get it wrong.
		return GameModePtr && GameModePtr->IsJumpEvent()
			&& GameModePtr->GetPhase() == EWSEventPhase::Active
			? EVisibility::Visible : EVisibility::Collapsed;
	}

	FReply OnTakeoff()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->PlayerTakeoff();
		}
		return FReply::Handled();
	}

	/** The series so far, in metres, with fouls shown as fouls. */
	FText GetJumpSeriesText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr || !GameModePtr->IsJumpEvent())
		{
			return FText::GetEmpty();
		}
		FString Text = GameModePtr->GetAttemptSummary();
		if (GameModePtr->GetBestMark() > 0.0f)
		{
			Text += FString::Printf(TEXT("Best %.2f m"), GameModePtr->GetBestMark());
		}
		return FText::FromString(Text);
	}

	FText GetPaceText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		AWSSprintRunner* Runner = GameModePtr ? GameModePtr->GetPlayerRunner() : nullptr;
		if (!Runner || !Runner->IsPaceEvent())
		{
			return FText::GetEmpty();
		}
		const FWSPaceState& Pace = Runner->GetPaceState();
		// The judgement the event is built on, stated plainly: how hard you
		// are running against how hard you can keep running. Above the line
		// costs the tank; below it slowly refills.
		const TCHAR* Verdict = Pace.bWalled
			? TEXT("EMPTY")
			: (Pace.Effort > Pace.SustainableEffort + 0.03 ? TEXT("BURNING")
				: (Pace.Effort < Pace.SustainableEffort - 0.03 ? TEXT("EASING")
					: TEXT("HOLDING")));
		return FText::FromString(FString::Printf(
			TEXT("%s   effort %d%%   sustainable %d%%%s"),
			Verdict,
			FMath::RoundToInt(Pace.Effort * 100.0),
			FMath::RoundToInt(Pace.SustainableEffort * 100.0),
			Pace.bKicked ? TEXT("   KICKED") : TEXT("")));
	}

	/** Marker offset encodes cadence error: left = slow, right = fast. */
	FOptionalSize GetMarkerOffset() const
	{
		constexpr float BandWidth = 520.0f;
		constexpr float Centre = BandWidth * 0.5f - 7.0f; // half the marker
		const FWSRaceState* State = PlayerState();
		if (!State || State->TargetCadenceHz <= 0.0)
		{
			return FOptionalSize(Centre);
		}
		const double Error = State->ActualCadenceHz - State->TargetCadenceHz;
		const double Normalized = FMath::Clamp(Error / 1.2, -1.0, 1.0);
		return FOptionalSize(
			FMath::Clamp(Centre + static_cast<float>(Normalized) * Centre, 0.0f, BandWidth - 14.0f));
	}

	FSlateColor GetBandColor() const
	{
		const FWSRaceState* State = PlayerState();
		const float Accuracy = State ? static_cast<float>(State->CadenceAccuracy) : 0.0f;
		// Colour reinforces; position and the label carry the meaning.
		return FSlateColor(FMath::Lerp(
			FLinearColor(1.0f, 0.55f, 0.2f), FLinearColor(0.3f, 1.0f, 0.45f), Accuracy));
	}

	FText GetBandLabel() const
	{
		const FWSRaceState* State = PlayerState();
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
		const FWSRaceState* State = PlayerState();
		if (!State || !State->bReleased || State->bFinished)
		{
			return FText::GetEmpty();
		}
		const int32 Percent = FMath::RoundToInt((1.0 - State->Fatigue) * 100.0);
		AWSSprintGameMode* GameModePtr = Mode();
		// In a paced race this bar is the energy budget, not general
		// freshness, and calling it the same thing would mislead.
		const TCHAR* Label = GameModePtr && GameModePtr->IsPaceEvent()
			? TEXT("Tank") : TEXT("Stamina");
		return FText::FromString(FString::Printf(TEXT("%s %d%%"), Label, Percent));
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
			// The segment length is the EVENT's, not a hardcoded 10m. A 400m
			// splits every 50m, and labelling those 10m/20m/30m told the
			// player their race was something it was not.
			const double Segment = Runner->GetSplitSegmentMetres();
			Text += FString::Printf(TEXT("  %.0fm %.2f"),
				Segment * (Index + 1), Splits[Index]);
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
		if (GameModePtr->IsJumpEvent())
		{
			// A field event has no finishing position and no time. Saying
			// "0th --.--" is race language borrowed for something that is
			// not a race; the result of a jump is a MARK.
			const float Best = GameModePtr->GetBestMark();
			return Best > 0.0f
				? FText::FromString(FString::Printf(TEXT("%.2f m"), Best))
				: LOCTEXT("NoMark", "No mark");
		}
		const FWSRaceOutcome Outcome = Runner->GetOutcome();
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
			GameModePtr->StartQuickPlay();
		}
		return FReply::Handled();
	}

	// -- Screen visibility ----------------------------------------------

	EVisibility GetScreenVisibility() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr && GameModePtr->GetAppState() != EWSAppState::Racing
			? EVisibility::SelfHitTestInvisible
			: EVisibility::Collapsed;
	}

	EVisibility VisibleWhen(EWSAppState State) const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr && GameModePtr->GetAppState() == State
			? EVisibility::SelfHitTestInvisible
			: EVisibility::Collapsed;
	}

	EVisibility GetMenuVisibility() const { return VisibleWhen(EWSAppState::Menu); }
	EVisibility GetSignInVisibility() const { return VisibleWhen(EWSAppState::SignIn); }
	EVisibility GetLeaderboardVisibility() const { return VisibleWhen(EWSAppState::Leaderboard); }
	EVisibility GetSettingsVisibility() const { return VisibleWhen(EWSAppState::Settings); }

	EVisibility GetPauseButtonVisibility() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr || GameModePtr->GetAppState() != EWSAppState::Racing ||
			GameModePtr->IsPaused())
		{
			return EVisibility::Collapsed;
		}
		const EWSEventPhase Phase = GameModePtr->GetPhase();
		return (Phase == EWSEventPhase::Ready || Phase == EWSEventPhase::Active)
			? EVisibility::Visible
			: EVisibility::Collapsed;
	}

	EVisibility GetPauseOverlayVisibility() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr && GameModePtr->IsPaused()
			? EVisibility::SelfHitTestInvisible
			: EVisibility::Collapsed;
	}

	// -- Screen content --------------------------------------------------

	FText GetBoardTitle() const
	{
		// Which board this is. Labelling every event's board "100m" would
		// misreport whose times the player is reading.
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr
			? FText::FromString(FString::Printf(TEXT("%s · Global · All time"),
				*GameModePtr->GetSelectedEventName()))
			: FText::GetEmpty();
	}

	FText GetTitleText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr
			? FText::FromString(GameModePtr->GetSelectedEventName().ToUpper())
			: FText::GetEmpty();
	}

	FText GetEventText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr
			? FText::FromString(GameModePtr->GetSelectedEventName())
			: FText::GetEmpty();
	}

	FReply OnPrevEvent()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->CycleEvent(-1);
		}
		return FReply::Handled();
	}

	FReply OnNextEvent()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->CycleEvent(1);
		}
		return FReply::Handled();
	}

	FText GetAccountLine() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr)
		{
			return FText::GetEmpty();
		}
		// Never imply results are being recorded when they are not.
		return GameModePtr->IsSignedIn()
			? FText::FromString(FString::Printf(
				TEXT("Signed in as %s — results count"), *GameModePtr->GetSignedInName()))
			: LOCTEXT("NotSignedIn", "Not signed in — races are practice only");
	}

	FText GetAccountStatus() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr ? FText::FromString(GameModePtr->GetAccountStatus()) : FText::GetEmpty();
	}

	FText GetLeaderboardStatus() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr ? FText::FromString(GameModePtr->GetLeaderboardStatus()) : FText::GetEmpty();
	}

	FText GetLeaderboardText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr)
		{
			return FText::GetEmpty();
		}
		FString Text;
		for (const FWSLeaderboardRow& Row : GameModePtr->GetLeaderboard())
		{
			Text += FString::Printf(TEXT("%3d.  %-22s  %8s  %s\n"),
				Row.Rank, *Row.AthleteName, *Row.ValueText, *Row.Country);
		}
		return FText::FromString(Text);
	}

	FText GetQualityText() const
	{
		const UWSDeviceProfileSubsystem* Device = DeviceProfile();
		if (!Device)
		{
			return FText::GetEmpty();
		}
		switch (Device->GetTier())
		{
		case EWSDeviceTier::Low:  return LOCTEXT("TierLow", "Quality: Low (30 fps target)");
		case EWSDeviceTier::High: return LOCTEXT("TierHigh", "Quality: High (60 fps target)");
		default:                  return LOCTEXT("TierMid", "Quality: Medium (30 fps target)");
		}
	}

	UWSDeviceProfileSubsystem* DeviceProfile() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		UGameInstance* GameInstance = GameModePtr ? GameModePtr->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UWSDeviceProfileSubsystem>() : nullptr;
	}

	// -- Screen actions --------------------------------------------------

	FReply OnQuickPlay()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->StartQuickPlay();
		}
		return FReply::Handled();
	}

	EVisibility GetCareerVisibility() const { return VisibleWhen(EWSAppState::Career); }
	EVisibility GetCreateAthleteVisibility() const { return VisibleWhen(EWSAppState::CreateAthlete); }
	EVisibility GetTrainingVisibility() const { return VisibleWhen(EWSAppState::Training); }

	FText GetCareerStatus() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr ? FText::FromString(GameModePtr->GetCareerStatus()) : FText::GetEmpty();
	}

	FText GetCareerSummary() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (!GameModePtr)
		{
			return FText::GetEmpty();
		}
		// No athlete is stated plainly rather than drawn as a row of zeros.
		return GameModePtr->HasCareerAthlete()
			? FText::FromString(GameModePtr->GetCareerSummary())
			: LOCTEXT("NoAthlete", "No career athlete yet - create one to train and rank.");
	}

	FText GetRecordsText() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr ? FText::FromString(GameModePtr->GetCareerRecordsText()) : FText::GetEmpty();
	}

	FText GetDrillPrompt() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr ? FText::FromString(GameModePtr->GetDrillPrompt()) : FText::GetEmpty();
	}

	FText GetDrillResult() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr ? FText::FromString(GameModePtr->GetDrillResult()) : FText::GetEmpty();
	}

	FReply OnCareer() { return Show(EWSAppState::Career); }

	FReply OnShowCreateAthlete() { return Show(EWSAppState::CreateAthlete); }

	EVisibility GetTournamentVisibility() const { return VisibleWhen(EWSAppState::Tournament); }

	FText GetTournamentStatus() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr ? FText::FromString(GameModePtr->GetTournamentStatus()) : FText::GetEmpty();
	}

	FText GetTournamentSummary() const
	{
		AWSSprintGameMode* GameModePtr = Mode();
		return GameModePtr ? FText::FromString(GameModePtr->GetTournamentSummary()) : FText::GetEmpty();
	}

	FReply OnTournament() { return Show(EWSAppState::Tournament); }

	FReply OnEnterTournament()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->EnterTournament();
		}
		return FReply::Handled();
	}

	FReply OnRaceRound()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->RaceTournamentRound();
		}
		return FReply::Handled();
	}

	FReply OnTrain()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->StartReactionDrill();
		}
		return FReply::Handled();
	}

	void OnDrillPress()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->DrillPress();
		}
	}

	void OnDrillRelease()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->DrillRelease();
		}
	}

	FReply CreateAthleteWith(const TCHAR* Gender)
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (GameModePtr && AthleteNameBox.IsValid())
		{
			GameModePtr->CreateAthlete(AthleteNameBox->GetText().ToString(), Gender);
		}
		return FReply::Handled();
	}

	FReply OnGenderF() { return CreateAthleteWith(TEXT("F")); }
	FReply OnGenderM() { return CreateAthleteWith(TEXT("M")); }
	FReply OnGenderX() { return CreateAthleteWith(TEXT("X")); }

	FReply OnLeaderboard() { return Show(EWSAppState::Leaderboard); }
	FReply OnAccount() { return Show(EWSAppState::SignIn); }
	FReply OnSettings() { return Show(EWSAppState::Settings); }
	FReply OnBackToMenu() { return Show(EWSAppState::Menu); }

	FReply Show(EWSAppState State)
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->ShowScreen(State);
		}
		return FReply::Handled();
	}

	FReply OnSignIn() { return Authenticate(/*bRegister=*/false); }
	FReply OnRegister() { return Authenticate(/*bRegister=*/true); }

	FReply Authenticate(bool bRegister)
	{
		AWSSprintGameMode* GameModePtr = Mode();
		if (GameModePtr && EmailBox.IsValid() && PasswordBox.IsValid())
		{
			// The player typed these on their own device; they go straight to
			// the backend over the configured connection and are never stored.
			GameModePtr->SubmitCredentials(
				EmailBox->GetText().ToString(),
				PasswordBox->GetText().ToString(),
				NameBox.IsValid() ? NameBox->GetText().ToString() : FString(),
				bRegister);
			PasswordBox->SetText(FText::GetEmpty());
		}
		return FReply::Handled();
	}

	FReply OnSignOut()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->SignOut();
		}
		return FReply::Handled();
	}

	FReply OnPause()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->SetPaused(true);
		}
		return FReply::Handled();
	}

	FReply OnResume()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->SetPaused(false);
		}
		return FReply::Handled();
	}

	FReply OnQuitToMenu()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->ReturnToMenu();
		}
		return FReply::Handled();
	}

	/** Absorbs presses on the pause backdrop so they never reach the race. */
	FReply OnSwallowClick() { return FReply::Handled(); }

	FReply OnReplay()
	{
		if (AWSSprintGameMode* GameModePtr = Mode())
		{
			GameModePtr->PlayFinishReplay();
		}
		return FReply::Handled();
	}

	FReply SetTier(EWSDeviceTier Tier)
	{
		if (UWSDeviceProfileSubsystem* Device = DeviceProfile())
		{
			Device->OverrideTier(Tier);
		}
		return FReply::Handled();
	}

	FReply OnQualityLow() { return SetTier(EWSDeviceTier::Low); }
	FReply OnQualityMid() { return SetTier(EWSDeviceTier::Mid); }
	FReply OnQualityHigh() { return SetTier(EWSDeviceTier::High); }

	TWeakObjectPtr<AWSSprintGameMode> GameMode;
	TSharedPtr<SEditableTextBox> EmailBox;
	TSharedPtr<SEditableTextBox> PasswordBox;
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SEditableTextBox> AthleteNameBox;
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
