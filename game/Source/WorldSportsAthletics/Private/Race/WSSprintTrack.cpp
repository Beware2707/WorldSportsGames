#include "Race/WSSprintTrack.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
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
	const float SurfaceHalfX = (TrackLengthCm + ApronCm) * 0.5f;
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
	MakeBox(TEXT("FinishLine"), FVector(TrackLengthCm, 0.0f, 0.6f),
		FVector(5.0f, SurfaceHalfY, 0.7f), FLinearColor::White);

	// 10 m distance markers along the infield edge — the split geometry
	// made visible, so a player can see where their splits came from.
	for (int32 Mark = 1; Mark < 10; ++Mark)
	{
		MakeBox(*FString::Printf(TEXT("Mark%d"), Mark),
			FVector(Mark * 1000.0f, -(SurfaceHalfY + 40.0f), 0.6f),
			FVector(4.0f, 30.0f, 0.7f),
			FLinearColor(0.7f, 0.7f, 0.72f));
	}

	// Blocks, one per lane, just behind the start line.
	for (int32 Lane = 0; Lane < LaneCount; ++Lane)
	{
		MakeBox(*FString::Printf(TEXT("Blocks%d"), Lane),
			FVector(-45.0f, LaneCenterY(Lane), 6.0f),
			FVector(25.0f, 20.0f, 6.0f),
			FLinearColor(0.05f, 0.15f, 0.45f));
	}

	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLight->SetupAttachment(Root);
	SunLight->SetRelativeRotation(FRotator(-48.0f, 35.0f, 0.0f));
	SunLight->SetIntensity(4.0f);
	SunLight->SetMobility(EComponentMobility::Movable);
	// Marks this light as the atmosphere's sun so the sky is lit by it.
	SunLight->SetAtmosphereSunLight(true);

	// Without an atmosphere the horizon is pure black, which reads as a bug
	// rather than as a stadium.
	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	Atmosphere->SetupAttachment(Root);
	Atmosphere->SetMobility(EComponentMobility::Movable);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetIntensity(1.0f);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true; // captures the atmosphere above
	SkyLight->bLowerHemisphereIsBlack = false;
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
