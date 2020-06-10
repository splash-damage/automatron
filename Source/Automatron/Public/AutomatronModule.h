// Copyright 2020 Splash Damage, Ltd. - All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include <Modules/ModuleManager.h>
#include "TestSpec.h"


class FAutomatronModule : public IModuleInterface
{
	static TArray<TSharedRef<FTestSpec>> SpecInstances;

public:

	/** Begin IModuleInterface implementation */
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
	/** End IModuleInterface implementation */
};
