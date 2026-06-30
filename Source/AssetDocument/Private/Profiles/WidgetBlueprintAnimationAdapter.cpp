// Copyright ProjectRPG. All Rights Reserved.

#include "Profiles/WidgetBlueprintAnimationAdapter.h"

#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Components/Widget.h"
#include "Dom/JsonValue.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Serialization/JsonSerializer.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
enum class EWidgetAnimationTrackKind : uint8
{
	Float,
	Transform
};

struct FWidgetAnimationKeySpec
{
	int32 Frame = 0;
	float Value = 0.0f;
};

struct FWidgetAnimationChannelSpec
{
	FName Name;
	TArray<FWidgetAnimationKeySpec> Keys;
};

struct FWidgetAnimationTrackSpec
{
	FName Widget;
	FName Property;
	EWidgetAnimationTrackKind Kind = EWidgetAnimationTrackKind::Float;
	TArray<FWidgetAnimationKeySpec> Keys;
	TArray<FWidgetAnimationChannelSpec> Channels;
	FString JsonPath;
};

struct FWidgetAnimationSpec
{
	FName Name;
	FFrameRate FrameRate = FFrameRate(30, 1);
	int32 StartFrame = 0;
	int32 EndFrame = 30;
	TArray<FWidgetAnimationTrackSpec> Tracks;
	FString JsonPath;
};

FAssetDocumentCapabilityResult AnimationFailure(const FString& Message, const FString& Path, const FString& Code)
{
	return FAssetDocumentCapabilityResult::Failure(Message, Path, Code);
}

FString JsonValueToComparableString(const TSharedPtr<FJsonValue>& Value)
{
	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Value.IsValid() ? Value.ToSharedRef() : MakeShared<FJsonValueNull>(), TEXT(""), Writer);
	return JsonText;
}

FAssetDocumentCapabilityResult RequireStringField(
	const TSharedPtr<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	const FString& Code,
	FString& OutValue)
{
	if (!Object.IsValid() || !Object->TryGetStringField(FieldName, OutValue) || OutValue.IsEmpty())
	{
		return AnimationFailure(
			FString::Printf(TEXT("Body.Animations entry requires non-empty %s"), *FieldName),
			Path,
			Code);
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadIntegralNumberField(
	const TSharedPtr<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	const FString& MissingCode,
	const FString& InvalidCode,
	double MinValue,
	double MaxValue,
	int64& OutValue)
{
	const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(FieldName) : nullptr;
	if (!Value.IsValid())
	{
		return AnimationFailure(
			FString::Printf(TEXT("Animation numeric field '%s' is required"), *FieldName),
			Path,
			MissingCode);
	}
	if (Value->Type != EJson::Number)
	{
		return AnimationFailure(
			FString::Printf(TEXT("Animation numeric field '%s' must be an integer"), *FieldName),
			Path,
			InvalidCode);
	}

	double Number = 0.0;
	if (!Value->TryGetNumber(Number)
		|| !FMath::IsFinite(Number)
		|| FMath::FloorToDouble(Number) != Number
		|| Number < MinValue
		|| Number > MaxValue)
	{
		return AnimationFailure(
			FString::Printf(TEXT("Animation numeric field '%s' must be a finite integer in range"), *FieldName),
			Path,
			InvalidCode);
	}

	OutValue = static_cast<int64>(Number);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadInt32NumberField(
	const TSharedPtr<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	const FString& MissingCode,
	const FString& InvalidCode,
	int32& OutValue)
{
	int64 Value = 0;
	const FAssetDocumentCapabilityResult Result = ReadIntegralNumberField(
		Object,
		FieldName,
		Path,
		MissingCode,
		InvalidCode,
		static_cast<double>(TNumericLimits<int32>::Min()),
		static_cast<double>(TNumericLimits<int32>::Max()),
		Value);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutValue = static_cast<int32>(Value);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ReadPositiveFrameRateNumberField(
	const TSharedPtr<FJsonObject>& Object,
	const FString& FieldName,
	const FString& Path,
	uint32& OutValue)
{
	int64 Value = 0;
	const FAssetDocumentCapabilityResult Result = ReadIntegralNumberField(
		Object,
		FieldName,
		Path,
		TEXT("MissingAnimationFrameRate"),
		TEXT("InvalidAnimationFrameRate"),
		1.0,
		static_cast<double>(TNumericLimits<int32>::Max()),
		Value);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutValue = static_cast<uint32>(Value);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseKeyArray(
	const TArray<TSharedPtr<FJsonValue>>* KeyValues,
	const FString& Path,
	TArray<FWidgetAnimationKeySpec>& OutKeys)
{
	OutKeys.Reset();
	if (!KeyValues)
	{
		return AnimationFailure(TEXT("Animation track requires Keys"), Path / TEXT("Keys"), TEXT("MissingAnimationKeys"));
	}

	for (int32 KeyIndex = 0; KeyIndex < KeyValues->Num(); ++KeyIndex)
	{
		const FString KeyPath = FString::Printf(TEXT("%s/Keys/%d"), *Path, KeyIndex);
		const TSharedPtr<FJsonValue>& KeyValue = (*KeyValues)[KeyIndex];
		if (!KeyValue.IsValid() || KeyValue->Type != EJson::Object)
		{
			return AnimationFailure(TEXT("Animation keys must be objects"), KeyPath, TEXT("InvalidAnimationKey"));
		}

		const TSharedPtr<FJsonObject> KeyObject = KeyValue->AsObject();
		double Value = 0.0;
		int32 Frame = 0;
		FAssetDocumentCapabilityResult Result = ReadInt32NumberField(
			KeyObject,
			TEXT("Frame"),
			KeyPath / TEXT("Frame"),
			TEXT("MissingAnimationKeyFrame"),
			TEXT("InvalidAnimationFrame"),
			Frame);
		if (!Result.bSuccess)
		{
			return Result;
		}
		if (!KeyObject->TryGetNumberField(TEXT("Value"), Value))
		{
			return AnimationFailure(TEXT("Animation key requires Value"), KeyPath / TEXT("Value"), TEXT("MissingAnimationKeyValue"));
		}

		FWidgetAnimationKeySpec Key;
		Key.Frame = Frame;
		Key.Value = static_cast<float>(Value);
		OutKeys.Add(Key);
	}

	OutKeys.Sort([](const FWidgetAnimationKeySpec& Left, const FWidgetAnimationKeySpec& Right)
	{
		return Left.Frame < Right.Frame;
	});
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ParseTransformChannels(
	const TSharedPtr<FJsonObject>& TrackObject,
	const FString& TrackPath,
	TArray<FWidgetAnimationChannelSpec>& OutChannels)
{
	OutChannels.Reset();
	const TArray<TSharedPtr<FJsonValue>>* ChannelValues = nullptr;
	if (!TrackObject->TryGetArrayField(TEXT("Channels"), ChannelValues) || !ChannelValues)
	{
		return AnimationFailure(TEXT("Transform animation tracks require Channels"), TrackPath / TEXT("Channels"), TEXT("MissingAnimationChannels"));
	}

	TSet<FName> SeenChannels;
	for (int32 ChannelIndex = 0; ChannelIndex < ChannelValues->Num(); ++ChannelIndex)
	{
		const FString ChannelPath = FString::Printf(TEXT("%s/Channels/%d"), *TrackPath, ChannelIndex);
		const TSharedPtr<FJsonValue>& ChannelValue = (*ChannelValues)[ChannelIndex];
		if (!ChannelValue.IsValid() || ChannelValue->Type != EJson::Object)
		{
			return AnimationFailure(TEXT("Animation transform channels must be objects"), ChannelPath, TEXT("InvalidAnimationChannel"));
		}

		const TSharedPtr<FJsonObject> ChannelObject = ChannelValue->AsObject();
		FString ChannelName;
		FAssetDocumentCapabilityResult Result = RequireStringField(
			ChannelObject,
			TEXT("Name"),
			ChannelPath / TEXT("Name"),
			TEXT("MissingAnimationChannelName"),
			ChannelName);
		if (!Result.bSuccess)
		{
			return Result;
		}

		FWidgetAnimationChannelSpec Channel;
		Channel.Name = FName(*ChannelName);
		if (Channel.Name != TEXT("Rotation"))
		{
			return AnimationFailure(
				FString::Printf(TEXT("Transform animation channel '%s' is not supported yet"), *ChannelName),
				ChannelPath / TEXT("Name"),
				TEXT("UnsupportedWidgetAnimationTrack"));
		}
		if (SeenChannels.Contains(Channel.Name))
		{
			return AnimationFailure(
				FString::Printf(TEXT("Duplicate transform animation channel '%s'"), *ChannelName),
				ChannelPath,
				TEXT("DuplicateWidgetAnimationChannel"));
		}
		SeenChannels.Add(Channel.Name);

		const TArray<TSharedPtr<FJsonValue>>* KeyValues = nullptr;
		ChannelObject->TryGetArrayField(TEXT("Keys"), KeyValues);
		Result = ParseKeyArray(KeyValues, ChannelPath, Channel.Keys);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutChannels.Add(MoveTemp(Channel));
	}

	OutChannels.Sort([](const FWidgetAnimationChannelSpec& Left, const FWidgetAnimationChannelSpec& Right)
	{
		return Left.Name.ToString() < Right.Name.ToString();
	});
	return FAssetDocumentCapabilityResult::Success();
}

FString TrackIdentityKey(const FWidgetAnimationTrackSpec& Track)
{
	return FString::Printf(
		TEXT("%s|%s|%s"),
		*Track.Widget.ToString(),
		*Track.Property.ToString(),
		Track.Kind == EWidgetAnimationTrackKind::Float ? TEXT("Float") : TEXT("Transform")).ToLower();
}

FAssetDocumentCapabilityResult ParseAnimationSpecs(
	const TSharedPtr<FJsonValue>& AnimationsJson,
	TArray<FWidgetAnimationSpec>& OutSpecs)
{
	OutSpecs.Reset();
	if (!AnimationsJson.IsValid() || AnimationsJson->Type == EJson::Null)
	{
		return FAssetDocumentCapabilityResult::Success();
	}
	if (AnimationsJson->Type != EJson::Array)
	{
		return AnimationFailure(TEXT("Body.Animations must be an array"), TEXT("/Body/Animations"), TEXT("InvalidBodySectionType"));
	}

	TSet<FName> SeenAnimationNames;
	const TArray<TSharedPtr<FJsonValue>>& AnimationValues = AnimationsJson->AsArray();
	for (int32 AnimationIndex = 0; AnimationIndex < AnimationValues.Num(); ++AnimationIndex)
	{
		const FString AnimationPath = FString::Printf(TEXT("/Body/Animations/%d"), AnimationIndex);
		const TSharedPtr<FJsonValue>& AnimationValue = AnimationValues[AnimationIndex];
		if (!AnimationValue.IsValid() || AnimationValue->Type != EJson::Object)
		{
			return AnimationFailure(TEXT("Body.Animations entries must be objects"), AnimationPath, TEXT("InvalidAnimationEntry"));
		}

		const TSharedPtr<FJsonObject> AnimationObject = AnimationValue->AsObject();
		FWidgetAnimationSpec Animation;
		Animation.JsonPath = AnimationPath;

		FString AnimationName;
		FAssetDocumentCapabilityResult Result = RequireStringField(
			AnimationObject,
			TEXT("Name"),
			AnimationPath / TEXT("Name"),
			TEXT("MissingAnimationName"),
			AnimationName);
		if (!Result.bSuccess)
		{
			return Result;
		}
		Animation.Name = FName(*AnimationName);
		if (SeenAnimationNames.Contains(Animation.Name))
		{
			return AnimationFailure(
				FString::Printf(TEXT("Duplicate animation name '%s'"), *AnimationName),
				AnimationPath,
				TEXT("DuplicateWidgetAnimationName"));
		}
		SeenAnimationNames.Add(Animation.Name);

		const TSharedPtr<FJsonObject>* FrameRateObject = nullptr;
		if (AnimationObject->TryGetObjectField(TEXT("FrameRate"), FrameRateObject) && FrameRateObject && FrameRateObject->IsValid())
		{
			uint32 Numerator = 30;
			uint32 Denominator = 1;
			if ((*FrameRateObject)->HasField(TEXT("Numerator")))
			{
				Result = ReadPositiveFrameRateNumberField(
					*FrameRateObject,
					TEXT("Numerator"),
					AnimationPath / TEXT("FrameRate") / TEXT("Numerator"),
					Numerator);
				if (!Result.bSuccess)
				{
					return Result;
				}
			}
			if ((*FrameRateObject)->HasField(TEXT("Denominator")))
			{
				Result = ReadPositiveFrameRateNumberField(
					*FrameRateObject,
					TEXT("Denominator"),
					AnimationPath / TEXT("FrameRate") / TEXT("Denominator"),
					Denominator);
				if (!Result.bSuccess)
				{
					return Result;
				}
			}
			Animation.FrameRate = FFrameRate(Numerator, Denominator);
		}

		const TSharedPtr<FJsonObject>* PlaybackRangeObject = nullptr;
		if (AnimationObject->TryGetObjectField(TEXT("PlaybackRange"), PlaybackRangeObject) && PlaybackRangeObject && PlaybackRangeObject->IsValid())
		{
			int32 StartFrame = 0;
			int32 EndFrame = 30;
			if ((*PlaybackRangeObject)->HasField(TEXT("StartFrame")))
			{
				Result = ReadInt32NumberField(
					*PlaybackRangeObject,
					TEXT("StartFrame"),
					AnimationPath / TEXT("PlaybackRange") / TEXT("StartFrame"),
					TEXT("MissingAnimationPlaybackRange"),
					TEXT("InvalidAnimationPlaybackRange"),
					StartFrame);
				if (!Result.bSuccess)
				{
					return Result;
				}
			}
			if ((*PlaybackRangeObject)->HasField(TEXT("EndFrame")))
			{
				Result = ReadInt32NumberField(
					*PlaybackRangeObject,
					TEXT("EndFrame"),
					AnimationPath / TEXT("PlaybackRange") / TEXT("EndFrame"),
					TEXT("MissingAnimationPlaybackRange"),
					TEXT("InvalidAnimationPlaybackRange"),
					EndFrame);
				if (!Result.bSuccess)
				{
					return Result;
				}
			}
			Animation.StartFrame = StartFrame;
			Animation.EndFrame = EndFrame;
			if (Animation.EndFrame < Animation.StartFrame)
			{
				return AnimationFailure(TEXT("Animation PlaybackRange.EndFrame must be >= StartFrame"), AnimationPath / TEXT("PlaybackRange"), TEXT("InvalidAnimationPlaybackRange"));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* TrackValues = nullptr;
		if (!AnimationObject->TryGetArrayField(TEXT("Tracks"), TrackValues) || !TrackValues)
		{
			return AnimationFailure(TEXT("Animation requires Tracks array"), AnimationPath / TEXT("Tracks"), TEXT("MissingAnimationTracks"));
		}

		TSet<FString> SeenTracks;
		for (int32 TrackIndex = 0; TrackIndex < TrackValues->Num(); ++TrackIndex)
		{
			const FString TrackPath = FString::Printf(TEXT("%s/Tracks/%d"), *AnimationPath, TrackIndex);
			const TSharedPtr<FJsonValue>& TrackValue = (*TrackValues)[TrackIndex];
			if (!TrackValue.IsValid() || TrackValue->Type != EJson::Object)
			{
				return AnimationFailure(TEXT("Animation tracks must be objects"), TrackPath, TEXT("InvalidAnimationTrack"));
			}

			const TSharedPtr<FJsonObject> TrackObject = TrackValue->AsObject();
			FWidgetAnimationTrackSpec Track;
			Track.JsonPath = TrackPath;
			FString WidgetName;
			FString PropertyName;
			FString TypeName;
			Result = RequireStringField(TrackObject, TEXT("Widget"), TrackPath / TEXT("Widget"), TEXT("MissingAnimationWidget"), WidgetName);
			if (!Result.bSuccess)
			{
				return Result;
			}
			Result = RequireStringField(TrackObject, TEXT("Property"), TrackPath / TEXT("Property"), TEXT("MissingAnimationProperty"), PropertyName);
			if (!Result.bSuccess)
			{
				return Result;
			}
			Result = RequireStringField(TrackObject, TEXT("Type"), TrackPath / TEXT("Type"), TEXT("MissingAnimationTrackType"), TypeName);
			if (!Result.bSuccess)
			{
				return Result;
			}

			Track.Widget = FName(*WidgetName);
			Track.Property = FName(*PropertyName);
			if (TypeName.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
			{
				Track.Kind = EWidgetAnimationTrackKind::Float;
				const TArray<TSharedPtr<FJsonValue>>* KeyValues = nullptr;
				TrackObject->TryGetArrayField(TEXT("Keys"), KeyValues);
				Result = ParseKeyArray(KeyValues, TrackPath, Track.Keys);
				if (!Result.bSuccess)
				{
					return Result;
				}
			}
			else if (TypeName.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
			{
				Track.Kind = EWidgetAnimationTrackKind::Transform;
				if (Track.Property != TEXT("RenderTransform"))
				{
					return AnimationFailure(
						TEXT("Transform animation tracks currently support RenderTransform only"),
						TrackPath / TEXT("Property"),
						TEXT("UnsupportedWidgetAnimationTrack"));
				}
				Result = ParseTransformChannels(TrackObject, TrackPath, Track.Channels);
				if (!Result.bSuccess)
				{
					return Result;
				}
			}
			else
			{
				return AnimationFailure(
					FString::Printf(TEXT("Widget animation track type '%s' is not supported"), *TypeName),
					TrackPath / TEXT("Type"),
					TEXT("UnsupportedWidgetAnimationTrack"));
			}

			const FString Identity = TrackIdentityKey(Track);
			if (SeenTracks.Contains(Identity))
			{
				return AnimationFailure(
					FString::Printf(TEXT("Duplicate animation track '%s.%s'"), *WidgetName, *PropertyName),
					TrackPath,
					TEXT("DuplicateWidgetAnimationTrack"));
			}
			SeenTracks.Add(Identity);
			Animation.Tracks.Add(MoveTemp(Track));
		}

		Animation.Tracks.Sort([](const FWidgetAnimationTrackSpec& Left, const FWidgetAnimationTrackSpec& Right)
		{
			return TrackIdentityKey(Left) < TrackIdentityKey(Right);
		});
		OutSpecs.Add(MoveTemp(Animation));
	}

	OutSpecs.Sort([](const FWidgetAnimationSpec& Left, const FWidgetAnimationSpec& Right)
	{
		return Left.Name.ToString() < Right.Name.ToString();
	});
	return FAssetDocumentCapabilityResult::Success();
}

bool IsSupportedFloatProperty(const UWidget* Widget, FName PropertyName)
{
	FProperty* Property = Widget ? FindFProperty<FProperty>(Widget->GetClass(), PropertyName) : nullptr;
	return CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property);
}

FAssetDocumentCapabilityResult ValidateTrackAgainstWidgetTree(UWidgetBlueprint* WidgetBlueprint, const FWidgetAnimationTrackSpec& Track)
{
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return AnimationFailure(TEXT("WidgetBlueprint WidgetTree is required for animations"), Track.JsonPath, TEXT("MissingWidgetTree"));
	}

	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(Track.Widget);
	if (!Widget)
	{
		return AnimationFailure(
			FString::Printf(TEXT("Animation target widget '%s' does not exist"), *Track.Widget.ToString()),
			Track.JsonPath / TEXT("Widget"),
			TEXT("MissingWidgetAnimationBinding"));
	}

	if (Track.Kind == EWidgetAnimationTrackKind::Float && !IsSupportedFloatProperty(Widget, Track.Property))
	{
		return AnimationFailure(
			FString::Printf(TEXT("Animation float property '%s.%s' is not supported"), *Track.Widget.ToString(), *Track.Property.ToString()),
			Track.JsonPath / TEXT("Property"),
			TEXT("UnsupportedWidgetAnimationTrack"));
	}

	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ValidateSpecsAgainstWidgetBlueprint(UWidgetBlueprint* WidgetBlueprint, const TArray<FWidgetAnimationSpec>& Specs)
{
	for (const FWidgetAnimationSpec& Animation : Specs)
	{
		for (const FWidgetAnimationTrackSpec& Track : Animation.Tracks)
		{
			const FAssetDocumentCapabilityResult Result = ValidateTrackAgainstWidgetTree(WidgetBlueprint, Track);
			if (!Result.bSuccess)
			{
				return Result;
			}
		}
	}
	return FAssetDocumentCapabilityResult::Success();
}

TSharedPtr<FJsonObject> KeyToJson(const FWidgetAnimationKeySpec& Key)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("Frame"), Key.Frame);
	Json->SetNumberField(TEXT("Value"), Key.Value);
	return Json;
}

TArray<TSharedPtr<FJsonValue>> KeysToJsonValues(const TArray<FWidgetAnimationKeySpec>& Keys)
{
	TArray<FWidgetAnimationKeySpec> SortedKeys = Keys;
	SortedKeys.Sort([](const FWidgetAnimationKeySpec& Left, const FWidgetAnimationKeySpec& Right)
	{
		return Left.Frame < Right.Frame;
	});

	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FWidgetAnimationKeySpec& Key : SortedKeys)
	{
		Values.Add(MakeShared<FJsonValueObject>(KeyToJson(Key)));
	}
	return Values;
}

TSharedPtr<FJsonObject> TrackSpecToJson(const FWidgetAnimationTrackSpec& Track)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Widget"), Track.Widget.ToString());
	Json->SetStringField(TEXT("Property"), Track.Property.ToString());
	if (Track.Kind == EWidgetAnimationTrackKind::Float)
	{
		Json->SetStringField(TEXT("Type"), TEXT("Float"));
		Json->SetArrayField(TEXT("Keys"), KeysToJsonValues(Track.Keys));
	}
	else
	{
		Json->SetStringField(TEXT("Type"), TEXT("Transform"));
		TArray<TSharedPtr<FJsonValue>> Channels;
		TArray<FWidgetAnimationChannelSpec> SortedChannels = Track.Channels;
		SortedChannels.Sort([](const FWidgetAnimationChannelSpec& Left, const FWidgetAnimationChannelSpec& Right)
		{
			return Left.Name.ToString() < Right.Name.ToString();
		});
		for (const FWidgetAnimationChannelSpec& Channel : SortedChannels)
		{
			TSharedPtr<FJsonObject> ChannelJson = MakeShared<FJsonObject>();
			ChannelJson->SetStringField(TEXT("Name"), Channel.Name.ToString());
			ChannelJson->SetArrayField(TEXT("Keys"), KeysToJsonValues(Channel.Keys));
			Channels.Add(MakeShared<FJsonValueObject>(ChannelJson));
		}
		Json->SetArrayField(TEXT("Channels"), Channels);
	}
	return Json;
}

TSharedPtr<FJsonObject> AnimationSpecToJson(const FWidgetAnimationSpec& Animation)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Name"), Animation.Name.ToString());

	TSharedPtr<FJsonObject> FrameRate = MakeShared<FJsonObject>();
	FrameRate->SetNumberField(TEXT("Numerator"), Animation.FrameRate.Numerator);
	FrameRate->SetNumberField(TEXT("Denominator"), Animation.FrameRate.Denominator);
	Json->SetObjectField(TEXT("FrameRate"), FrameRate);

	TSharedPtr<FJsonObject> PlaybackRange = MakeShared<FJsonObject>();
	PlaybackRange->SetNumberField(TEXT("StartFrame"), Animation.StartFrame);
	PlaybackRange->SetNumberField(TEXT("EndFrame"), Animation.EndFrame);
	Json->SetObjectField(TEXT("PlaybackRange"), PlaybackRange);

	TArray<TSharedPtr<FJsonValue>> Tracks;
	for (const FWidgetAnimationTrackSpec& Track : Animation.Tracks)
	{
		Tracks.Add(MakeShared<FJsonValueObject>(TrackSpecToJson(Track)));
	}
	Json->SetArrayField(TEXT("Tracks"), Tracks);
	return Json;
}

TSharedPtr<FJsonValue> AnimationSpecsToJsonValue(const TArray<FWidgetAnimationSpec>& Animations)
{
	TArray<FWidgetAnimationSpec> SortedAnimations = Animations;
	SortedAnimations.Sort([](const FWidgetAnimationSpec& Left, const FWidgetAnimationSpec& Right)
	{
		return Left.Name.ToString() < Right.Name.ToString();
	});

	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FWidgetAnimationSpec& Animation : SortedAnimations)
	{
		Values.Add(MakeShared<FJsonValueObject>(AnimationSpecToJson(Animation)));
	}
	return MakeShared<FJsonValueArray>(Values);
}

void SetFloatChannelKeys(FMovieSceneFloatChannel& Channel, const TArray<FWidgetAnimationKeySpec>& Keys, FFrameRate TickResolution)
{
	TArray<FFrameNumber> Times;
	TArray<FMovieSceneFloatValue> Values;
	for (const FWidgetAnimationKeySpec& Key : Keys)
	{
		Times.Add(FFrameNumber(Key.Frame));
		FMovieSceneFloatValue Value(Key.Value);
		Value.InterpMode = RCIM_Linear;
		Values.Add(Value);
	}
	Channel.SetTickResolution(TickResolution);
	Channel.Set(MoveTemp(Times), MoveTemp(Values));
}

TArray<FWidgetAnimationKeySpec> ExtractFloatChannelKeys(const FMovieSceneFloatChannel& Channel)
{
	TArray<FWidgetAnimationKeySpec> Keys;
	const TArrayView<const FFrameNumber> Times = Channel.GetTimes();
	const TArrayView<const FMovieSceneFloatValue> Values = Channel.GetValues();
	const int32 Count = FMath::Min(Times.Num(), Values.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FWidgetAnimationKeySpec Key;
		Key.Frame = Times[Index].Value;
		Key.Value = Values[Index].Value;
		Keys.Add(Key);
	}
	Keys.Sort([](const FWidgetAnimationKeySpec& Left, const FWidgetAnimationKeySpec& Right)
	{
		return Left.Frame < Right.Frame;
	});
	return Keys;
}

UWidgetAnimation* FindAnimationByName(UWidgetBlueprint* WidgetBlueprint, FName Name)
{
	if (!WidgetBlueprint)
	{
		return nullptr;
	}
	for (UWidgetAnimation* Animation : WidgetBlueprint->Animations)
	{
		if (Animation && Animation->GetFName() == Name)
		{
			return Animation;
		}
	}
	return nullptr;
}

struct FExistingAnimationState
{
	UWidgetAnimation* Animation = nullptr;
	FName OriginalName;
};

void MoveAnimationToTransient(UWidgetAnimation* Animation)
{
	if (!Animation)
	{
		return;
	}
	const FName TransientName = MakeUniqueObjectName(
		GetTransientPackage(),
		UWidgetAnimation::StaticClass(),
		FName(*(Animation->GetName() + TEXT("_AssetDocumentDiscarded"))));
	Animation->Rename(*TransientName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
}

void RenameExistingAnimationsOutOfTheWay(UWidgetBlueprint* WidgetBlueprint, TArray<FExistingAnimationState>& OutExistingAnimations)
{
	OutExistingAnimations.Reset();
	if (!WidgetBlueprint)
	{
		return;
	}

	for (UWidgetAnimation* Animation : WidgetBlueprint->Animations)
	{
		if (!Animation)
		{
			continue;
		}
		OutExistingAnimations.Add({Animation, Animation->GetFName()});
		const FName TrashName = MakeUniqueObjectName(
			WidgetBlueprint,
			UWidgetAnimation::StaticClass(),
			FName(*(Animation->GetName() + TEXT("_AssetDocumentReplaced"))));
		Animation->Rename(*TrashName.ToString(), WidgetBlueprint, REN_DontCreateRedirectors | REN_NonTransactional);
	}
	WidgetBlueprint->Animations.Empty();
}

void RestoreExistingAnimations(UWidgetBlueprint* WidgetBlueprint, const TArray<FExistingAnimationState>& ExistingAnimations)
{
	if (!WidgetBlueprint)
	{
		return;
	}
	WidgetBlueprint->Animations.Empty();
	for (const FExistingAnimationState& Existing : ExistingAnimations)
	{
		if (!Existing.Animation)
		{
			continue;
		}
		Existing.Animation->Rename(*Existing.OriginalName.ToString(), WidgetBlueprint, REN_DontCreateRedirectors | REN_NonTransactional);
		WidgetBlueprint->Animations.Add(Existing.Animation);
	}
}

FGuid ResolveOrCreateWidgetBindingGuid(
	UWidgetBlueprint* WidgetBlueprint,
	UWidgetAnimation* Animation,
	UMovieScene* MovieScene,
	FName WidgetName,
	TMap<FName, FGuid>& WidgetGuids)
{
	if (const FGuid* ExistingGuid = WidgetGuids.Find(WidgetName))
	{
		return *ExistingGuid;
	}

	UWidget* Widget = WidgetBlueprint && WidgetBlueprint->WidgetTree ? WidgetBlueprint->WidgetTree->FindWidget(WidgetName) : nullptr;
	FGuid Guid = MovieScene->AddPossessable(WidgetName.ToString(), Widget ? Widget->GetClass() : UWidget::StaticClass());
#if WITH_EDITORONLY_DATA
	MovieScene->SetObjectDisplayName(Guid, FText::FromName(WidgetName));
#endif

	FWidgetAnimationBinding Binding;
	Binding.WidgetName = WidgetName;
	Binding.AnimationGuid = Guid;
	Binding.bIsRootWidget = WidgetBlueprint && WidgetBlueprint->WidgetTree && WidgetBlueprint->WidgetTree->RootWidget == Widget;
	Animation->AnimationBindings.Add(Binding);
	WidgetGuids.Add(WidgetName, Guid);
	return Guid;
}

FAssetDocumentCapabilityResult AddFloatTrack(
	UMovieScene* MovieScene,
	const FGuid& BindingGuid,
	const FWidgetAnimationTrackSpec& TrackSpec)
{
	UMovieSceneFloatTrack* Track = MovieScene ? MovieScene->AddTrack<UMovieSceneFloatTrack>(BindingGuid) : nullptr;
	if (!Track)
	{
		return AnimationFailure(TEXT("Failed to create MovieScene float track"), TrackSpec.JsonPath, TEXT("CreateWidgetAnimationTrackFailed"));
	}
	Track->SetPropertyNameAndPath(TrackSpec.Property, TrackSpec.Property.ToString());

	UMovieSceneFloatSection* Section = Cast<UMovieSceneFloatSection>(Track->CreateNewSection());
	if (!Section)
	{
		return AnimationFailure(TEXT("Failed to create MovieScene float section"), TrackSpec.JsonPath, TEXT("CreateWidgetAnimationTrackFailed"));
	}
	Section->SetRange(MovieScene->GetPlaybackRange());
	SetFloatChannelKeys(Section->GetChannel(), TrackSpec.Keys, MovieScene->GetTickResolution());
	Track->AddSection(*Section);
	Track->SetSectionToKey(Section);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult AddTransformTrack(
	UMovieScene* MovieScene,
	const FGuid& BindingGuid,
	const FWidgetAnimationTrackSpec& TrackSpec)
{
	UMovieScene2DTransformTrack* Track = MovieScene ? MovieScene->AddTrack<UMovieScene2DTransformTrack>(BindingGuid) : nullptr;
	if (!Track)
	{
		return AnimationFailure(TEXT("Failed to create MovieScene transform track"), TrackSpec.JsonPath, TEXT("CreateWidgetAnimationTrackFailed"));
	}
	Track->SetPropertyNameAndPath(TEXT("RenderTransform"), TEXT("RenderTransform"));

	UMovieScene2DTransformSection* Section = Cast<UMovieScene2DTransformSection>(Track->CreateNewSection());
	if (!Section)
	{
		return AnimationFailure(TEXT("Failed to create MovieScene transform section"), TrackSpec.JsonPath, TEXT("CreateWidgetAnimationTrackFailed"));
	}
	Section->SetRange(MovieScene->GetPlaybackRange());
	Section->SetMask(FMovieScene2DTransformMask(EMovieScene2DTransformChannel::Rotation));
	for (const FWidgetAnimationChannelSpec& Channel : TrackSpec.Channels)
	{
		if (Channel.Name == TEXT("Rotation"))
		{
			SetFloatChannelKeys(Section->Rotation, Channel.Keys, MovieScene->GetTickResolution());
		}
	}
	Track->AddSection(*Section);
	Track->SetSectionToKey(Section);
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult MaterializeAnimation(
	UWidgetBlueprint* WidgetBlueprint,
	const FWidgetAnimationSpec& Spec,
	const FName& ObjectName,
	UWidgetAnimation*& OutAnimation)
{
	OutAnimation = nullptr;
	UWidgetAnimation* Animation = NewObject<UWidgetAnimation>(WidgetBlueprint, ObjectName, RF_Transactional);
	if (!Animation)
	{
		return AnimationFailure(TEXT("Failed to create WidgetAnimation object"), Spec.JsonPath, TEXT("CreateWidgetAnimationFailed"));
	}
#if WITH_EDITOR
	Animation->SetDisplayLabel(Spec.Name.ToString());
#endif
	Animation->MovieScene = NewObject<UMovieScene>(Animation, TEXT("MovieScene"), RF_Transactional);
	if (!Animation->MovieScene)
	{
		return AnimationFailure(TEXT("Failed to create WidgetAnimation MovieScene"), Spec.JsonPath, TEXT("CreateWidgetAnimationFailed"));
	}

	UMovieScene* MovieScene = Animation->MovieScene;
	MovieScene->SetTickResolutionDirectly(Spec.FrameRate);
	MovieScene->SetDisplayRate(Spec.FrameRate);
	MovieScene->SetPlaybackRange(FFrameNumber(Spec.StartFrame), FMath::Max(0, Spec.EndFrame - Spec.StartFrame));
#if WITH_EDITORONLY_DATA
	MovieScene->SetViewRange(static_cast<double>(Spec.StartFrame) / Spec.FrameRate.AsDecimal(), static_cast<double>(Spec.EndFrame) / Spec.FrameRate.AsDecimal());
	MovieScene->SetWorkingRange(static_cast<double>(Spec.StartFrame) / Spec.FrameRate.AsDecimal(), static_cast<double>(Spec.EndFrame) / Spec.FrameRate.AsDecimal());
#endif

	TMap<FName, FGuid> WidgetGuids;
	for (const FWidgetAnimationTrackSpec& Track : Spec.Tracks)
	{
		const FGuid BindingGuid = ResolveOrCreateWidgetBindingGuid(WidgetBlueprint, Animation, MovieScene, Track.Widget, WidgetGuids);
		const FAssetDocumentCapabilityResult TrackResult = Track.Kind == EWidgetAnimationTrackKind::Float
			? AddFloatTrack(MovieScene, BindingGuid, Track)
			: AddTransformTrack(MovieScene, BindingGuid, Track);
		if (!TrackResult.bSuccess)
		{
			return TrackResult;
		}
	}

	OutAnimation = Animation;
	return FAssetDocumentCapabilityResult::Success();
}

FName FindWidgetNameForBinding(const UWidgetAnimation* Animation, const FGuid& BindingGuid)
{
	if (!Animation)
	{
		return NAME_None;
	}
	for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
	{
		if (Binding.AnimationGuid == BindingGuid)
		{
			return Binding.WidgetName;
		}
	}
	return NAME_None;
}

FAssetDocumentCapabilityResult ExtractFloatTrack(
	const UMovieSceneFloatTrack* Track,
	const FName& WidgetName,
	FWidgetAnimationTrackSpec& OutTrack)
{
	if (!Track)
	{
		return AnimationFailure(TEXT("Invalid WidgetAnimation float track"), TEXT("/Body/Animations"), TEXT("UnsupportedWidgetAnimationTrack"));
	}
	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (Sections.Num() != 1 || !Cast<UMovieSceneFloatSection>(Sections[0]))
	{
		return AnimationFailure(TEXT("WidgetAnimation float track sections are unsupported"), TEXT("/Body/Animations"), TEXT("UnsupportedWidgetAnimationTrack"));
	}

	OutTrack.Widget = WidgetName;
	OutTrack.Property = Track->GetPropertyName();
	OutTrack.Kind = EWidgetAnimationTrackKind::Float;
	OutTrack.Keys = ExtractFloatChannelKeys(CastChecked<UMovieSceneFloatSection>(Sections[0])->GetChannel());
	return FAssetDocumentCapabilityResult::Success();
}

bool HasUnsupportedTransformChannelData(const UMovieScene2DTransformSection* Section)
{
	return Section
		&& (Section->Translation[0].HasAnyData()
			|| Section->Translation[1].HasAnyData()
			|| Section->Scale[0].HasAnyData()
			|| Section->Scale[1].HasAnyData()
			|| Section->Shear[0].HasAnyData()
			|| Section->Shear[1].HasAnyData());
}

FAssetDocumentCapabilityResult ExtractTransformTrack(
	const UMovieScene2DTransformTrack* Track,
	const FName& WidgetName,
	FWidgetAnimationTrackSpec& OutTrack)
{
	if (!Track)
	{
		return AnimationFailure(TEXT("Invalid WidgetAnimation transform track"), TEXT("/Body/Animations"), TEXT("UnsupportedWidgetAnimationTrack"));
	}
	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (Sections.Num() != 1 || !Cast<UMovieScene2DTransformSection>(Sections[0]))
	{
		return AnimationFailure(TEXT("WidgetAnimation transform track sections are unsupported"), TEXT("/Body/Animations"), TEXT("UnsupportedWidgetAnimationTrack"));
	}

	const UMovieScene2DTransformSection* Section = CastChecked<UMovieScene2DTransformSection>(Sections[0]);
	if (HasUnsupportedTransformChannelData(Section))
	{
		return AnimationFailure(TEXT("WidgetAnimation transform track has unsupported channels"), TEXT("/Body/Animations"), TEXT("UnsupportedWidgetAnimationTrack"));
	}

	OutTrack.Widget = WidgetName;
	OutTrack.Property = TEXT("RenderTransform");
	OutTrack.Kind = EWidgetAnimationTrackKind::Transform;
	if (Section->Rotation.HasAnyData())
	{
		FWidgetAnimationChannelSpec Rotation;
		Rotation.Name = TEXT("Rotation");
		Rotation.Keys = ExtractFloatChannelKeys(Section->Rotation);
		OutTrack.Channels.Add(MoveTemp(Rotation));
	}
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractAnimationSpec(const UWidgetAnimation* Animation, FWidgetAnimationSpec& OutSpec)
{
	if (!Animation || !Animation->MovieScene)
	{
		return AnimationFailure(TEXT("WidgetAnimation is missing MovieScene"), TEXT("/Body/Animations"), TEXT("UnsupportedWidgetAnimationTrack"));
	}

	const UMovieScene* MovieScene = Animation->MovieScene;
	OutSpec.Name = Animation->GetFName();
	OutSpec.FrameRate = MovieScene->GetDisplayRate();
	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	OutSpec.StartFrame = PlaybackRange.GetLowerBoundValue().Value;
	OutSpec.EndFrame = PlaybackRange.GetUpperBoundValue().Value;

	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		const FName WidgetName = FindWidgetNameForBinding(Animation, Binding.GetObjectGuid());
		if (WidgetName.IsNone())
		{
			return AnimationFailure(TEXT("WidgetAnimation binding has no widget name evidence"), TEXT("/Body/Animations"), TEXT("UnsupportedWidgetAnimationTrack"));
		}

		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			FWidgetAnimationTrackSpec TrackSpec;
			FAssetDocumentCapabilityResult TrackResult = FAssetDocumentCapabilityResult::Success();
			if (const UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track))
			{
				TrackResult = ExtractFloatTrack(FloatTrack, WidgetName, TrackSpec);
			}
			else if (const UMovieScene2DTransformTrack* TransformTrack = Cast<UMovieScene2DTransformTrack>(Track))
			{
				TrackResult = ExtractTransformTrack(TransformTrack, WidgetName, TrackSpec);
			}
			else
			{
				return AnimationFailure(
					FString::Printf(TEXT("WidgetAnimation track class '%s' is not supported"), Track ? *Track->GetClass()->GetPathName() : TEXT("<null>")),
					TEXT("/Body/Animations"),
					TEXT("UnsupportedWidgetAnimationTrack"));
			}

			if (!TrackResult.bSuccess)
			{
				return TrackResult;
			}
			OutSpec.Tracks.Add(MoveTemp(TrackSpec));
		}
	}

	OutSpec.Tracks.Sort([](const FWidgetAnimationTrackSpec& Left, const FWidgetAnimationTrackSpec& Right)
	{
		return TrackIdentityKey(Left) < TrackIdentityKey(Right);
	});
	return FAssetDocumentCapabilityResult::Success();
}

FAssetDocumentCapabilityResult ExtractAnimationSpecs(const UWidgetBlueprint* WidgetBlueprint, TArray<FWidgetAnimationSpec>& OutSpecs)
{
	OutSpecs.Reset();
#if WITH_EDITORONLY_DATA
	if (!WidgetBlueprint)
	{
		return FAssetDocumentCapabilityResult::Success();
	}

	for (const UWidgetAnimation* Animation : WidgetBlueprint->Animations)
	{
		FWidgetAnimationSpec Spec;
		const FAssetDocumentCapabilityResult Result = ExtractAnimationSpec(Animation, Spec);
		if (!Result.bSuccess)
		{
			return Result;
		}
		OutSpecs.Add(MoveTemp(Spec));
	}
	OutSpecs.Sort([](const FWidgetAnimationSpec& Left, const FWidgetAnimationSpec& Right)
	{
		return Left.Name.ToString() < Right.Name.ToString();
	});
#endif
	return FAssetDocumentCapabilityResult::Success();
}
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationAdapter::Validate(const TSharedPtr<FJsonValue>& AnimationsJson)
{
	TArray<FWidgetAnimationSpec> Specs;
	return ParseAnimationSpecs(AnimationsJson, Specs);
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationAdapter::Preflight(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& AnimationsJson)
{
	TArray<FWidgetAnimationSpec> Specs;
	const FAssetDocumentCapabilityResult ParseResult = ParseAnimationSpecs(AnimationsJson, Specs);
	if (!ParseResult.bSuccess)
	{
		return ParseResult;
	}
	return ValidateSpecsAgainstWidgetBlueprint(WidgetBlueprint, Specs);
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationAdapter::Apply(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonValue>& AnimationsJson, bool* bOutChanged)
{
	if (bOutChanged)
	{
		*bOutChanged = false;
	}

	if (!WidgetBlueprint)
	{
		return AnimationFailure(TEXT("WidgetBlueprint is required for applying animations"), TEXT("/Body/Animations"), TEXT("UnsupportedAsset"));
	}

	const FAssetDocumentCapabilityResult CurrentResult = CheckForUnsupportedCurrentTracks(WidgetBlueprint);
	if (!CurrentResult.bSuccess)
	{
		return CurrentResult;
	}

	TArray<FWidgetAnimationSpec> DesiredSpecs;
	FAssetDocumentCapabilityResult Result = ParseAnimationSpecs(AnimationsJson, DesiredSpecs);
	if (!Result.bSuccess)
	{
		return Result;
	}
	Result = ValidateSpecsAgainstWidgetBlueprint(WidgetBlueprint, DesiredSpecs);
	if (!Result.bSuccess)
	{
		return Result;
	}

	TArray<FWidgetAnimationSpec> CurrentSpecs;
	Result = ExtractAnimationSpecs(WidgetBlueprint, CurrentSpecs);
	if (!Result.bSuccess)
	{
		return Result;
	}
	const FString CurrentComparable = JsonValueToComparableString(AnimationSpecsToJsonValue(CurrentSpecs));
	const FString DesiredComparable = JsonValueToComparableString(AnimationSpecsToJsonValue(DesiredSpecs));
	if (CurrentComparable == DesiredComparable)
	{
		return FAssetDocumentCapabilityResult::Success(TEXT("Applied WidgetBlueprint Animations"));
	}

	TArray<UWidgetAnimation*> StagedAnimations;
	for (const FWidgetAnimationSpec& Spec : DesiredSpecs)
	{
		const FName StagedName = MakeUniqueObjectName(
			WidgetBlueprint,
			UWidgetAnimation::StaticClass(),
			FName(*(Spec.Name.ToString() + TEXT("_AssetDocumentStaged"))));
		UWidgetAnimation* StagedAnimation = nullptr;
		Result = MaterializeAnimation(WidgetBlueprint, Spec, StagedName, StagedAnimation);
		if (!Result.bSuccess)
		{
			for (UWidgetAnimation* Animation : StagedAnimations)
			{
				MoveAnimationToTransient(Animation);
			}
			return Result;
		}
		StagedAnimations.Add(StagedAnimation);
	}

	WidgetBlueprint->Modify();
	TArray<FExistingAnimationState> ExistingAnimations;
	RenameExistingAnimationsOutOfTheWay(WidgetBlueprint, ExistingAnimations);
	for (int32 Index = 0; Index < DesiredSpecs.Num(); ++Index)
	{
		UWidgetAnimation* Animation = StagedAnimations.IsValidIndex(Index) ? StagedAnimations[Index] : nullptr;
		if (!Animation || !Animation->Rename(*DesiredSpecs[Index].Name.ToString(), WidgetBlueprint, REN_DontCreateRedirectors | REN_NonTransactional))
		{
			RestoreExistingAnimations(WidgetBlueprint, ExistingAnimations);
			for (UWidgetAnimation* StagedAnimation : StagedAnimations)
			{
				MoveAnimationToTransient(StagedAnimation);
			}
			return AnimationFailure(TEXT("Failed to finalize WidgetAnimation replacement"), DesiredSpecs[Index].JsonPath, TEXT("CreateWidgetAnimationFailed"));
		}
		WidgetBlueprint->Animations.Add(Animation);
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
	if (bOutChanged)
	{
		*bOutChanged = true;
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Applied WidgetBlueprint Animations"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationAdapter::Extract(const UWidgetBlueprint* WidgetBlueprint, TArray<TSharedPtr<FJsonValue>>& OutAnimations)
{
	OutAnimations.Reset();
	TArray<FWidgetAnimationSpec> Specs;
	const FAssetDocumentCapabilityResult Result = ExtractAnimationSpecs(WidgetBlueprint, Specs);
	if (!Result.bSuccess)
	{
		return Result;
	}
	const TSharedPtr<FJsonValue> JsonValue = AnimationSpecsToJsonValue(Specs);
	if (JsonValue.IsValid() && JsonValue->Type == EJson::Array)
	{
		OutAnimations = JsonValue->AsArray();
	}
	return FAssetDocumentCapabilityResult::Success(TEXT("Extracted WidgetBlueprint Animations"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationAdapter::Diff(
	const UWidgetBlueprint* WidgetBlueprint,
	const TSharedPtr<FJsonValue>& DesiredJson,
	TArray<TSharedPtr<FJsonValue>>& OutDiffEntries)
{
	TSharedPtr<FJsonValue> CurrentValue;
	{
		TArray<FWidgetAnimationSpec> CurrentSpecs;
		const FAssetDocumentCapabilityResult ExtractResult = ExtractAnimationSpecs(WidgetBlueprint, CurrentSpecs);
		if (!ExtractResult.bSuccess)
		{
			return ExtractResult;
		}
		CurrentValue = AnimationSpecsToJsonValue(CurrentSpecs);
	}

	TSharedPtr<FJsonValue> DesiredValue;
	const FAssetDocumentCapabilityResult DesiredResult = CanonicalizeDesired(DesiredJson, DesiredValue);
	if (!DesiredResult.bSuccess)
	{
		return DesiredResult;
	}

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), TEXT("/Body/Animations"));
	Entry->SetStringField(
		TEXT("status"),
		JsonValueToComparableString(CurrentValue) == JsonValueToComparableString(DesiredValue) ? TEXT("unchanged") : TEXT("changed"));
	Entry->SetField(TEXT("current"), CurrentValue.IsValid() ? CurrentValue : MakeShared<FJsonValueNull>());
	Entry->SetField(TEXT("desired"), DesiredValue.IsValid() ? DesiredValue : MakeShared<FJsonValueNull>());
	OutDiffEntries.Add(MakeShared<FJsonValueObject>(Entry));
	return FAssetDocumentCapabilityResult::Success(TEXT("Diffed WidgetBlueprint Animations"));
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationAdapter::CheckForUnsupportedCurrentTracks(const UWidgetBlueprint* WidgetBlueprint)
{
	TArray<FWidgetAnimationSpec> Specs;
	return ExtractAnimationSpecs(WidgetBlueprint, Specs);
}

FAssetDocumentCapabilityResult FWidgetBlueprintAnimationAdapter::CanonicalizeDesired(const TSharedPtr<FJsonValue>& AnimationsJson, TSharedPtr<FJsonValue>& OutCanonicalJson)
{
	TArray<FWidgetAnimationSpec> Specs;
	const FAssetDocumentCapabilityResult Result = ParseAnimationSpecs(AnimationsJson, Specs);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutCanonicalJson = AnimationSpecsToJsonValue(Specs);
	return FAssetDocumentCapabilityResult::Success();
}
