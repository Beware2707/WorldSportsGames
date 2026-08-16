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

	if (BodyMaterial)
	{
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
	ScriptedOutcome = FWSSprintOutcome();
	ScriptedOutcome.TimeSeconds = ScriptedFinishSeconds;
	// A rival's reaction is not modelled; reporting a made-up one would be
	// inventing detail the server never sent.
	ScriptedOutcome.ReactionMs = 0.0;
	ScriptedState = FWSSprintState();
}

void AWSSprintRunner::AdvanceTo(double RaceTime)
{
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
	if (!Simulation.IsValid())
	{
		return;
	}
	// Fixed-step catch-up: the simulation never sees a frame delta, so a
	// stutter on a phone cannot change anyone's time. The cap stops a long
	// hitch from turning into a lockup.
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

void AWSSprintRunner::UpdateVisual(double RaceTime)
{
	const FWSSprintState& State = GetState();
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
