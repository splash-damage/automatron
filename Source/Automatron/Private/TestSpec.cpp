// Copyright 2020 Splash Damage, Ltd. - All Rights Reserved.

#include "TestSpec.h"
#include <EngineUtils.h>

#if WITH_EDITOR
#include <Tests/AutomationEditorPromotionCommon.h>
#include <Editor.h>
#endif


void FTestSpec::PreDefine()
{
	FTestSpecBase::PreDefine();

	if (!bUseWorld)
	{
		return;
	}

	LatentBeforeEach(EAsyncExecution::ThreadPool, [this](const auto & Done)
	{
		PrepareTestWorld(FSpecBaseOnWorldReady::CreateLambda([this, &Done](UWorld * InWorld)
		{
			World = InWorld;
			Done.Execute();
		}));
	});
}

void FTestSpec::PostDefine()
{
	AfterEach([this]()
	{
		// If this spec initialized a PIE world, tear it down
		if (!bReuseWorldForAllTests || IsLastTest())
		{
			ReleaseTestWorld();
		}
	});

	FTestSpecBase::PostDefine();
}

void FTestSpec::PrepareTestWorld(FSpecBaseOnWorldReady OnWorldReady)
{
	checkf(!IsInGameThread(), TEXT("PrepareTestWorld can only be done asynchronously. (LatentBeforeEach with ThreadPool or TaskGraph)"));

	UWorld* SelectedWorld = FindGameWorld();

#if WITH_EDITOR
	// If there was no PIE world, start it and try again
	if (bCanUsePIEWorld && !SelectedWorld && GIsEditor)
	{
		bool bPIEWorldIsReady = false;
		FDelegateHandle PIEStartedHandle;
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			PIEStartedHandle = FEditorDelegates::PostPIEStarted.AddLambda([&](const bool bIsSimulating)
			{
				// Notify the thread about the world being ready
				bPIEWorldIsReady = true;
				FEditorDelegates::PostPIEStarted.Remove(PIEStartedHandle);
			});
			FEditorPromotionTestUtilities::StartPIE(false);
		});

		// Wait while PIE initializes
		while (!bPIEWorldIsReady)
		{
			FPlatformProcess::Sleep(0.005f);
		}

		SelectedWorld = FindGameWorld();
		bInitializedPIE = SelectedWorld != nullptr;
		bInitializedWorld = bInitializedPIE;
	}
#endif

	if (!SelectedWorld)
	{
		SelectedWorld = GWorld;
#if WITH_EDITOR
		if (GIsEditor)
		{
			UE_LOG(LogTemp, Warning, TEXT("Test using GWorld. Not correct for PIE"));
		}
#endif
	}

	OnWorldReady.ExecuteIfBound(SelectedWorld);
}

void FTestSpec::ReleaseTestWorld()
{
	if (!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			ReleaseTestWorld();
		});
		return;
	}

#if WITH_EDITOR
	if (bInitializedPIE)
	{
		FEditorPromotionTestUtilities::EndPIE();
		bInitializedPIE = false;
		return;
	}
#endif

	if (!bInitializedWorld)
	{
		return;
	}

	// If world is not PIE, we take care of its teardown
	UWorld* WorldPtr = World.Get();
	if(WorldPtr && !WorldPtr->IsPlayInEditor())
	{
		WorldPtr->BeginTearingDown();

		// Cancel any pending connection to a server
		GEngine->CancelPending(WorldPtr);

		// Shut down any existing game connections
		GEngine->ShutdownWorldNetDriver(WorldPtr);

		for (FActorIterator ActorIt(WorldPtr); ActorIt; ++ActorIt)
		{
			ActorIt->RouteEndPlay(EEndPlayReason::Quit);
		}

		if (WorldPtr->GetGameInstance() != nullptr)
		{
			WorldPtr->GetGameInstance()->Shutdown();
		}

		World->FlushLevelStreaming(EFlushLevelStreamingType::Visibility);
		World->CleanupWorld();
	}
}

UWorld* FTestSpec::FindGameWorld()
{
	const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
	for (const FWorldContext& Context : WorldContexts)
	{
		if (Context.World() != nullptr)
		{
			if (Context.WorldType == EWorldType::PIE /*&& Context.PIEInstance == 0*/)
			{
				return Context.World();
			}

			if (Context.WorldType == EWorldType::Game)
			{
				return Context.World();
			}
		}
	}
	return nullptr;
}
