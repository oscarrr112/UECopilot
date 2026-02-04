// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/MaterialGenerator.h"
#include "AssetFactoryModule.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionCeil.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionStep.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionArctangent2.h"

#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"

FGenerationResult FMaterialGenerator::Generate(
	const FString& Name,
	const FString& Path,
	EGenerationAction Action,
	TSharedPtr<FJsonObject> Config)
{
	const bool bExists = DoesAssetExist(Path, Name);

	if (Action == EGenerationAction::Create && bExists)
	{
		return FGenerationResult::MakeSkipped(GetAssetType(), Name, Path, TEXT("Asset already exists"));
	}

	if (Action == EGenerationAction::Update && !bExists)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Asset does not exist for update"));
	}

	// Get template type
	FString Template = GetStringField(Config, TEXT("Template"), TEXT(""));
	if (Template.IsEmpty())
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Missing 'Template' field"));
	}

	// Build full path
	FString FullPath = Path / Name;
	if (!FullPath.StartsWith(TEXT("/")))
	{
		FullPath = TEXT("/") + FullPath;
	}

	UPackage* Package = nullptr;
	UMaterial* Material = nullptr;

	// Handle existing material for Update/CreateOrUpdate
	if (bExists && (Action == EGenerationAction::Update || Action == EGenerationAction::CreateOrUpdate))
	{
		// Load existing material
		Material = LoadObject<UMaterial>(nullptr, *FullPath);
		if (Material)
		{
			Package = Material->GetOutermost();
			Package->FullyLoad();

			// Clear existing expressions
			ClearMaterialExpressions(Material);
		}
		else
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing material"));
		}
	}
	else
	{
		// Create new package and material
		Package = CreatePackage(*FullPath);
		if (!Package)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create package"));
		}
		Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone);
	}

	// Get parameters
	TSharedPtr<FJsonObject> Params = GetObjectField(Config, TEXT("Parameters"));

	// Setup material defaults
	SetupUIMaterialDefaults(Material);

	// Build material based on template
	bool bSuccess = false;
	if (Template.Equals(TEXT("CircularProgress"), ESearchCase::IgnoreCase))
	{
		bSuccess = BuildCircularProgressMaterial(Material, Params);
	}
	else if (Template.Equals(TEXT("CooldownSweep"), ESearchCase::IgnoreCase))
	{
		bSuccess = BuildCooldownSweepMaterial(Material, Params);
	}
	else if (Template.Equals(TEXT("GradientFill"), ESearchCase::IgnoreCase))
	{
		bSuccess = BuildGradientFillMaterial(Material, Params);
	}
	else if (Template.Equals(TEXT("HealthBarFill"), ESearchCase::IgnoreCase))
	{
		bSuccess = BuildHealthBarFillMaterial(Material, Params);
	}
	else
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path,
			FString::Printf(TEXT("Unknown template: %s"), *Template));
	}

	if (!bSuccess)
	{
		return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to build material"));
	}

	// Compile and save
	CompileAndSaveMaterial(Material);

	if (bExists)
	{
		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, Material);
	}
	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, Material);
}

bool FMaterialGenerator::BuildCircularProgressMaterial(UMaterial* Material, TSharedPtr<FJsonObject> Params)
{
	if (!Material)
	{
		return false;
	}

	int32 NodeX = -400;
	int32 NodeY = 0;

	// === Parameters ===

	// Progress parameter (0-1)
	UMaterialExpressionScalarParameter* ProgressParam = NewObject<UMaterialExpressionScalarParameter>(Material);
	ProgressParam->ParameterName = TEXT("Progress");
	ProgressParam->DefaultValue = 0.5f;
	ProgressParam->MaterialExpressionEditorX = NodeX - 200;
	ProgressParam->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(ProgressParam);

	// Color parameter (RGB) - use Constant3Vector for simplicity
	UMaterialExpressionConstant3Vector* ColorParam = NewObject<UMaterialExpressionConstant3Vector>(Material);
	ColorParam->Constant = FLinearColor(0.83f, 0.66f, 0.33f);
	ColorParam->MaterialExpressionEditorX = NodeX - 200;
	ColorParam->MaterialExpressionEditorY = NodeY + 150;
	Material->GetExpressionCollection().AddExpression(ColorParam);

	// Color opacity (separate scalar)
	UMaterialExpressionConstant* ColorOpacity = NewObject<UMaterialExpressionConstant>(Material);
	ColorOpacity->R = 1.0f;
	ColorOpacity->MaterialExpressionEditorX = NodeX - 200;
	ColorOpacity->MaterialExpressionEditorY = NodeY + 225;
	Material->GetExpressionCollection().AddExpression(ColorOpacity);

	// Background color parameter (RGB)
	UMaterialExpressionConstant3Vector* BgColorParam = NewObject<UMaterialExpressionConstant3Vector>(Material);
	BgColorParam->Constant = FLinearColor(0.05f, 0.05f, 0.08f);
	BgColorParam->MaterialExpressionEditorX = NodeX - 200;
	BgColorParam->MaterialExpressionEditorY = NodeY + 300;
	Material->GetExpressionCollection().AddExpression(BgColorParam);

	// Background opacity (separate scalar)
	UMaterialExpressionConstant* BgOpacity = NewObject<UMaterialExpressionConstant>(Material);
	BgOpacity->R = 0.8f;
	BgOpacity->MaterialExpressionEditorX = NodeX - 200;
	BgOpacity->MaterialExpressionEditorY = NodeY + 375;
	Material->GetExpressionCollection().AddExpression(BgOpacity);

	// === Texture Coordinate ===
	UMaterialExpressionTextureCoordinate* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
	TexCoord->MaterialExpressionEditorX = NodeX;
	TexCoord->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(TexCoord);

	// === Calculate UV offset from center (UV - 0.5) ===
	UMaterialExpressionConstant* Half = NewObject<UMaterialExpressionConstant>(Material);
	Half->R = 0.5f;
	Half->MaterialExpressionEditorX = NodeX;
	Half->MaterialExpressionEditorY = NodeY + 100;
	Material->GetExpressionCollection().AddExpression(Half);

	UMaterialExpressionSubtract* UVCentered = NewObject<UMaterialExpressionSubtract>(Material);
	UVCentered->A.Connect(0, TexCoord);
	UVCentered->B.Connect(0, Half);
	UVCentered->MaterialExpressionEditorX = NodeX + 150;
	UVCentered->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(UVCentered);

	// === Get U and V components ===
	UMaterialExpressionComponentMask* MaskU = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskU->R = true; MaskU->G = false; MaskU->B = false; MaskU->A = false;
	MaskU->Input.Connect(0, UVCentered);
	MaskU->MaterialExpressionEditorX = NodeX + 300;
	MaskU->MaterialExpressionEditorY = NodeY - 50;
	Material->GetExpressionCollection().AddExpression(MaskU);

	UMaterialExpressionComponentMask* MaskV = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskV->R = false; MaskV->G = true; MaskV->B = false; MaskV->A = false;
	MaskV->Input.Connect(0, UVCentered);
	MaskV->MaterialExpressionEditorX = NodeX + 300;
	MaskV->MaterialExpressionEditorY = NodeY + 50;
	Material->GetExpressionCollection().AddExpression(MaskV);

	// === Calculate angle using Arctangent2 ===
	UMaterialExpressionArctangent2* Atan2 = NewObject<UMaterialExpressionArctangent2>(Material);
	Atan2->Y.Connect(0, MaskV);
	Atan2->X.Connect(0, MaskU);
	Atan2->MaterialExpressionEditorX = NodeX + 450;
	Atan2->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(Atan2);

	// === Normalize angle to 0-1: (atan2 + PI) / (2*PI) ===
	UMaterialExpressionConstant* Pi = NewObject<UMaterialExpressionConstant>(Material);
	Pi->R = 3.14159f;
	Pi->MaterialExpressionEditorX = NodeX + 450;
	Pi->MaterialExpressionEditorY = NodeY + 100;
	Material->GetExpressionCollection().AddExpression(Pi);

	UMaterialExpressionAdd* AnglePlusPi = NewObject<UMaterialExpressionAdd>(Material);
	AnglePlusPi->A.Connect(0, Atan2);
	AnglePlusPi->B.Connect(0, Pi);
	AnglePlusPi->MaterialExpressionEditorX = NodeX + 600;
	AnglePlusPi->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(AnglePlusPi);

	UMaterialExpressionConstant* TwoPi = NewObject<UMaterialExpressionConstant>(Material);
	TwoPi->R = 6.28318f;
	TwoPi->MaterialExpressionEditorX = NodeX + 600;
	TwoPi->MaterialExpressionEditorY = NodeY + 100;
	Material->GetExpressionCollection().AddExpression(TwoPi);

	UMaterialExpressionDivide* NormalizedAngle = NewObject<UMaterialExpressionDivide>(Material);
	NormalizedAngle->A.Connect(0, AnglePlusPi);
	NormalizedAngle->B.Connect(0, TwoPi);
	NormalizedAngle->MaterialExpressionEditorX = NodeX + 750;
	NormalizedAngle->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(NormalizedAngle);

	// === Rotate so 0 is at top: frac(angle + 0.25) ===
	UMaterialExpressionConstant* Quarter = NewObject<UMaterialExpressionConstant>(Material);
	Quarter->R = 0.25f;
	Quarter->MaterialExpressionEditorX = NodeX + 750;
	Quarter->MaterialExpressionEditorY = NodeY + 100;
	Material->GetExpressionCollection().AddExpression(Quarter);

	UMaterialExpressionAdd* AngleRotated = NewObject<UMaterialExpressionAdd>(Material);
	AngleRotated->A.Connect(0, NormalizedAngle);
	AngleRotated->B.Connect(0, Quarter);
	AngleRotated->MaterialExpressionEditorX = NodeX + 900;
	AngleRotated->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(AngleRotated);

	UMaterialExpressionFrac* AngleFrac = NewObject<UMaterialExpressionFrac>(Material);
	AngleFrac->Input.Connect(0, AngleRotated);
	AngleFrac->MaterialExpressionEditorX = NodeX + 1050;
	AngleFrac->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(AngleFrac);

	// === Compare angle with Progress using Step ===
	// Step(Progress, Angle) = Angle >= Progress ? 1 : 0
	// We want Angle < Progress, so use 1 - Step
	UMaterialExpressionStep* StepNode = NewObject<UMaterialExpressionStep>(Material);
	StepNode->Y.Connect(0, ProgressParam);  // Edge
	StepNode->X.Connect(0, AngleFrac);      // Value
	StepNode->MaterialExpressionEditorX = NodeX + 1200;
	StepNode->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(StepNode);

	UMaterialExpressionOneMinus* ProgressMask = NewObject<UMaterialExpressionOneMinus>(Material);
	ProgressMask->Input.Connect(0, StepNode);
	ProgressMask->MaterialExpressionEditorX = NodeX + 1350;
	ProgressMask->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(ProgressMask);

	// === Circular mask (distance from center < 0.5) ===
	UMaterialExpressionConstant2Vector* Zero2D = NewObject<UMaterialExpressionConstant2Vector>(Material);
	Zero2D->R = 0.0f;
	Zero2D->G = 0.0f;
	Zero2D->MaterialExpressionEditorX = NodeX + 150;
	Zero2D->MaterialExpressionEditorY = NodeY + 250;
	Material->GetExpressionCollection().AddExpression(Zero2D);

	UMaterialExpressionDistance* Dist = NewObject<UMaterialExpressionDistance>(Material);
	Dist->A.Connect(0, UVCentered);
	Dist->B.Connect(0, Zero2D);
	Dist->MaterialExpressionEditorX = NodeX + 300;
	Dist->MaterialExpressionEditorY = NodeY + 200;
	Material->GetExpressionCollection().AddExpression(Dist);

	// distance < 0.5 means inside circle
	UMaterialExpressionStep* CircleStep = NewObject<UMaterialExpressionStep>(Material);
	CircleStep->Y.Connect(0, Dist);  // Value (distance)
	CircleStep->X.Connect(0, Half);  // Edge (0.5)
	CircleStep->MaterialExpressionEditorX = NodeX + 450;
	CircleStep->MaterialExpressionEditorY = NodeY + 200;
	Material->GetExpressionCollection().AddExpression(CircleStep);

	// === Combine masks ===
	UMaterialExpressionMultiply* FinalMask = NewObject<UMaterialExpressionMultiply>(Material);
	FinalMask->A.Connect(0, ProgressMask);
	FinalMask->B.Connect(0, CircleStep);
	FinalMask->MaterialExpressionEditorX = NodeX + 1500;
	FinalMask->MaterialExpressionEditorY = NodeY + 100;
	Material->GetExpressionCollection().AddExpression(FinalMask);

	// === Lerp between BgColor and Color based on mask (RGB only, using Constant3Vector) ===
	UMaterialExpressionLinearInterpolate* ColorLerp = NewObject<UMaterialExpressionLinearInterpolate>(Material);
	ColorLerp->A.Connect(0, BgColorParam);
	ColorLerp->B.Connect(0, ColorParam);
	ColorLerp->Alpha.Connect(0, FinalMask);
	ColorLerp->MaterialExpressionEditorX = NodeX + 1650;
	ColorLerp->MaterialExpressionEditorY = NodeY + 100;
	Material->GetExpressionCollection().AddExpression(ColorLerp);

	// === Lerp between BgOpacity and ColorOpacity based on mask ===
	UMaterialExpressionLinearInterpolate* OpacityLerp = NewObject<UMaterialExpressionLinearInterpolate>(Material);
	OpacityLerp->A.Connect(0, BgOpacity);
	OpacityLerp->B.Connect(0, ColorOpacity);
	OpacityLerp->Alpha.Connect(0, FinalMask);
	OpacityLerp->MaterialExpressionEditorX = NodeX + 1700;
	OpacityLerp->MaterialExpressionEditorY = NodeY + 250;
	Material->GetExpressionCollection().AddExpression(OpacityLerp);

	// === Connect to material outputs ===
	Material->GetEditorOnlyData()->EmissiveColor.Connect(0, ColorLerp);
	Material->GetEditorOnlyData()->Opacity.Connect(0, OpacityLerp);

	return true;
}

bool FMaterialGenerator::BuildCooldownSweepMaterial(UMaterial* Material, TSharedPtr<FJsonObject> Params)
{
	if (!Material)
	{
		return false;
	}

	int32 NodeX = -400;
	int32 NodeY = 0;

	// Progress parameter (0-1, where 1 = fully on cooldown)
	UMaterialExpressionScalarParameter* ProgressParam = NewObject<UMaterialExpressionScalarParameter>(Material);
	ProgressParam->ParameterName = TEXT("Progress");
	ProgressParam->DefaultValue = 0.5f;
	ProgressParam->MaterialExpressionEditorX = NodeX - 200;
	ProgressParam->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(ProgressParam);

	// Overlay color (RGB only)
	UMaterialExpressionVectorParameter* OverlayColor = NewObject<UMaterialExpressionVectorParameter>(Material);
	OverlayColor->ParameterName = TEXT("OverlayColor");
	OverlayColor->DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	OverlayColor->MaterialExpressionEditorX = NodeX - 200;
	OverlayColor->MaterialExpressionEditorY = NodeY + 150;
	Material->GetExpressionCollection().AddExpression(OverlayColor);

	// Overlay opacity (separate scalar for alpha)
	UMaterialExpressionScalarParameter* OverlayOpacity = NewObject<UMaterialExpressionScalarParameter>(Material);
	OverlayOpacity->ParameterName = TEXT("OverlayOpacity");
	OverlayOpacity->DefaultValue = 0.7f;
	OverlayOpacity->MaterialExpressionEditorX = NodeX - 200;
	OverlayOpacity->MaterialExpressionEditorY = NodeY + 250;
	Material->GetExpressionCollection().AddExpression(OverlayOpacity);

	// Texture Coordinate
	UMaterialExpressionTextureCoordinate* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
	TexCoord->MaterialExpressionEditorX = NodeX;
	TexCoord->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(TexCoord);

	// UV - 0.5
	UMaterialExpressionConstant* Half = NewObject<UMaterialExpressionConstant>(Material);
	Half->R = 0.5f;
	Half->MaterialExpressionEditorX = NodeX;
	Half->MaterialExpressionEditorY = NodeY + 100;
	Material->GetExpressionCollection().AddExpression(Half);

	UMaterialExpressionSubtract* UVCentered = NewObject<UMaterialExpressionSubtract>(Material);
	UVCentered->A.Connect(0, TexCoord);
	UVCentered->B.Connect(0, Half);
	UVCentered->MaterialExpressionEditorX = NodeX + 150;
	UVCentered->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(UVCentered);

	// Get U and V
	UMaterialExpressionComponentMask* MaskU = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskU->R = true; MaskU->G = false; MaskU->B = false; MaskU->A = false;
	MaskU->Input.Connect(0, UVCentered);
	MaskU->MaterialExpressionEditorX = NodeX + 300;
	MaskU->MaterialExpressionEditorY = NodeY - 50;
	Material->GetExpressionCollection().AddExpression(MaskU);

	UMaterialExpressionComponentMask* MaskV = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskV->R = false; MaskV->G = true; MaskV->B = false; MaskV->A = false;
	MaskV->Input.Connect(0, UVCentered);
	MaskV->MaterialExpressionEditorX = NodeX + 300;
	MaskV->MaterialExpressionEditorY = NodeY + 50;
	Material->GetExpressionCollection().AddExpression(MaskV);

	// Negate V for clockwise from top
	UMaterialExpressionMultiply* NegV = NewObject<UMaterialExpressionMultiply>(Material);
	NegV->A.Connect(0, MaskV);
	NegV->ConstB = -1.0f;
	NegV->MaterialExpressionEditorX = NodeX + 450;
	NegV->MaterialExpressionEditorY = NodeY + 50;
	Material->GetExpressionCollection().AddExpression(NegV);

	// Atan2(U, -V) for clockwise from top
	UMaterialExpressionArctangent2* Atan2 = NewObject<UMaterialExpressionArctangent2>(Material);
	Atan2->Y.Connect(0, MaskU);
	Atan2->X.Connect(0, NegV);
	Atan2->MaterialExpressionEditorX = NodeX + 600;
	Atan2->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(Atan2);

	// Normalize to 0-1
	UMaterialExpressionConstant* Pi = NewObject<UMaterialExpressionConstant>(Material);
	Pi->R = 3.14159f;
	Pi->MaterialExpressionEditorX = NodeX + 600;
	Pi->MaterialExpressionEditorY = NodeY + 100;
	Material->GetExpressionCollection().AddExpression(Pi);

	UMaterialExpressionAdd* AnglePlusPi = NewObject<UMaterialExpressionAdd>(Material);
	AnglePlusPi->A.Connect(0, Atan2);
	AnglePlusPi->B.Connect(0, Pi);
	AnglePlusPi->MaterialExpressionEditorX = NodeX + 750;
	AnglePlusPi->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(AnglePlusPi);

	UMaterialExpressionConstant* TwoPi = NewObject<UMaterialExpressionConstant>(Material);
	TwoPi->R = 6.28318f;
	TwoPi->MaterialExpressionEditorX = NodeX + 750;
	TwoPi->MaterialExpressionEditorY = NodeY + 100;
	Material->GetExpressionCollection().AddExpression(TwoPi);

	UMaterialExpressionDivide* NormalizedAngle = NewObject<UMaterialExpressionDivide>(Material);
	NormalizedAngle->A.Connect(0, AnglePlusPi);
	NormalizedAngle->B.Connect(0, TwoPi);
	NormalizedAngle->MaterialExpressionEditorX = NodeX + 900;
	NormalizedAngle->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(NormalizedAngle);

	// Step: angle < progress
	UMaterialExpressionStep* StepNode = NewObject<UMaterialExpressionStep>(Material);
	StepNode->Y.Connect(0, ProgressParam);
	StepNode->X.Connect(0, NormalizedAngle);
	StepNode->MaterialExpressionEditorX = NodeX + 1050;
	StepNode->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(StepNode);

	UMaterialExpressionOneMinus* Mask = NewObject<UMaterialExpressionOneMinus>(Material);
	Mask->Input.Connect(0, StepNode);
	Mask->MaterialExpressionEditorX = NodeX + 1200;
	Mask->MaterialExpressionEditorY = NodeY;
	Material->GetExpressionCollection().AddExpression(Mask);

	// Multiply color by mask for final emissive
	UMaterialExpressionMultiply* FinalRGB = NewObject<UMaterialExpressionMultiply>(Material);
	FinalRGB->A.Connect(0, OverlayColor);
	FinalRGB->B.Connect(0, Mask);
	FinalRGB->MaterialExpressionEditorX = NodeX + 1350;
	FinalRGB->MaterialExpressionEditorY = NodeY + 50;
	Material->GetExpressionCollection().AddExpression(FinalRGB);

	// Multiply opacity by mask for final opacity
	UMaterialExpressionMultiply* FinalAlpha = NewObject<UMaterialExpressionMultiply>(Material);
	FinalAlpha->A.Connect(0, OverlayOpacity);
	FinalAlpha->B.Connect(0, Mask);
	FinalAlpha->MaterialExpressionEditorX = NodeX + 1350;
	FinalAlpha->MaterialExpressionEditorY = NodeY + 200;
	Material->GetExpressionCollection().AddExpression(FinalAlpha);

	Material->GetEditorOnlyData()->EmissiveColor.Connect(0, FinalRGB);
	Material->GetEditorOnlyData()->Opacity.Connect(0, FinalAlpha);

	return true;
}

bool FMaterialGenerator::BuildGradientFillMaterial(UMaterial* Material, TSharedPtr<FJsonObject> Params)
{
	if (!Material)
	{
		return false;
	}

	// Start color (RGB)
	UMaterialExpressionVectorParameter* StartColor = NewObject<UMaterialExpressionVectorParameter>(Material);
	StartColor->ParameterName = TEXT("StartColor");
	StartColor->DefaultValue = FLinearColor(0.55f, 0.41f, 0.08f, 1.0f);
	StartColor->MaterialExpressionEditorX = -400;
	StartColor->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(StartColor);

	// End color (RGB)
	UMaterialExpressionVectorParameter* EndColor = NewObject<UMaterialExpressionVectorParameter>(Material);
	EndColor->ParameterName = TEXT("EndColor");
	EndColor->DefaultValue = FLinearColor(0.96f, 0.83f, 0.52f, 1.0f);
	EndColor->MaterialExpressionEditorX = -400;
	EndColor->MaterialExpressionEditorY = 150;
	Material->GetExpressionCollection().AddExpression(EndColor);

	// Opacity parameter
	UMaterialExpressionScalarParameter* Opacity = NewObject<UMaterialExpressionScalarParameter>(Material);
	Opacity->ParameterName = TEXT("Opacity");
	Opacity->DefaultValue = 1.0f;
	Opacity->MaterialExpressionEditorX = -400;
	Opacity->MaterialExpressionEditorY = 250;
	Material->GetExpressionCollection().AddExpression(Opacity);

	// Texture coordinate
	UMaterialExpressionTextureCoordinate* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
	TexCoord->MaterialExpressionEditorX = -400;
	TexCoord->MaterialExpressionEditorY = 350;
	Material->GetExpressionCollection().AddExpression(TexCoord);

	// Get U for horizontal gradient
	UMaterialExpressionComponentMask* UMask = NewObject<UMaterialExpressionComponentMask>(Material);
	UMask->R = true; UMask->G = false; UMask->B = false; UMask->A = false;
	UMask->Input.Connect(0, TexCoord);
	UMask->MaterialExpressionEditorX = -200;
	UMask->MaterialExpressionEditorY = 350;
	Material->GetExpressionCollection().AddExpression(UMask);

	// Lerp colors
	UMaterialExpressionLinearInterpolate* Lerp = NewObject<UMaterialExpressionLinearInterpolate>(Material);
	Lerp->A.Connect(0, StartColor);
	Lerp->B.Connect(0, EndColor);
	Lerp->Alpha.Connect(0, UMask);
	Lerp->MaterialExpressionEditorX = 0;
	Lerp->MaterialExpressionEditorY = 100;
	Material->GetExpressionCollection().AddExpression(Lerp);

	// Connect directly to outputs (Lerp is already RGB)
	Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Lerp);
	Material->GetEditorOnlyData()->Opacity.Connect(0, Opacity);

	return true;
}

bool FMaterialGenerator::BuildHealthBarFillMaterial(UMaterial* Material, TSharedPtr<FJsonObject> Params)
{
	if (!Material)
	{
		return false;
	}

	// Fill percentage (0-1)
	UMaterialExpressionScalarParameter* FillParam = NewObject<UMaterialExpressionScalarParameter>(Material);
	FillParam->ParameterName = TEXT("FillPercent");
	FillParam->DefaultValue = 1.0f;
	FillParam->MaterialExpressionEditorX = -400;
	FillParam->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(FillParam);

	// Health color
	UMaterialExpressionVectorParameter* HealthColor = NewObject<UMaterialExpressionVectorParameter>(Material);
	HealthColor->ParameterName = TEXT("HealthColor");
	HealthColor->DefaultValue = FLinearColor(0.29f, 0.87f, 0.5f, 1.0f);
	HealthColor->MaterialExpressionEditorX = -400;
	HealthColor->MaterialExpressionEditorY = 150;
	Material->GetExpressionCollection().AddExpression(HealthColor);

	// Texture coordinate
	UMaterialExpressionTextureCoordinate* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
	TexCoord->MaterialExpressionEditorX = -400;
	TexCoord->MaterialExpressionEditorY = 300;
	Material->GetExpressionCollection().AddExpression(TexCoord);

	// Get U for fill comparison
	UMaterialExpressionComponentMask* UMask = NewObject<UMaterialExpressionComponentMask>(Material);
	UMask->R = true; UMask->G = false; UMask->B = false; UMask->A = false;
	UMask->Input.Connect(0, TexCoord);
	UMask->MaterialExpressionEditorX = -200;
	UMask->MaterialExpressionEditorY = 300;
	Material->GetExpressionCollection().AddExpression(UMask);

	// Get V for shine effect
	UMaterialExpressionComponentMask* VMask = NewObject<UMaterialExpressionComponentMask>(Material);
	VMask->R = false; VMask->G = true; VMask->B = false; VMask->A = false;
	VMask->Input.Connect(0, TexCoord);
	VMask->MaterialExpressionEditorX = -200;
	VMask->MaterialExpressionEditorY = 400;
	Material->GetExpressionCollection().AddExpression(VMask);

	// Fill mask: U < FillPercent
	UMaterialExpressionStep* FillStep = NewObject<UMaterialExpressionStep>(Material);
	FillStep->Y.Connect(0, FillParam);
	FillStep->X.Connect(0, UMask);
	FillStep->MaterialExpressionEditorX = 0;
	FillStep->MaterialExpressionEditorY = 200;
	Material->GetExpressionCollection().AddExpression(FillStep);

	UMaterialExpressionOneMinus* FillMask = NewObject<UMaterialExpressionOneMinus>(Material);
	FillMask->Input.Connect(0, FillStep);
	FillMask->MaterialExpressionEditorX = 150;
	FillMask->MaterialExpressionEditorY = 200;
	Material->GetExpressionCollection().AddExpression(FillMask);

	// Shine: (1 - V*2) * 0.3, clamped
	UMaterialExpressionMultiply* VTimes2 = NewObject<UMaterialExpressionMultiply>(Material);
	VTimes2->A.Connect(0, VMask);
	VTimes2->ConstB = 2.0f;
	VTimes2->MaterialExpressionEditorX = 0;
	VTimes2->MaterialExpressionEditorY = 400;
	Material->GetExpressionCollection().AddExpression(VTimes2);

	UMaterialExpressionOneMinus* OneMinusV = NewObject<UMaterialExpressionOneMinus>(Material);
	OneMinusV->Input.Connect(0, VTimes2);
	OneMinusV->MaterialExpressionEditorX = 150;
	OneMinusV->MaterialExpressionEditorY = 400;
	Material->GetExpressionCollection().AddExpression(OneMinusV);

	UMaterialExpressionSaturate* ShineClamped = NewObject<UMaterialExpressionSaturate>(Material);
	ShineClamped->Input.Connect(0, OneMinusV);
	ShineClamped->MaterialExpressionEditorX = 300;
	ShineClamped->MaterialExpressionEditorY = 400;
	Material->GetExpressionCollection().AddExpression(ShineClamped);

	UMaterialExpressionMultiply* ShineScaled = NewObject<UMaterialExpressionMultiply>(Material);
	ShineScaled->A.Connect(0, ShineClamped);
	ShineScaled->ConstB = 0.3f;
	ShineScaled->MaterialExpressionEditorX = 450;
	ShineScaled->MaterialExpressionEditorY = 400;
	Material->GetExpressionCollection().AddExpression(ShineScaled);

	// Add shine to color
	UMaterialExpressionAdd* ColorWithShine = NewObject<UMaterialExpressionAdd>(Material);
	ColorWithShine->A.Connect(0, HealthColor);
	ColorWithShine->B.Connect(0, ShineScaled);
	ColorWithShine->MaterialExpressionEditorX = 300;
	ColorWithShine->MaterialExpressionEditorY = 150;
	Material->GetExpressionCollection().AddExpression(ColorWithShine);

	// Multiply by fill mask
	UMaterialExpressionMultiply* FinalColor = NewObject<UMaterialExpressionMultiply>(Material);
	FinalColor->A.Connect(0, ColorWithShine);
	FinalColor->B.Connect(0, FillMask);
	FinalColor->MaterialExpressionEditorX = 450;
	FinalColor->MaterialExpressionEditorY = 150;
	Material->GetExpressionCollection().AddExpression(FinalColor);

	// RGB
	UMaterialExpressionComponentMask* RGBMask = NewObject<UMaterialExpressionComponentMask>(Material);
	RGBMask->R = true; RGBMask->G = true; RGBMask->B = true; RGBMask->A = false;
	RGBMask->Input.Connect(0, FinalColor);
	RGBMask->MaterialExpressionEditorX = 600;
	RGBMask->MaterialExpressionEditorY = 150;
	Material->GetExpressionCollection().AddExpression(RGBMask);

	Material->GetEditorOnlyData()->EmissiveColor.Connect(0, RGBMask);
	Material->GetEditorOnlyData()->Opacity.Connect(0, FillMask);

	return true;
}

void FMaterialGenerator::SetupUIMaterialDefaults(UMaterial* Material)
{
	Material->MaterialDomain = MD_UI;
	Material->BlendMode = BLEND_Translucent;
	Material->SetShadingModel(MSM_Unlit);
	Material->TwoSided = true;
}

void FMaterialGenerator::ClearMaterialExpressions(UMaterial* Material)
{
	if (!Material)
	{
		return;
	}

	// Disconnect all outputs
	auto* EditorData = Material->GetEditorOnlyData();
	if (EditorData)
	{
		EditorData->EmissiveColor.Expression = nullptr;
		EditorData->EmissiveColor.OutputIndex = 0;
		EditorData->Opacity.Expression = nullptr;
		EditorData->Opacity.OutputIndex = 0;
		EditorData->OpacityMask.Expression = nullptr;
		EditorData->OpacityMask.OutputIndex = 0;
		EditorData->BaseColor.Expression = nullptr;
		EditorData->BaseColor.OutputIndex = 0;
		EditorData->Metallic.Expression = nullptr;
		EditorData->Specular.Expression = nullptr;
		EditorData->Roughness.Expression = nullptr;
		EditorData->Normal.Expression = nullptr;
	}

	// Get expression collection and destroy all expressions
	FMaterialExpressionCollection& ExpressionCollection = Material->GetExpressionCollection();

	// Mark expressions for garbage collection
	for (UMaterialExpression* Expression : ExpressionCollection.Expressions)
	{
		if (Expression)
		{
			Expression->MarkAsGarbage();
		}
	}

	// Clear collections
	ExpressionCollection.Expressions.Empty();
	ExpressionCollection.ExpressionExecBegin = nullptr;
	ExpressionCollection.ExpressionExecEnd = nullptr;
	ExpressionCollection.EditorComments.Empty();
}

void FMaterialGenerator::CompileAndSaveMaterial(UMaterial* Material)
{
	// Mark for compilation
	Material->PreEditChange(nullptr);
	Material->PostEditChange();

	// Register asset with asset registry
	FAssetRegistryModule::AssetCreated(Material);

	// Save
	Material->MarkPackageDirty();
	UPackage* Package = Material->GetOutermost();
	Package->FullyLoad();
	Package->SetDirtyFlag(true);

	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Material, *PackageFileName, SaveArgs);
}

FLinearColor FMaterialGenerator::ParseColor(TSharedPtr<FJsonValue> ColorValue) const
{
	if (!ColorValue.IsValid())
	{
		return FLinearColor::White;
	}

	const TArray<TSharedPtr<FJsonValue>>* ColorArray;
	if (ColorValue->TryGetArray(ColorArray) && ColorArray->Num() >= 3)
	{
		float R = (*ColorArray)[0]->AsNumber();
		float G = (*ColorArray)[1]->AsNumber();
		float B = (*ColorArray)[2]->AsNumber();
		float A = ColorArray->Num() >= 4 ? (*ColorArray)[3]->AsNumber() : 1.0f;
		return FLinearColor(R, G, B, A);
	}

	return FLinearColor::White;
}

EMaterialDomain FMaterialGenerator::ParseDomain(const FString& DomainString) const
{
	if (DomainString.Equals(TEXT("Surface"), ESearchCase::IgnoreCase))
		return MD_Surface;
	if (DomainString.Equals(TEXT("PostProcess"), ESearchCase::IgnoreCase))
		return MD_PostProcess;
	if (DomainString.Equals(TEXT("UI"), ESearchCase::IgnoreCase))
		return MD_UI;
	return MD_UI;
}

EBlendMode FMaterialGenerator::ParseBlendMode(const FString& BlendString) const
{
	if (BlendString.Equals(TEXT("Opaque"), ESearchCase::IgnoreCase))
		return BLEND_Opaque;
	if (BlendString.Equals(TEXT("Masked"), ESearchCase::IgnoreCase))
		return BLEND_Masked;
	if (BlendString.Equals(TEXT("Additive"), ESearchCase::IgnoreCase))
		return BLEND_Additive;
	return BLEND_Translucent;
}
