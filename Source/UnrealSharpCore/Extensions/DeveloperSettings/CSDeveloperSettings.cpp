#include "CSDeveloperSettings.h"

FName UCSDeveloperSettings::GetCategoryName() const
{
	FName Name = K2_GetCategoryName();
	if (Name.IsNone())
	{
		return Super::GetCategoryName();
	}

	return Name;
}

FName UCSDeveloperSettings::K2_GetCategoryName_Implementation() const
{
	return FName();
}
