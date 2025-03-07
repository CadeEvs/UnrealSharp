﻿// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FunctionsExporter.h"
#include "TPersistentObjectPtrExporter.generated.h"

/**
 * 
 */
UCLASS(meta = (NotGeneratorValid))
class UNREALSHARPCORE_API UTPersistentObjectPtrExporter : public UFunctionsExporter
{
	GENERATED_BODY()

public:

	// UFunctionsExporter interface implementation
	virtual void ExportFunctions(FRegisterExportedFunction RegisterExportedFunction) override;
	// End

private:
	
	static void FromObject(TPersistentObjectPtr<FSoftObjectPath>* Path, UObject* Object);
	static void FromSoftObjectPath(TPersistentObjectPtr<FSoftObjectPath>* Path, const FSoftObjectPath* SoftObjectPath);
	static void* Get(TPersistentObjectPtr<FSoftObjectPath>* Path);
	static void* GetNativePointer(TPersistentObjectPtr<FSoftObjectPath>* Path);
	static void* GetUniqueID(TPersistentObjectPtr<FSoftObjectPath>* Path);
	
};
