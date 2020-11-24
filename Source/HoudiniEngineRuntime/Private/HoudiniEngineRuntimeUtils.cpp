/*
* Copyright (c) <2018> Side Effects Software Inc.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. The name of Side Effects Software may not be used to endorse or
*    promote products derived from this software without specific prior
*    written permission.
*
* THIS SOFTWARE IS PROVIDED BY SIDE EFFECTS SOFTWARE "AS IS" AND ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
* NO EVENT SHALL SIDE EFFECTS SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "HoudiniEngineRuntimeUtils.h"
//@THE_COALITION_CHANGE: ryandow@microsoft.com - BEGIN [Fix non-unity non-pch]
#include "HoudiniEngineRuntimePrivatePCH.h"
//@THE_COALITION_CHANGE: ryandow@microsoft.com - END [Fix non-unity non-pch]
#include "EngineUtils.h"

#if WITH_EDITOR
	#include "Editor.h"
#endif

FString
FHoudiniEngineRuntimeUtils::GetLibHAPIName()
{
	static const FString LibHAPIName =

#if PLATFORM_WINDOWS
		HAPI_LIB_OBJECT_WINDOWS;
#elif PLATFORM_MAC
		HAPI_LIB_OBJECT_MAC;
#elif PLATFORM_LINUX
		HAPI_LIB_OBJECT_LINUX;
#else
		TEXT("");
#endif

	return LibHAPIName;
}


void 
FHoudiniEngineRuntimeUtils::GetBoundingBoxesFromActors(const TArray<AActor*> InActors, TArray<FBox>& OutBBoxes)
{
	OutBBoxes.Empty();

	for (auto CurrentActor : InActors)
	{
		if (!CurrentActor || CurrentActor->IsPendingKill())
			continue;

		OutBBoxes.Add(CurrentActor->GetComponentsBoundingBox(true, true));
	}
}


bool 
FHoudiniEngineRuntimeUtils::FindActorsOfClassInBounds(UWorld* World, TSubclassOf<AActor> ActorType, const TArray<FBox>& BBoxes, const TArray<AActor*>* ExcludeActors, TArray<AActor*>& OutActors)
{
	if (!IsValid(World))
		return false;
	
	OutActors.Empty();
	for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr)
	{
		AActor* CurrentActor = *ActorItr;
		if (!IsValid(CurrentActor))
			continue;
		
		if (!CurrentActor->GetClass()->IsChildOf(ActorType.Get()))
			continue;

		if (ExcludeActors && ExcludeActors->Contains(CurrentActor))
			continue;

		// Special case
		// Ignore the SkySpheres?
		FString ClassName = CurrentActor->GetClass() ? CurrentActor->GetClass()->GetName() : FString();
		if (ClassName.Contains("BP_Sky_Sphere"))
			continue;

		FBox ActorBounds = CurrentActor->GetComponentsBoundingBox(true);
		for (auto InBounds : BBoxes)
		{
			// Check if both actor's bounds intersects
			if (!ActorBounds.Intersect(InBounds))
				continue;

			OutActors.Add(CurrentActor);
			break;
		}
	}

	return true;
}

bool
FHoudiniEngineRuntimeUtils::SafeDeleteSingleObject(UObject* const InObjectToDelete, UPackage*& OutPackage, bool& bOutPackageIsInMemoryOnly)
{
	bool bDeleted = false;
	OutPackage = nullptr;
	bOutPackageIsInMemoryOnly = false;
	
	if (!IsValid(InObjectToDelete))
		return false;

	// Don't try to delete the object if it has references (we do this here to avoid the FMessageDialog in DeleteSingleObject
	bool bIsReferenced = false;
	bool bIsReferencedByUndo = false;
	if (!GatherObjectReferencersForDeletion(InObjectToDelete, bIsReferenced, bIsReferencedByUndo))
		return false;

	if (bIsReferenced)
	{
		HOUDINI_LOG_WARNING(TEXT("[FHoudiniEngineRuntimeUtils::SafeDeleteSingleObject] Not deleting %s: there are still references to it."), *InObjectToDelete->GetFullName());
	}
	else
	{
		// Even though we already checked for references, we still let DeleteSingleObject check for references, since
		// we want that code path where it'll clean up in-memory references (undo buffer/transactions)
		const bool bCheckForReferences = true;
		if (DeleteSingleObject(InObjectToDelete, bCheckForReferences))
		{
			bDeleted = true;
			
			OutPackage = InObjectToDelete->GetOutermost();
			
			FString PackageFilename;
			if (!IsValid(OutPackage) || !FPackageName::DoesPackageExist(OutPackage->GetName(), nullptr, &PackageFilename))
			{
				// Package is in memory only, we don't have call CleanUpAfterSuccessfulDelete on it, just do garbage
				// collection to pick up the stale package
				bOutPackageIsInMemoryOnly = true;
			}
			else
			{
				// There is an on-disk package that is now potentially empty, CleanUpAfterSuccessfulDelete must be
				// called on this. Since CleanUpAfterSuccessfulDelete does garbage collection, we return the Package
				// as part of this function so that the caller can collect all Packages and do one call to
				// CleanUpAfterSuccessfulDelete with an array
			}
		}
	}

	return bDeleted;
}

int32
FHoudiniEngineRuntimeUtils::SafeDeleteObjects(TArray<UObject*>& InObjectsToDelete, TArray<UObject*>* OutObjectsNotDeleted)
{
	int32 NumDeleted = 0;
	bool bGarbageCollectionRequired = false;
	TSet<UPackage*> PackagesToCleanUp;
	TSet<UObject*> ProcessedObjects;
	while (InObjectsToDelete.Num() > 0)
	{
		UObject* const ObjectToDelete = InObjectsToDelete.Pop();

		if (ProcessedObjects.Contains(ObjectToDelete))
			continue;
		
		ProcessedObjects.Add(ObjectToDelete);
		
		if (!IsValid(ObjectToDelete))
			continue;

		UPackage* Package = nullptr;
		bool bInMemoryPackageOnly = false;
		if (SafeDeleteSingleObject(ObjectToDelete, Package, bInMemoryPackageOnly))
		{
			NumDeleted++;
			if (bInMemoryPackageOnly)
			{
				// Packages that are in-memory only are cleaned up by garbage collection
				if (!bGarbageCollectionRequired)
					bGarbageCollectionRequired = true;
			}
			else
			{
				// Clean up potentially empty packages in one call to CleanupAfterSuccessfulDelete at the end
				PackagesToCleanUp.Add(Package);
			}
		}
		else if (OutObjectsNotDeleted)
		{
			OutObjectsNotDeleted->Add(ObjectToDelete);
		}
	}

	// CleanupAfterSuccessfulDelete calls CollectGarbage, so don't call it here if we have PackagesToCleanUp
	if (bGarbageCollectionRequired && PackagesToCleanUp.Num() <= 0)
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	if (PackagesToCleanUp.Num() > 0)
		CleanupAfterSuccessfulDelete(PackagesToCleanUp.Array());

	return NumDeleted;
}


#if WITH_EDITOR
int32
FHoudiniEngineRuntimeUtils::CopyComponentProperties(UActorComponent* SourceComponent, UActorComponent* TargetComponent, const EditorUtilities::FCopyOptions& Options)
{
	UClass* ComponentClass = SourceComponent->GetClass();
	check( ComponentClass == TargetComponent->GetClass() );

	const bool bIsPreviewing = ( Options.Flags & EditorUtilities::ECopyOptions::PreviewOnly ) != 0;
	int32 CopiedPropertyCount = 0;
	bool bTransformChanged = false;

	// Build a list of matching component archetype instances for propagation (if requested)
	TArray<UActorComponent*> ComponentArchetypeInstances;
	if( Options.Flags & EditorUtilities::ECopyOptions::PropagateChangesToArchetypeInstances )
	{
		TArray<UObject*> Instances;
		TargetComponent->GetArchetypeInstances(Instances);
		for(UObject* ObjInstance : Instances)
		{
			UActorComponent* ComponentInstance = Cast<UActorComponent>(ObjInstance);
			if (ComponentInstance && ComponentInstance != SourceComponent && ComponentInstance != TargetComponent)
			{ 
				ComponentArchetypeInstances.Add(ComponentInstance);
			}
		}
	}

	TSet<const FProperty*> SourceUCSModifiedProperties;
	SourceComponent->GetUCSModifiedProperties(SourceUCSModifiedProperties);

	TArray<UActorComponent*> ComponentInstancesToReregister;

	// Copy component properties
	for( FProperty* Property = ComponentClass->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext )
	{
		const bool bIsTransient = !!( Property->PropertyFlags & CPF_Transient );
		const bool bIsIdentical = Property->Identical_InContainer( SourceComponent, TargetComponent );
		const bool bIsComponent = !!( Property->PropertyFlags & ( CPF_InstancedReference | CPF_ContainsInstancedReference ) );
		const bool bIsTransform =
			Property->GetFName() == USceneComponent::GetRelativeScale3DPropertyName() ||
			Property->GetFName() == USceneComponent::GetRelativeLocationPropertyName() ||
			Property->GetFName() == USceneComponent::GetRelativeRotationPropertyName();

		// auto SourceComponentIsRoot = [&]()
		// {
		// 	USceneComponent* RootComponent = SourceActor->GetRootComponent();
		// 	if (SourceComponent == RootComponent)
		// 	{
		// 		return true;
		// 	}
		// 	else if (RootComponent == nullptr && bSourceActorIsBPCDO)
		// 	{
		// 		// If we're dealing with a BP CDO as source, then look at the target for whether this is the root component
		// 		return (TargetComponent == TargetActor->GetRootComponent());
		// 	}
		// 	return false;
		// };

		TSet<UObject*> ModifiedObjects;
		// if( !bIsTransient && !bIsIdentical && !bIsComponent && !SourceUCSModifiedProperties.Contains(Property)
		// 	&& ( !bIsTransform || (!bSourceActorIsCDO && !bTargetActorIsCDO) || !SourceComponentIsRoot() ) )
		if( !bIsTransient && !bIsIdentical && !bIsComponent && !SourceUCSModifiedProperties.Contains(Property)
			&& ( !bIsTransform ))
		{
			const bool bIsSafeToCopy = (!(Options.Flags & EditorUtilities::ECopyOptions::OnlyCopyEditOrInterpProperties) || (Property->HasAnyPropertyFlags(CPF_Edit | CPF_Interp)))
				                    && (!(Options.Flags & EditorUtilities::ECopyOptions::SkipInstanceOnlyProperties) || (!Property->HasAllPropertyFlags(CPF_DisableEditOnTemplate)));
			if( bIsSafeToCopy )
			{
				// if (!Options.CanCopyProperty(*Property, *SourceActor))
				// {
				// 	continue;
				// }
				if (!Options.CanCopyProperty(*Property, *SourceComponent))
				{
					continue;
				}
					
				if( !bIsPreviewing )
				{
					if( !ModifiedObjects.Contains(TargetComponent) )
					{
						TargetComponent->SetFlags(RF_Transactional);
						TargetComponent->Modify();
						ModifiedObjects.Add(TargetComponent);
					}

					if( Options.Flags & EditorUtilities::ECopyOptions::CallPostEditChangeProperty )
					{
						TargetComponent->PreEditChange( Property );
					}

					// Determine which component archetype instances match the current property value of the target component (before it gets changed). We only want to propagate the change to those instances.
					TArray<UActorComponent*> ComponentArchetypeInstancesToChange;
					if( Options.Flags & EditorUtilities::ECopyOptions::PropagateChangesToArchetypeInstances )
					{
						for (UActorComponent* ComponentArchetypeInstance : ComponentArchetypeInstances)
						{
							if( ComponentArchetypeInstance != nullptr && Property->Identical_InContainer( ComponentArchetypeInstance, TargetComponent ) )
							{
								bool bAdd = true;
								// We also need to double check that either the direct archetype of the target is also identical
								if (ComponentArchetypeInstance->GetArchetype() != TargetComponent)
								{
									UActorComponent* CheckComponent = CastChecked<UActorComponent>(ComponentArchetypeInstance->GetArchetype());
									while (CheckComponent != ComponentArchetypeInstance)
									{
										if (!Property->Identical_InContainer( CheckComponent, TargetComponent ))
										{
											bAdd = false;
											break;
										}
										CheckComponent = CastChecked<UActorComponent>(CheckComponent->GetArchetype());
									}
								}
									
								if (bAdd)
								{
									ComponentArchetypeInstancesToChange.Add( ComponentArchetypeInstance );
								}
							}
						}
					}

					EditorUtilities::CopySingleProperty(SourceComponent, TargetComponent, Property);

					if( Options.Flags & EditorUtilities::ECopyOptions::CallPostEditChangeProperty )
					{
						FPropertyChangedEvent PropertyChangedEvent( Property );
						TargetComponent->PostEditChangeProperty( PropertyChangedEvent );
					}

					if( Options.Flags & EditorUtilities::ECopyOptions::PropagateChangesToArchetypeInstances )
					{
						for( int32 InstanceIndex = 0; InstanceIndex < ComponentArchetypeInstancesToChange.Num(); ++InstanceIndex )
						{
							UActorComponent* ComponentArchetypeInstance = ComponentArchetypeInstancesToChange[InstanceIndex];
							if( ComponentArchetypeInstance != nullptr )
							{
								if( !ModifiedObjects.Contains(ComponentArchetypeInstance) )
								{
									// Ensure that this instance will be included in any undo/redo operations, and record it into the transaction buffer.
									// Note: We don't do this for components that originate from script, because they will be re-instanced from the template after an undo, so there is no need to record them.
									if (!ComponentArchetypeInstance->IsCreatedByConstructionScript())
									{
										ComponentArchetypeInstance->SetFlags(RF_Transactional);
										ComponentArchetypeInstance->Modify();
										ModifiedObjects.Add(ComponentArchetypeInstance);
									}

									// We must also modify the owner, because we'll need script components to be reconstructed as part of an undo operation.
									AActor* Owner = ComponentArchetypeInstance->GetOwner();
									if( Owner != nullptr && !ModifiedObjects.Contains(Owner))
									{
										Owner->Modify();
										ModifiedObjects.Add(Owner);
									}
								}

								if (ComponentArchetypeInstance->IsRegistered())
								{
									ComponentArchetypeInstance->UnregisterComponent();
									ComponentInstancesToReregister.Add(ComponentArchetypeInstance);
								}

								EditorUtilities::CopySingleProperty( TargetComponent, ComponentArchetypeInstance, Property );
							}
						}
					}
				}

				++CopiedPropertyCount;

				if( bIsTransform )
				{
					bTransformChanged = true;
				}
			}
		}
	}

	for (UActorComponent* ModifiedComponentInstance : ComponentInstancesToReregister)
	{
		ModifiedComponentInstance->RegisterComponent();
	}

	return CopiedPropertyCount;
}
#endif


#if WITH_EDITOR
FBlueprintEditor*
FHoudiniEngineRuntimeUtils::GetBlueprintEditor(const UObject* InObject)
{
	if (!IsValid(InObject))
		return nullptr;

	UObject* Outer = InObject->GetOuter();
	if (!IsValid(Outer))
		return nullptr;

	UBlueprintGeneratedClass* OuterBPClass = Cast<UBlueprintGeneratedClass>(Outer->GetClass());

	if (!OuterBPClass)
		return nullptr;

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	return static_cast<FBlueprintEditor*>(AssetEditorSubsystem->FindEditorForAsset(OuterBPClass->ClassGeneratedBy, false));
}
#endif
