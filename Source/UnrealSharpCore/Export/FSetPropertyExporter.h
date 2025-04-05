﻿// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CSBindsManager.h"
#include "UObject/Object.h"
#include "FSetPropertyExporter.generated.h"

UCLASS()
class UNREALSHARPCORE_API UFSetPropertyExporter : public UObject
{
	GENERATED_BODY()
public:
	UNREALSHARP_FUNCTION()
	static void* GetElement(FSetProperty* Property);
};
