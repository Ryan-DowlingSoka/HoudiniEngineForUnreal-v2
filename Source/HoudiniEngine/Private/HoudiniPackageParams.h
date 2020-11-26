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

#pragma once

#include "UObject/ObjectMacros.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

#include "HoudiniStringResolver.h"

#include "HoudiniPackageParams.generated.h"

class UStaticMesh;

UENUM()
enum class EPackageMode : int8
{
	CookToLevel,
	CookToTemp,
	Bake
};

UENUM()
enum class EPackageReplaceMode : int8
{
	CreateNewAssets,
	ReplaceExistingAssets
};

USTRUCT()
struct HOUDINIENGINE_API FHoudiniPackageParams
{
public:
	GENERATED_BODY();
	
	//
	FHoudiniPackageParams();
	//
	~FHoudiniPackageParams();

	// Helper functions returning the default behavior expected when cooking mesh
	static EPackageMode GetDefaultStaticMeshesCookMode() { return EPackageMode::CookToTemp; };
	// Helper functions returning the default behavior expected when cooking materials or textures
	static EPackageMode GetDefaultMaterialAndTextureCookMode() { return EPackageMode::CookToTemp; };
	// Helper functions returning the default behavior for replacing existing package
	static EPackageReplaceMode GetDefaultReplaceMode() { return EPackageReplaceMode::ReplaceExistingAssets; };

	// Returns the name for the package depending on the mode
	FString GetPackageName() const;
	// Returns the package's path depending on the mode
	FString GetPackagePath() const;
	// Returns the object flags corresponding to the current package mode
	EObjectFlags GetObjectFlags() const;

	// Get the bake counter for InAsset's package metadata. Return true if the counter was found, false otherwise.
	static bool GetBakeCounterFromBakedAsset(const UObject* InAsset, int32& OutBakeCounter);

	// Get the GUID for a temp asset.
	static bool GetGUIDFromTempAsset(const UObject* InAsset, FString& OutGUID);

	// Get package name without bake counter
	static FString GetPackageNameExcludingBakeCounter(const UObject* InAsset);

	// Get package name without temp GUID suffix
	static FString GetPackageNameExcludingGUID(const UObject* InAsset);

	// Returns true if these package params generate the same package path and name as InAsset's package path name (with
	// any potential bake counters stripped during comparison)
	bool MatchesPackagePathNameExcludingBakeCounter(const UObject* InAsset) const;

	// Helper function to create a Package for a given object
	UPackage* CreatePackageForObject(FString& OutPackageName, int32 InBakeCounterStart=0) const;

	// Helper function to create an object and its package
	template<typename T> T* CreateObjectAndPackage();


	// The current cook/baking mode
	UPROPERTY()
	EPackageMode PackageMode;
	// How to handle existing assets? replace or rename?
	UPROPERTY()
	EPackageReplaceMode ReplaceMode;

	// When cooking in bake mode - folder to create assets in
	UPROPERTY()
	FString BakeFolder;
	// When cooking in temp mode - folder to create assets in
	UPROPERTY()
	FString TempCookFolder;
	
	// Package to save to
	UPROPERTY()
	UObject* OuterPackage;

	// Name of the package we want to create
	// If null, we'll generate one from:
	// (without PDG) ASSET_OBJ_GEO_PART_SPLIT,
	// (with PDG) ASSET_TOPNET_TOPNODE_WORKITEMINDEX_PART_SPLIT
	UPROPERTY()
	FString ObjectName;

	// Name of the HDA
	UPROPERTY()
	FString HoudiniAssetName;

	// Name of actor that is managing an instance of the HDA
	UPROPERTY()
	FString HoudiniAssetActorName;

	//
	UPROPERTY()
	int32	ObjectId;
	//
	UPROPERTY()
	int32	GeoId;
	//
	UPROPERTY()
	int32	PartId;
	//
	UPROPERTY()
	FString SplitStr;

	// GUID used for the owner
	UPROPERTY()
	FGuid ComponentGUID;

	// For PDG temporary outputs: the TOP network name
	UPROPERTY()
	FString PDGTOPNetworkName;
	// For PDG temporary outputs: the TOP node name
	UPROPERTY()
	FString PDGTOPNodeName;
	// For PDG temporary outputs: the work item index of the TOP node
	UPROPERTY()
	int32 PDGWorkItemIndex;

	// If FindPackage returns null, if this flag is true then a LoadPackage will also be attempted
	// This is for use cases, such as commandlets, that might unload packages once done with them, but that must
	// reliably be able to determine if a package exists later
	UPROPERTY()
	bool bAttemptToLoadMissingPackages;

	////TODO: We don't have access to Houdini attributes in HoudiniEngine/HoudiniEnginePrivatePCH. 
	//FString GetTempFolderArgument(ERuntimePackageMode PackageMode) const;
	//FString GetBakeFolderArgument(ERuntimePackageMode PackageMode) const;

	//// Return the output path as either the temp or bake path, depending on the package mode.
	//FString GetOutputFolderForPackageMode(ERuntimePackageMode PackageMode) const;

	/*
	 * Build a "standard" set of string formatting arguments that
	 * is typically used across HoudiniEngine path naming outputs.
	 * Note that each output type may contain additional named arguments
	 * that are not listed here.
	 * {out} - The output directory (varies depending on the package mode).
	 * {pkg} - The path to the destination package (varies depending on the package mode).
	 * {world} - Path the directory that contains the world.
	 * {hda_name} - Name of the HDA
	 * {guid} - guid of the HDA component
	 * @param PackageParams The output path for the current build mode (Temp / Bake).
	 * @param HACWorld The world in which the HDA component lives (typically Editor world).
	 * @param OutArgs The generated named arguments to be used for string formatting. 
	*/

	// Populate a map of named arguments from this FHoudiniPackageParams.
	template<typename ValueT>
	void UpdateTokensFromParams(
		const UWorld* WorldContext,
		TMap<FString, ValueT>& OutTokens) const
	{
		UpdateOutputPathTokens(PackageMode, OutTokens);

		OutTokens.Add("world", ValueT( FPaths::GetPath(WorldContext->GetPathName()) ));
		OutTokens.Add("object_name", ValueT( ObjectName ));
		OutTokens.Add("object_id", ValueT( FString::FromInt(ObjectId) ));
		OutTokens.Add("geo_id", ValueT( FString::FromInt(GeoId) ));
		OutTokens.Add("part_id", ValueT( FString::FromInt(PartId) ));
		OutTokens.Add("split_str", ValueT( SplitStr));
		OutTokens.Add("hda_name", ValueT( HoudiniAssetName ));
		OutTokens.Add("hda_actor_name", ValueT( HoudiniAssetActorName ));
		OutTokens.Add("pdg_topnet_name", ValueT( PDGTOPNetworkName ));
		OutTokens.Add("pdg_topnode_name", ValueT( PDGTOPNodeName ));
		OutTokens.Add("pdg_workitem_index", ValueT( FString::FromInt(PDGWorkItemIndex) ));
		OutTokens.Add("guid", ValueT( ComponentGUID.ToString() ));
	}

	template<typename ValueT>
	void UpdateOutputPathTokens(EPackageMode InPackageMode, TMap<FString, ValueT>& OutTokens) const
	{
		const FString PackagePath = GetPackagePath();

		OutTokens.Add("temp", ValueT(FPaths::GetPath(TempCookFolder)));
		OutTokens.Add("bake", ValueT(FPaths::GetPath(BakeFolder)));

		// `out_basepath` is useful if users want to organize their cook/bake assets
		// different to the convention defined by GetPackagePath(). This would typically
		// be combined with `unreal_level_path` during level path resolves.
		switch (InPackageMode)
		{
		case EPackageMode::CookToTemp:
		case EPackageMode::CookToLevel:
			OutTokens.Add("out_basepath", ValueT(FPaths::GetPath(TempCookFolder)));
			break;
		case EPackageMode::Bake:
			OutTokens.Add("out_basepath", ValueT(FPaths::GetPath(BakeFolder)));
			break;
		}

		OutTokens.Add("out", ValueT( FPaths::GetPath(PackagePath) ));
	}

};


