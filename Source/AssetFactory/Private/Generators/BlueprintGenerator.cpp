// Copyright ProjectRPG. All Rights Reserved.

#include "Generators/BlueprintGenerator.h"
#include "AssetFactoryModule.h"
#include "Utils/ClassFinderUtils.h"
#include "Utils/PropertySetterUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "UObject/SavePackage.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/Class.h"

FGenerationResult FBlueprintGenerator::Generate(
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

	// Get parent class using utility
	FString ParentClassName = GetStringField(Config, TEXT("ParentClass"), TEXT("Actor"));
	UClass* ParentClass = FClassFinderUtils::FindClassByName(ParentClassName, UObject::StaticClass(), true);
	if (!ParentClass)
	{
		// Fallback to AActor if not found
		UE_LOG(LogAssetFactory, Warning, TEXT("Parent class '%s' not found, using AActor"), *ParentClassName);
		ParentClass = AActor::StaticClass();
	}

	UBlueprint* Blueprint = nullptr;

	if (bExists)
	{
		Blueprint = Cast<UBlueprint>(LoadExistingAsset(Path, Name));
		if (!Blueprint)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to load existing blueprint"));
		}
	}
	else
	{
		// Create new blueprint
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
		Factory->ParentClass = ParentClass;

		Blueprint = Cast<UBlueprint>(AssetTools.CreateAsset(Name, Path, UBlueprint::StaticClass(), Factory));
		if (!Blueprint)
		{
			return FGenerationResult::MakeFailed(GetAssetType(), Name, Path, TEXT("Failed to create blueprint"));
		}
	}

	// Add interfaces using utility
	const TArray<TSharedPtr<FJsonValue>>* InterfacesArray = GetArrayField(Config, TEXT("Interfaces"));
	if (InterfacesArray)
	{
		for (const TSharedPtr<FJsonValue>& InterfaceValue : *InterfacesArray)
		{
			FString InterfaceName;
			if (InterfaceValue->TryGetString(InterfaceName))
			{
				UClass* InterfaceClass = FClassFinderUtils::FindInterfaceClass(InterfaceName);
				if (InterfaceClass)
				{
					// Check if interface is already implemented
					bool bAlreadyImplemented = false;
					for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
					{
						if (Desc.Interface == InterfaceClass)
						{
							bAlreadyImplemented = true;
							break;
						}
					}

					if (!bAlreadyImplemented)
					{
						FTopLevelAssetPath InterfacePath(InterfaceClass->GetPathName());
						FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfacePath);
					}
				}
				else
				{
					UE_LOG(LogAssetFactory, Warning, TEXT("Interface class '%s' not found"), *InterfaceName);
				}
			}
		}
	}

	// Add variables
	const TArray<TSharedPtr<FJsonValue>>* VariablesArray = GetArrayField(Config, TEXT("Variables"));
	if (VariablesArray)
	{
		AddVariables(Blueprint, VariablesArray);
	}

	// Add components (only for Actor-based blueprints)
	const TArray<TSharedPtr<FJsonValue>>* ComponentsArray = GetArrayField(Config, TEXT("Components"));
	if (ComponentsArray)
	{
		AddComponents(Blueprint, ComponentsArray);
	}

	// Set default properties on CDO
	TSharedPtr<FJsonObject> DefaultProperties = GetObjectField(Config, TEXT("DefaultProperties"));
	if (DefaultProperties.IsValid())
	{
		SetDefaultProperties(Blueprint, DefaultProperties);
	}

	// Compile blueprint
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// Save
	Blueprint->MarkPackageDirty();

	UPackage* Package = Blueprint->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Blueprint, *PackageFileName, SaveArgs);

	if (bExists)
	{
		return FGenerationResult::MakeUpdated(GetAssetType(), Name, Path, Blueprint);
	}
	return FGenerationResult::MakeSuccess(GetAssetType(), Name, Path, Blueprint);
}

void FBlueprintGenerator::SetDefaultProperties(UBlueprint* Blueprint, TSharedPtr<FJsonObject> Properties)
{
	if (!Blueprint || !Properties.IsValid())
	{
		return;
	}

	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (!GeneratedClass)
	{
		return;
	}

	UObject* CDO = GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		return;
	}

	// Detect format: check if first property has "type" field (new typed format)
	bool bUseTypedFormat = false;
	for (const auto& Pair : Properties->Values)
	{
		const TSharedPtr<FJsonObject>* PropObj;
		if (Pair.Value->TryGetObject(PropObj) && (*PropObj)->HasField(TEXT("type")))
		{
			bUseTypedFormat = true;
		}
		break; // Only check first property
	}

	// Use PropertySetterUtils to set properties on CDO
	if (bUseTypedFormat)
	{
		FPropertySetterUtils::SetTypedPropertiesFromJson(CDO, Properties);
	}
	else
	{
		FPropertySetterUtils::SetPropertiesFromJson(CDO, Properties);
	}

	// Mark CDO as modified
	CDO->Modify();
}

TOptional<FString> FBlueprintGenerator::ValidateConfig(TSharedPtr<FJsonObject> Config) const
{
	if (!Config.IsValid())
	{
		return FString(TEXT("Invalid configuration object"));
	}

	// ParentClass is optional (defaults to Actor), so no required fields
	// But if specified, we could validate it exists

	return TOptional<FString>();
}

TArray<FString> FBlueprintGenerator::GetRequiredFields() const
{
	// No strictly required fields - ParentClass defaults to "Actor"
	return {};
}

void FBlueprintGenerator::AddVariables(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>* VariablesArray)
{
	if (!Blueprint || !VariablesArray)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& VarValue : *VariablesArray)
	{
		const TSharedPtr<FJsonObject>* VarObject;
		if (!VarValue->TryGetObject(VarObject) || !VarObject->IsValid())
		{
			continue;
		}

		FString VarName;
		if (!(*VarObject)->TryGetStringField(TEXT("Name"), VarName) || VarName.IsEmpty())
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Variable missing 'Name' field"));
			continue;
		}

		FString TypeString;
		if (!(*VarObject)->TryGetStringField(TEXT("Type"), TypeString))
		{
			TypeString = TEXT("Float"); // Default
		}

		// Get pin type from string
		FEdGraphPinType PinType = GetPinTypeFromString(TypeString);

		// Add the variable
		FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VarName), PinType);

		// Set default value if provided
		FString DefaultValue;
		if ((*VarObject)->TryGetStringField(TEXT("DefaultValue"), DefaultValue))
		{
			// Find the variable and set its default
			for (FBPVariableDescription& Var : Blueprint->NewVariables)
			{
				if (Var.VarName == FName(*VarName))
				{
					Var.DefaultValue = DefaultValue;
					break;
				}
			}
		}

		UE_LOG(LogAssetFactory, Log, TEXT("Added variable '%s' of type '%s'"), *VarName, *TypeString);
	}
}

void FBlueprintGenerator::AddComponents(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>* ComponentsArray)
{
	if (!Blueprint || !ComponentsArray)
	{
		return;
	}

	// Check if blueprint is Actor-based
	if (!Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
	{
		UE_LOG(LogAssetFactory, Warning, TEXT("Components can only be added to Actor-based blueprints"));
		return;
	}

	// Ensure SimpleConstructionScript exists
	if (!Blueprint->SimpleConstructionScript)
	{
		Blueprint->SimpleConstructionScript = NewObject<USimpleConstructionScript>(Blueprint);
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	TMap<FString, USCS_Node*> NodeMap; // For AttachTo lookup

	// Build map of existing nodes for update detection and AttachTo lookup
	TMap<FString, USCS_Node*> ExistingNodes;
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node && Node->ComponentTemplate)
		{
			FString NodeName = Node->GetVariableName().ToString();
			ExistingNodes.Add(NodeName, Node);
			NodeMap.Add(NodeName, Node); // Also add to NodeMap so AttachTo can find existing nodes
		}
	}

	for (const TSharedPtr<FJsonValue>& CompValue : *ComponentsArray)
	{
		const TSharedPtr<FJsonObject>* CompObject;
		if (!CompValue->TryGetObject(CompObject) || !CompObject->IsValid())
		{
			continue;
		}

		FString CompName;
		if (!(*CompObject)->TryGetStringField(TEXT("Name"), CompName) || CompName.IsEmpty())
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Component missing 'Name' field"));
			continue;
		}

		// Check Action field (default: CreateOrUpdate)
		FString ActionStr;
		(*CompObject)->TryGetStringField(TEXT("Action"), ActionStr);

		// Handle Remove action
		if (ActionStr.Equals(TEXT("Remove"), ESearchCase::IgnoreCase))
		{
			if (USCS_Node** FoundNode = ExistingNodes.Find(CompName))
			{
				USCS_Node* NodeToRemove = *FoundNode;
				SCS->RemoveNode(NodeToRemove);
				ExistingNodes.Remove(CompName);
				UE_LOG(LogAssetFactory, Log, TEXT("Removed component '%s'"), *CompName);
			}
			else
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Component '%s' not found for removal"), *CompName);
			}
			continue;
		}

		FString ClassName;
		if (!(*CompObject)->TryGetStringField(TEXT("Class"), ClassName))
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Component '%s' missing 'Class' field"), *CompName);
			continue;
		}

		// Check if component already exists (for update scenarios)
		USCS_Node* ExistingNode = nullptr;
		if (USCS_Node** FoundNode = ExistingNodes.Find(CompName))
		{
			ExistingNode = *FoundNode;
		}

		// Dynamically find component class using ClassFinderUtils
		UClass* ComponentClass = FClassFinderUtils::FindClassByName(ClassName, UActorComponent::StaticClass(), true);
		if (!ComponentClass)
		{
			UE_LOG(LogAssetFactory, Warning, TEXT("Component class '%s' not found"), *ClassName);
			continue;
		}

		USCS_Node* TargetNode = ExistingNode;

		// Only create new node if component doesn't exist
		if (!TargetNode)
		{
			TargetNode = SCS->CreateNode(ComponentClass, FName(*CompName));
			if (!TargetNode)
			{
				UE_LOG(LogAssetFactory, Warning, TEXT("Failed to create SCS node for component '%s'"), *CompName);
				continue;
			}
		}

		NodeMap.Add(CompName, TargetNode);

		// Set component properties if provided
		TSharedPtr<FJsonObject> PropertiesObject;
		if ((*CompObject)->HasTypedField<EJson::Object>(TEXT("Properties")))
		{
			PropertiesObject = (*CompObject)->GetObjectField(TEXT("Properties"));
		}
		if (PropertiesObject.IsValid() && TargetNode->ComponentTemplate)
		{
			// Detect format: check if first property has "type" field (new typed format)
			bool bUseTypedFormat = false;
			for (const auto& Pair : PropertiesObject->Values)
			{
				const TSharedPtr<FJsonObject>* PropObj;
				if (Pair.Value->TryGetObject(PropObj) && (*PropObj)->HasField(TEXT("type")))
				{
					bUseTypedFormat = true;
				}
				break; // Only check first property
			}

			if (bUseTypedFormat)
			{
				FPropertySetterUtils::SetTypedPropertiesFromJson(TargetNode->ComponentTemplate, PropertiesObject);
			}
			else
			{
				FPropertySetterUtils::SetPropertiesFromJson(TargetNode->ComponentTemplate, PropertiesObject);
			}
			UE_LOG(LogAssetFactory, Log, TEXT("Set properties on component '%s' (typed=%d)"), *CompName, bUseTypedFormat);
		}

		// Skip hierarchy setup if component already existed
		if (ExistingNode)
		{
			UE_LOG(LogAssetFactory, Log, TEXT("Updated existing component '%s'"), *CompName);
			continue;
		}

		// Check if this is root
		bool bIsRoot = false;
		(*CompObject)->TryGetBoolField(TEXT("bIsRoot"), bIsRoot);

		// Check AttachTo
		FString AttachTo;
		(*CompObject)->TryGetStringField(TEXT("AttachTo"), AttachTo);

		if (bIsRoot)
		{
			// Set as root node - for scene components, this becomes the default scene root
			if (ComponentClass->IsChildOf(USceneComponent::StaticClass()) && !SCS->GetDefaultSceneRootNode())
			{
				// Add as the default scene root
				SCS->AddNode(TargetNode);
				// The first SceneComponent added becomes the root automatically
			}
			else
			{
				SCS->AddNode(TargetNode);
			}
		}
		else if (!AttachTo.IsEmpty())
		{
			// Attach to specified parent
			USCS_Node** ParentNode = NodeMap.Find(AttachTo);
			if (ParentNode && *ParentNode)
			{
				(*ParentNode)->AddChildNode(TargetNode);
			}
			else
			{
				// Parent not found yet, add to root for now
				SCS->AddNode(TargetNode);
			}
		}
		else
		{
			// No parent specified, add to root
			SCS->AddNode(TargetNode);
		}

		UE_LOG(LogAssetFactory, Log, TEXT("Added component '%s' of class '%s'"), *CompName, *ClassName);
	}
}

FEdGraphPinType FBlueprintGenerator::GetPinTypeFromString(const FString& TypeString)
{
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; // Default

	// Use case-insensitive comparison
	FString TypeLower = TypeString.ToLower();

	if (TypeLower == TEXT("bool") || TypeLower == TEXT("boolean"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (TypeLower == TEXT("byte"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (TypeLower == TEXT("int") || TypeLower == TEXT("int32") || TypeLower == TEXT("integer"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (TypeLower == TEXT("int64"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (TypeLower == TEXT("float") || TypeLower == TEXT("real"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (TypeLower == TEXT("double"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (TypeLower == TEXT("name") || TypeLower == TEXT("fname"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (TypeLower == TEXT("string") || TypeLower == TEXT("fstring"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (TypeLower == TEXT("text") || TypeLower == TEXT("ftext"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (TypeLower == TEXT("vector") || TypeLower == TEXT("fvector"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (TypeLower == TEXT("rotator") || TypeLower == TEXT("frotator"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (TypeLower == TEXT("transform") || TypeLower == TEXT("ftransform"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (TypeLower == TEXT("color") || TypeLower == TEXT("fcolor") || TypeLower == TEXT("linearcolor") || TypeLower == TEXT("flinearcolor"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
	}
	else
	{
		// Try to find as a class or struct
		UClass* FoundClass = FClassFinderUtils::FindClassByName(TypeString, UObject::StaticClass(), false);
		if (FoundClass)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = FoundClass;
		}
		else
		{
			// Default to float if unknown
			UE_LOG(LogAssetFactory, Warning, TEXT("Unknown type '%s', defaulting to Float"), *TypeString);
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		}
	}

	return PinType;
}

//~ Extract Implementation

bool FBlueprintGenerator::CanExtract(UObject* Asset) const
{
	// Can extract from UBlueprint but not UWidgetBlueprint (handled by WidgetBlueprintGenerator)
	if (!Asset)
	{
		return false;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (!Blueprint)
	{
		return false;
	}

	// Exclude WidgetBlueprint - it has its own generator
	if (Asset->IsA<UWidgetBlueprint>())
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FBlueprintGenerator::Extract(UObject* Asset, bool bDiffOnly) const
{
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (!Blueprint)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();

	// ParentClass
	if (Blueprint->ParentClass)
	{
		// GetName() already returns the name without prefix (e.g., "Actor" not "AActor")
		Config->SetStringField(TEXT("ParentClass"), Blueprint->ParentClass->GetName());
	}

	// Components (for Actor-based blueprints)
	TArray<TSharedPtr<FJsonValue>> ComponentsArray = ExtractComponents(Blueprint);
	if (ComponentsArray.Num() > 0)
	{
		Config->SetArrayField(TEXT("Components"), ComponentsArray);
	}

	// Variables
	TArray<TSharedPtr<FJsonValue>> VariablesArray = ExtractVariables(Blueprint);
	if (VariablesArray.Num() > 0)
	{
		Config->SetArrayField(TEXT("Variables"), VariablesArray);
	}

	return Config;
}

TArray<TSharedPtr<FJsonValue>> FBlueprintGenerator::ExtractComponents(UBlueprint* Blueprint) const
{
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;

	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return ComponentsArray;
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	TArray<USCS_Node*> AllNodes = SCS->GetAllNodes();

	// Build parent map for AttachTo
	TMap<USCS_Node*, USCS_Node*> ParentMap;
	for (USCS_Node* Node : AllNodes)
	{
		for (USCS_Node* ChildNode : Node->GetChildNodes())
		{
			ParentMap.Add(ChildNode, Node);
		}
	}

	// Find root node
	USCS_Node* RootNode = SCS->GetDefaultSceneRootNode();

	for (USCS_Node* Node : AllNodes)
	{
		if (!Node || !Node->ComponentTemplate)
		{
			continue;
		}

		TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();

		// Name
		CompObj->SetStringField(TEXT("Name"), Node->GetVariableName().ToString());

		// Class
		FString ClassName = Node->ComponentClass->GetName();
		if (ClassName.StartsWith(TEXT("U")))
		{
			ClassName = ClassName.Mid(1);
		}
		CompObj->SetStringField(TEXT("Class"), ClassName);

		// bIsRoot
		if (Node == RootNode)
		{
			CompObj->SetBoolField(TEXT("bIsRoot"), true);
		}

		// AttachTo
		USCS_Node** ParentNodePtr = ParentMap.Find(Node);
		if (ParentNodePtr && *ParentNodePtr)
		{
			CompObj->SetStringField(TEXT("AttachTo"), (*ParentNodePtr)->GetVariableName().ToString());
		}

		// Properties (only extract non-default values)
		TSharedPtr<FJsonObject> PropertiesObj = ExtractComponentProperties(Node->ComponentTemplate, true);
		if (PropertiesObj.IsValid() && PropertiesObj->Values.Num() > 0)
		{
			CompObj->SetObjectField(TEXT("Properties"), PropertiesObj);
		}

		ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
	}

	return ComponentsArray;
}

TArray<TSharedPtr<FJsonValue>> FBlueprintGenerator::ExtractVariables(UBlueprint* Blueprint) const
{
	TArray<TSharedPtr<FJsonValue>> VariablesArray;

	if (!Blueprint)
	{
		return VariablesArray;
	}

	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();

		// Name
		VarObj->SetStringField(TEXT("Name"), Var.VarName.ToString());

		// Type
		VarObj->SetStringField(TEXT("Type"), PinTypeToString(Var.VarType));

		// DefaultValue (if has one)
		if (!Var.DefaultValue.IsEmpty())
		{
			VarObj->SetStringField(TEXT("DefaultValue"), Var.DefaultValue);
		}

		VariablesArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}

	return VariablesArray;
}

FString FBlueprintGenerator::PinTypeToString(const FEdGraphPinType& PinType) const
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		return TEXT("Boolean");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		return TEXT("Byte");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
	{
		return TEXT("Int");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
	{
		return TEXT("Int64");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		if (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
		{
			return TEXT("Double");
		}
		return TEXT("Float");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		return TEXT("Name");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		return TEXT("String");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		return TEXT("Text");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		if (UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()))
		{
			return Struct->GetName();
		}
		return TEXT("Struct");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		if (UClass* Class = Cast<UClass>(PinType.PinSubCategoryObject.Get()))
		{
			return Class->GetName();
		}
		return TEXT("Object");
	}

	return TEXT("Unknown");
}

TSharedPtr<FJsonObject> FBlueprintGenerator::ExtractComponentProperties(UActorComponent* Component, bool bDiffOnly) const
{
	if (!Component)
	{
		return nullptr;
	}

	// Use the generic property extraction utility
	TSharedPtr<FJsonObject> AllProperties = FPropertySetterUtils::ExtractPropertiesToJson(Component, true, bDiffOnly);

	if (!AllProperties.IsValid())
	{
		return nullptr;
	}

	// Filter out properties from base classes (UActorComponent, USceneComponent, UObject)
	TSharedPtr<FJsonObject> FilteredProperties = MakeShared<FJsonObject>();
	UClass* ComponentClass = Component->GetClass();

	for (const auto& Pair : AllProperties->Values)
	{
		FProperty* Property = ComponentClass->FindPropertyByName(*Pair.Key);
		if (Property)
		{
			UClass* OwnerClass = Property->GetOwnerClass();
			// Keep properties that are NOT from base infrastructure classes
			if (OwnerClass != UActorComponent::StaticClass() &&
				OwnerClass != USceneComponent::StaticClass() &&
				OwnerClass != UObject::StaticClass())
			{
				FilteredProperties->SetField(Pair.Key, Pair.Value);
			}
			// Also keep important SceneComponent properties
			else if (OwnerClass == USceneComponent::StaticClass())
			{
				FString PropName = Property->GetName();
				if (PropName == TEXT("RelativeLocation") ||
					PropName == TEXT("RelativeRotation") ||
					PropName == TEXT("RelativeScale3D") ||
					PropName == TEXT("bVisible") ||
					PropName == TEXT("bHiddenInGame"))
				{
					FilteredProperties->SetField(Pair.Key, Pair.Value);
				}
			}
		}
	}

	return FilteredProperties;
}
