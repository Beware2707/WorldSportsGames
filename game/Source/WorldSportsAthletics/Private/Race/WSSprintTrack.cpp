#include "Race/WSSprintTrack.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AWSSprintTrack::AWSSprintTrack()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	// Static children cannot attach to a movable root — without this the
	// engine silently drops the blocks and the far distance markers.
	Root->SetMobility(EComponentMobility::Static);

	// Track surface, extended past the line so the run-out is not a void.
	const float SurfaceHalfX = (MaxTrackLengthCm + ApronCm) * 0.5f;
	const float SurfaceHalfY = LaneCount * LaneWidthCm * 0.5f + 60.0f;
	MakeBox(TEXT("Surface"),
		FVector(SurfaceHalfX - ApronCm * 0.5f, 0.0f, -10.0f),
		FVector(SurfaceHalfX, SurfaceHalfY, 10.0f),
		FLinearColor(0.42f, 0.10f, 0.07f)); // track red

	// Infield either side, so the horizon is not black nothing.
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		MakeBox(*FString::Printf(TEXT("Infield%d"), Side),
			FVector(SurfaceHalfX - ApronCm * 0.5f,
				Side * (SurfaceHalfY + 2000.0f), -12.0f),
			FVector(SurfaceHalfX + 4000.0f, 2000.0f, 10.0f),
			FLinearColor(0.06f, 0.22f, 0.07f)); // grass
	}

	// Lane separator lines.
	for (int32 Line = 0; Line <= LaneCount; ++Line)
	{
		const float Y = (Line - LaneCount * 0.5f) * LaneWidthCm;
		MakeBox(*FString::Printf(TEXT("LaneLine%d"), Line),
			FVector(SurfaceHalfX - ApronCm * 0.5f, Y, 0.5f),
			FVector(SurfaceHalfX, 2.5f, 0.6f),
			FLinearColor(0.85f, 0.85f, 0.85f));
	}

	// Start and finish lines: the two marks a player actually reads.
	MakeBox(TEXT("StartLine"), FVector(0.0f, 0.0f, 0.6f),
		FVector(5.0f, SurfaceHalfY, 0.7f), FLinearColor::White);
	FinishLine = MakeBox(TEXT("FinishLine"), FVector(10000.0f, 0.0f, 0.6f),
		FVector(5.0f, SurfaceHalfY, 0.7f), FLinearColor::White);
	FinishLine->SetMobility(EComponentMobility::Movable);

	// Distance markers along the infield edge — the split geometry made
	// visible, so a player can see where their splits came from. They are
	// placed per race, because a 400m's splits are not a 100m's.
	for (int32 Mark = 1; Mark <= MaxDistanceMarks; ++Mark)
	{
		UStaticMeshComponent* Box = MakeBox(*FString::Printf(TEXT("Mark%d"), Mark),
			FVector(Mark * 1000.0f, -(SurfaceHalfY + 40.0f), 0.6f),
			FVector(4.0f, 30.0f, 0.7f),
			FLinearColor(0.7f, 0.7f, 0.72f));
		Box->SetMobility(EComponentMobility::Movable);
		DistanceMarks.Add(Box);
	}

	// Blocks, one per lane, just behind the start line.
	for (int32 Lane = 0; Lane < LaneCount; ++Lane)
	{
		MakeBox(*FString::Printf(TEXT("Blocks%d"), Lane),
			FVector(-45.0f, LaneCenterY(Lane), 6.0f),
			FVector(25.0f, 20.0f, 6.0f),
			FLinearColor(0.05f, 0.15f, 0.45f));
	}

	// Backdrop walls instead of a sky atmosphere: SkyAtmosphere needs a sky
	// mesh and an IsSky material to render, and without them UE prints a
	// red error over the running game. Boxes need no content at all, and a
	// hazy horizon reads better than a black void.
	const float BackdropHeight = 3000.0f;
	MakeBox(TEXT("BackdropFar"),
		FVector(MaxTrackLengthCm + 9000.0f, 0.0f, BackdropHeight - 400.0f),
		FVector(50.0f, 30000.0f, BackdropHeight),
		FLinearColor(0.40f, 0.52f, 0.68f));
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		MakeBox(*FString::Printf(TEXT("BackdropSide%d"), Side),
			FVector(SurfaceHalfX, Side * 9000.0f, BackdropHeight - 400.0f),
			FVector(30000.0f, 50.0f, BackdropHeight),
			FLinearColor(0.34f, 0.45f, 0.60f));
	}
	MakeBox(TEXT("BackdropBehind"),
		FVector(-6000.0f, 0.0f, BackdropHeight - 400.0f),
		FVector(50.0f, 30000.0f, BackdropHeight),
		FLinearColor(0.34f, 0.45f, 0.60f));

	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLight->SetupAttachment(Root);
	SunLight->SetRelativeRotation(FRotator(-48.0f, 35.0f, 0.0f));
	SunLight->SetIntensity(4.0f);
	SunLight->SetMobility(EComponentMobility::Movable);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetIntensity(1.6f);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bLowerHemisphereIsBlack = false;
}

void AWSSprintTrack::SetRaceDistance(float DistanceMetres, int32 SplitCount)
{
	const float FinishX = FMath::Max(DistanceMetres, 1.0f) * 100.0f;
	if (FinishLine)
	{
		const FVector Current = FinishLine->GetRelativeLocation();
		FinishLine->SetRelativeLocation(FVector(FinishX, Current.Y, Current.Z));
	}

	// One marker per split boundary, the finish excluded — it already has a
	// line. Any marker the event does not need is hidden rather than left
	// standing somewhere the race never reaches.
	const int32 Wanted = FMath::Clamp(SplitCount - 1, 0, DistanceMarks.Num());
	const float Segment = SplitCount > 0 ? FinishX / SplitCount : FinishX;
	for (int32 Index = 0; Index < DistanceMarks.Num(); ++Index)
	{
		UStaticMeshComponent* Mark = DistanceMarks[Index];
		if (!Mark)
		{
			continue;
		}
		const bool bUsed = Index < Wanted;
		Mark->SetVisibility(bUsed);
		if (bUsed)
		{
			const FVector Current = Mark->GetRelativeLocation();
			Mark->SetRelativeLocation(
				FVector(Segment * (Index + 1), Current.Y, Current.Z));
		}
	}
}

float AWSSprintTrack::GetFinishLineX() const
{
	return FinishLine ? FinishLine->GetRelativeLocation().X : 0.0f;
}

TArray<float> AWSSprintTrack::GetVisibleMarkPositions() const
{
	TArray<float> Positions;
	for (const UStaticMeshComponent* Mark : DistanceMarks)
	{
		if (Mark && Mark->GetVisibleFlag())
		{
			Positions.Add(Mark->GetRelativeLocation().X);
		}
	}
	return Positions;
}

UStaticMeshComponent* AWSSprintTrack::MakeBox(const TCHAR* Name,
	const FVector& Center, const FVector& Extent, const FLinearColor& Color)
{
	// The engine's 1x1x1m cube scaled to the wanted extent: no imported
	// content, so the level is pure code and reviewable in a diff.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterial> BaseMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	UStaticMeshComponent* Box = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Box->SetupAttachment(GetRootComponent());
	if (CubeMesh.Succeeded())
	{
		Box->SetStaticMesh(CubeMesh.Object);
	}
	Box->SetRelativeLocation(Center);
	// The basic cube is 100 uu across, so extent/50 gives the half-size.
	Box->SetRelativeScale3D(Extent / 50.0f);
	Box->SetMobility(EComponentMobility::Static);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetCastShadow(false);

	if (BaseMaterial.Succeeded())
	{
		UMaterialInstanceDynamic* Dynamic =
			UMaterialInstanceDynamic::Create(BaseMaterial.Object, Box);
		Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
		Box->SetMaterial(0, Dynamic);
	}
	return Box;
}
