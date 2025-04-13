#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CSDeveloperSettings.generated.h"

UCLASS(Abstract)
class UCSDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	//~UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	//~End of UDeveloperSettings interface
	
#if WITH_EDITOR
	// UDeveloperSettings interface
	virtual bool SupportsAutoRegistration() const override { return false; }
	// End of UDeveloperSettings interface
#endif

protected:
	UFUNCTION(BlueprintNativeEvent, meta = (ScriptName = "GetCategoryName"), Category = "Managed Developer Settings")
	FName K2_GetCategoryName() const;
};
