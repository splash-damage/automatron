// Copyright 2020 Splash Damage, Ltd. - All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include <Engine/Engine.h>
#include <Misc/AutomationTest.h>
#include <Tests/AutomationCommon.h>
#include <Templates/UnrealTypeTraits.h>

#include "TestSpec.h"


#define GENERATE_SPEC(TClass, PrettyName, TFlags) \
	GENERATE_SPEC_PRIVATE(TClass, PrettyName, TFlags, __FILE__, __LINE__)

#define GENERATE_SPEC_PRIVATE(TClass, PrettyName, TFlags, FileName, LineNumber) \
private: \
	void Setup() \
	{ \
		FTestSpec::Setup<TFlags>(TEXT(#TClass), TEXT(PrettyName), FileName, LineNumber); \
	} \
    static TSpecRegister<TClass>& __meta_register() \
	{ \
        return TSpecRegister<TClass>::Register; \
    } \
	friend TSpecRegister<TClass>; \
\
	virtual void Define() override


#define SPEC(TClass, TParent, PrettyName, TFlags) \
class TClass : public TParent \
{ \
	GENERATE_SPEC(TClass, PrettyName, TFlags); \
}; \
void TClass::Define()
