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
	uint32 Seed, int32 InLaneIndex, const FString& InDisplayName, bool bInIsPlayer)
{
	// Every runner in a race shares the seed, so wind is the same for all —
	// a race where opponents ran in different weather would be a lie.
	Simulation = MakeShared<FWSSprintSimulation>(InAttributes, Seed);
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

void AWSSprintRunner::AdvanceTo(double RaceTime)
{
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
