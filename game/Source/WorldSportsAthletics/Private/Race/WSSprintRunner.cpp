#include "Race/WSSprintRunner.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Race/WSSprintTrack.h"
#include "UObject/ConstructorHelpers.h"

AWSSprintRunner::AWSSprintRunner()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	if (CylinderMesh.Succeeded())
	{
		Body->SetStaticMesh(CylinderMesh.Object);
	}
	Body->SetRelativeScale3D(FVector(0.35f, 0.35f, 0.85f));
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, 85.0f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Head->SetupAttachment(Root);
	if (SphereMesh.Succeeded())
	{
		Head->SetStaticMesh(SphereMesh.Object);
	}
	Head->SetRelativeScale3D(FVector(0.24f));
	Head->SetRelativeLocation(FVector(0.0f, 0.0f, 180.0f));
	Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Asset lookups are only legal inside a constructor, so the material is
	// resolved here and only its colour parameter is set per race.
	static ConstructorHelpers::FObjectFinder<UMaterial> BaseMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMaterial.Succeeded())
	{
		BodyMaterial = BaseMaterial.Object;
	}
}

void AWSSprintRunner::InitializeRace(const FWSSprintAttributes& InAttributes,
	uint32 Seed, int32 InLaneIndex, const FString& InDisplayName, bool bInIsPlayer,
	const FWSSprintEventSpec& InEventSpec)
{
	// Every runner in a race shares the seed AND the event, so wind and
	// distance are the same for all — a race where opponents ran a different
	// distance, or in different weather, would be a lie.
	Simulation = MakeShared<FWSSprintSimulation>(InAttributes, Seed, InEventSpec);
	SimulatedTime = Simulation->GetState().RaceTime;
	LaneIndex = InLaneIndex;
	DisplayName = InDisplayName;
	bIsPlayer = bInIsPlayer;

	SetActorLocation(FVector(-40.0f, AWSSprintTrack::LaneCenterY(LaneIndex), 0.0f));
	ApplyKitColour();
}

void AWSSprintRunner::ApplyKitColour()
{
	if (!BodyMaterial)
	{
		return;
	}
	// The player is unmistakable; opponents share a neutral colour so no
	// one reads a rival's kit as a difficulty tell.
	const FLinearColor Color = bIsPlayer
		? FLinearColor(0.05f, 0.55f, 1.0f)
		: FLinearColor(0.75f, 0.72f, 0.68f);
	for (UStaticMeshComponent* Part : {Body.Get(), Head.Get()})
	{
		UMaterialInstanceDynamic* Dynamic =
			UMaterialInstanceDynamic::Create(BodyMaterial, Part);
		Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
		Part->SetMaterial(0, Dynamic);
	}
}

void AWSSprintRunner::InitializePaceRace(const FWSSprintAttributes& InAttributes,
	uint32 Seed, int32 InLaneIndex, const FString& InDisplayName, bool bInIsPlayer,
	const FWSPaceEventSpec& InPaceSpec)
{
	PaceSimulation = MakeShared<FWSMiddleDistanceSimulation>(InAttributes, Seed, InPaceSpec);
	SimulatedTime = 0.0;
	LaneIndex = InLaneIndex;
	DisplayName = InDisplayName;
	bIsPlayer = bInIsPlayer;

	SetActorLocation(FVector(-40.0f, AWSSprintTrack::LaneCenterY(LaneIndex), 0.0f));
	ApplyKitColour();
}

void AWSSprintRunner::PushPaceInput(const FWSPaceInputEvent& Event)
{
	if (PaceSimulation.IsValid())
	{
		PaceSimulation->AddInput(Event);
	}
}

void AWSSprintRunner::PushPaceTrace(const TArray<FWSPaceInputEvent>& Trace)
{
	for (const FWSPaceInputEvent& Event : Trace)
	{
		PushPaceInput(Event);
	}
}

const FWSPaceState& AWSSprintRunner::GetPaceState() const
{
	static const FWSPaceState Empty;
	return PaceSimulation.IsValid() ? PaceSimulation->GetState() : Empty;
}

void AWSSprintRunner::DriveVisual(int32 InLaneIndex, const FString& InDisplayName,
	double DistanceMetres, double SpeedMetresPerSecond, bool bAirborne)
{
	bFieldEvent = true;
	bIsPlayer = true;
	LaneIndex = InLaneIndex;
	DisplayName = InDisplayName;
	FieldState.Distance = DistanceMetres;
	FieldState.Speed = SpeedMetresPerSecond;
	FieldState.bReleased = true;
	FieldState.bFinished = bAirborne;
	ApplyKitColour();
	UpdateVisual(FieldState.RaceTime);
}

const FWSRaceState& AWSSprintRunner::GetState() const
{
	if (bFieldEvent)
	{
		return FieldState;
	}
	if (ScriptedFinishSeconds > 0.0)
	{
		return ScriptedState;
	}
	if (PaceSimulation.IsValid())
	{
		return PaceProjectedState;
	}
	return Simulation->GetState();
}

FWSRaceOutcome AWSSprintRunner::GetOutcome() const
{
	if (bFieldEvent)
	{
		// A field event has no race outcome: its result is a MARK, and the
		// game mode owns it. Reaching for a simulation that was never
		// created here is what crashed the first jump.
		return FWSRaceOutcome();
	}
	if (ScriptedFinishSeconds > 0.0)
	{
		return ScriptedOutcome;
	}
	if (PaceSimulation.IsValid())
	{
		const FWSPaceOutcome& Pace = PaceSimulation->GetOutcome();
		FWSRaceOutcome Outcome;
		Outcome.bFinished = Pace.bFinished;
		Outcome.TimeSeconds = Pace.TimeSeconds;
		Outcome.Splits = Pace.Splits;
		// No blocks and no measured wind in these events, so both stay at
		// zero: absent, not invented. The submit path sends neither.
		Outcome.ReactionMs = 0.0;
		Outcome.Wind = 0.0;
		return Outcome;
	}
	return Simulation->GetOutcome();
}

double AWSSprintRunner::GetSplitSegmentMetres() const
{
	if (bFieldEvent)
	{
		return 0.0; // a jump has no splits
	}
	if (PaceSimulation.IsValid())
	{
		const FWSPaceEventSpec& Spec = PaceSimulation->GetEvent();
		return Spec.SplitCount > 0 ? Spec.DistanceMetres / Spec.SplitCount : Spec.DistanceMetres;
	}
	if (!Simulation.IsValid() || Simulation->GetEvent().SplitCount <= 0)
	{
		return 10.0;
	}
	return Simulation->GetRaceDistance() / Simulation->GetEvent().SplitCount;
}

double AWSSprintRunner::GetRaceDistance() const
{
	if (bFieldEvent)
	{
		return FieldState.Distance;
	}
	if (PaceSimulation.IsValid())
	{
		return PaceSimulation->GetRaceDistance();
	}
	return Simulation.IsValid() ? Simulation->GetRaceDistance() : ScriptedDistanceMetres;
}

void AWSSprintRunner::PushInput(const FWSSprintInputEvent& Event)
{
	if (Simulation.IsValid())
	{
		Simulation->AddInput(Event);
	}
}

void AWSSprintRunner::PushTrace(const TArray<FWSSprintInputEvent>& Trace)
{
	for (const FWSSprintInputEvent& Event : Trace)
	{
		PushInput(Event);
	}
}

void AWSSprintRunner::SetScriptedFinish(double FinishTimeSeconds)
{
	// The distance the scripted rival covers is the event's, not the 100m's.
	ScriptedDistanceMetres = Simulation.IsValid()
		? Simulation->GetRaceDistance()
		: 100.0;
	ScriptedFinishSeconds = FMath::Max(FinishTimeSeconds, 0.01);
	ScriptedOutcome = FWSRaceOutcome();
	ScriptedOutcome.TimeSeconds = ScriptedFinishSeconds;
	// A rival's reaction is not modelled; reporting a made-up one would be
	// inventing detail the server never sent.
	ScriptedOutcome.ReactionMs = 0.0;
	ScriptedState = FWSRaceState();
}

void AWSSprintRunner::AdvanceTo(double RaceTime)
{
	if (bFieldEvent)
	{
		return; // driven from outside; there is nothing here to step
	}
	if (ScriptedFinishSeconds > 0.0)
	{
		// Smooth acceleration to the server's time: the finish is exact, the
		// motion in between is presentation only.
		const double Clamped = FMath::Clamp(RaceTime, 0.0, ScriptedFinishSeconds);
		const double Alpha = Clamped / ScriptedFinishSeconds;
		// Ease-out so the runner is quick off the line and holds speed,
		// rather than gliding at a constant rate.
		const double Eased = 1.0 - FMath::Pow(1.0 - Alpha, 1.35);
		ScriptedState.RaceTime = RaceTime;
		ScriptedState.Distance = Eased * ScriptedDistanceMetres;
		ScriptedState.Speed = RaceTime > 0.0 && RaceTime < ScriptedFinishSeconds
			? ScriptedDistanceMetres / ScriptedFinishSeconds
			: 0.0;
		ScriptedState.bReleased = RaceTime > 0.0;
		ScriptedState.bFinished = RaceTime >= ScriptedFinishSeconds;
		ScriptedOutcome.bFinished = ScriptedState.bFinished;
		UpdateVisual(RaceTime);
		return;
	}
	// Fixed-step catch-up: the simulation never sees a frame delta, so a
	// stutter on a phone cannot change anyone's time. The cap stops a long
	// hitch from turning into a lockup.
	//
	// Middle distance needs a bigger cap purely because its races are
	// minutes long: at 120Hz a 1500m is ~25,000 steps, and a 64-step
	// ceiling would let the simulation fall permanently behind the clock.
	if (PaceSimulation.IsValid())
	{
		int32 Steps = 0;
		while (SimulatedTime < RaceTime && Steps < 512)
		{
			if (!PaceSimulation->Step())
			{
				break;
			}
			SimulatedTime = PaceSimulation->GetState().RaceTime;
			++Steps;
		}
		ProjectPaceState();
		UpdateVisual(RaceTime);
		return;
	}
	if (!Simulation.IsValid())
	{
		return;
	}
	int32 Steps = 0;
	while (SimulatedTime < RaceTime && Steps < 64)
	{
		if (!Simulation->Step())
		{
			break;
		}
		SimulatedTime = Simulation->GetState().RaceTime;
		++Steps;
	}
	UpdateVisual(RaceTime);
}

void AWSSprintRunner::ProjectPaceState()
{
	// One race state for the whole game: the HUD, camera and standings read
	// the same shape whatever kind of event is running.
	const FWSPaceState& Pace = PaceSimulation->GetState();
	PaceProjectedState.RaceTime = Pace.RaceTime;
	PaceProjectedState.Distance = Pace.Distance;
	PaceProjectedState.Speed = Pace.Speed;
	// The HUD's "stamina" readout is 1 - fatigue, and in a paced event what
	// it should show is exactly how much of the tank is left.
	PaceProjectedState.Fatigue = 1.0 - Pace.Energy;
	PaceProjectedState.bReleased = true;   // no blocks to leave
	PaceProjectedState.bFinished = Pace.bFinished;
	PaceProjectedState.bFalseStart = false; // no gun to beat
	PaceProjectedState.CadenceAccuracy = 0.0;
	PaceProjectedState.TargetCadenceHz = 0.0;
	PaceProjectedState.ActualCadenceHz = 0.0;
}

void AWSSprintRunner::UpdateVisual(double RaceTime)
{
	const FWSRaceState& State = GetState();
	const float X = static_cast<float>(State.Distance) * 100.0f; // m -> cm

	// Stride bob scales with speed; it reads as effort without animation
	// content, and it is purely cosmetic — never fed back into the sim.
	StridePhase += State.Speed * 0.06;
	const float Bob = static_cast<float>(FMath::Abs(FMath::Sin(StridePhase))) * 6.0f;
	const float Lean = State.bReleased
		? FMath::Clamp(28.0f - static_cast<float>(State.Distance) * 0.9f, 0.0f, 28.0f)
		: 42.0f; // crouched in the blocks

	SetActorLocation(FVector(X, AWSSprintTrack::LaneCenterY(LaneIndex), Bob));
	SetActorRotation(FRotator(0.0f, 0.0f, 0.0f));
	Body->SetRelativeRotation(FRotator(-Lean, 0.0f, 0.0f));
	Head->SetRelativeLocation(FVector(
		FMath::Sin(FMath::DegreesToRadians(Lean)) * 95.0f, 0.0f,
		180.0f - (1.0f - FMath::Cos(FMath::DegreesToRadians(Lean))) * 95.0f));
}
