// Copyright 2020 Splash Damage, Ltd. - All Rights Reserved.

#include "Base/TestSpecBase.h"

bool FTestSpecBase::FSingleExecuteLatentCommand::Update()
{
	if (bSkipIfErrored && Spec->HasAnyErrors())
	{
		return true;
	}

	Predicate();
	return true;
}


bool FTestSpecBase::FUntilDoneLatentCommand::Update()
{
	if (!bIsRunning)
	{
		if (bSkipIfErrored && Spec->HasAnyErrors())
		{
			return true;
		}

		Predicate(FDoneDelegate::CreateSP(this, &FUntilDoneLatentCommand::Done));
		bIsRunning = true;
		StartedRunning = FDateTime::UtcNow();
	}

	if (bDone)
	{
		Reset();
		return true;
	}
	else if (FDateTime::UtcNow() >= StartedRunning + Timeout)
	{
		Reset();
		Spec->AddError(TEXT("Latent command timed out."), 0);
		return true;
	}

	return false;
}


bool FTestSpecBase::FAsyncUntilDoneLatentCommand::Update()
{
	if (!Future.IsValid())
	{
		if (bSkipIfErrored && Spec->HasAnyErrors())
		{
			return true;
		}

		Future = Async(Execution, [this]() {
			Predicate(FDoneDelegate::CreateRaw(this, &FAsyncUntilDoneLatentCommand::Done));
		});

		StartedRunning = FDateTime::UtcNow();
	}

	if (bDone)
	{
		Reset();
		return true;
	}
	else if (FDateTime::UtcNow() >= StartedRunning + Timeout)
	{
		Reset();
		Spec->AddError(TEXT("Latent command timed out."), 0);
		return true;
	}
	return false;
}


bool FTestSpecBase::FAsyncLatentCommand::Update()
{
	if (!Future.IsValid())
	{
		if (bSkipIfErrored && Spec->HasAnyErrors())
		{
			return true;
		}

		Future = Async(Execution, [this]() {
			Predicate();
			bDone = true;
		});

		StartedRunning = FDateTime::UtcNow();
	}

	if (bDone)
	{
		Reset();
		return true;
	}
	else if (FDateTime::UtcNow() >= StartedRunning + Timeout)
	{
		Reset();
		Spec->AddError(TEXT("Latent command timed out."), 0);
		return true;
	}

	return false;
}


bool FTestSpecBase::RunTest(const FString& InParameters)
{
	EnsureDefinitions();

	if (!InParameters.IsEmpty())
	{
		const TSharedRef<FSpec>* SpecToRun = IdToSpecMap.Find(InParameters);
		if (SpecToRun != nullptr)
		{
			for (int32 Index = 0; Index < (*SpecToRun)->Commands.Num(); ++Index)
			{
				FAutomationTestFramework::GetInstance().EnqueueLatentCommand((*SpecToRun)->Commands[Index]);
			}
		}
	}
	else
	{
		TArray<TSharedRef<FSpec>> Specs;
		IdToSpecMap.GenerateValueArray(Specs);

		for (int32 SpecIndex = 0; SpecIndex < Specs.Num(); SpecIndex++)
		{
			for (int32 CommandIndex = 0; CommandIndex < Specs[SpecIndex]->Commands.Num(); ++CommandIndex)
			{
				FAutomationTestFramework::GetInstance().EnqueueLatentCommand(Specs[SpecIndex]->Commands[CommandIndex]);
			}
		}
	}

	TestsRemaining = GetNumTests();
	return true;
}

FString FTestSpecBase::GetTestSourceFileName(const FString& InTestName) const
{
	FString TestId = InTestName;
	if (TestId.StartsWith(TestName + TEXT(" ")))
	{
		TestId = InTestName.RightChop(TestName.Len() + 1);
	}

	const TSharedRef<FSpec>* Spec = IdToSpecMap.Find(TestId);
	if (Spec != nullptr)
	{
		return (*Spec)->Filename;
	}

	return GetTestSourceFileName();
}

int32 FTestSpecBase::GetTestSourceFileLine(const FString& InTestName) const
{
	FString TestId = InTestName;
	if (TestId.StartsWith(TestName + TEXT(" ")))
	{
		TestId = InTestName.RightChop(TestName.Len() + 1);
	}

	const TSharedRef<FSpec>* Spec = IdToSpecMap.Find(TestId);
	if (Spec != nullptr)
	{
		return (*Spec)->LineNumber;
	}

	return GetTestSourceFileLine();
}

void FTestSpecBase::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	EnsureDefinitions();

	TArray<TSharedRef<FSpec>> Specs;
	IdToSpecMap.GenerateValueArray(Specs);

	for (int32 Index = 0; Index < Specs.Num(); Index++)
	{
		OutTestCommands.Push(Specs[Index]->Id);
		OutBeautifiedNames.Push(Specs[Index]->Description);
	}
}

void FTestSpecBase::Describe(const FString& InDescription, TFunction<void()> DoWork)
{
	const TSharedRef<FSpecDefinitionScope> ParentScope = DefinitionScopeStack.Last();
	const TSharedRef<FSpecDefinitionScope> NewScope = MakeShared<FSpecDefinitionScope>();
	NewScope->Description = InDescription;
	ParentScope->Children.Push(NewScope);

	DefinitionScopeStack.Push(NewScope);
	PushDescription(InDescription);
	DoWork();
	PopDescription(InDescription);
	DefinitionScopeStack.Pop();

	if (NewScope->It.Num() == 0 && NewScope->Children.Num() == 0)
	{
		ParentScope->Children.Remove(NewScope);
	}
}

void FTestSpecBase::PreDefine()
{
	BeforeEach([this]()
	{
		CurrentContext = CurrentContext.NextContext();
	});
}
void FTestSpecBase::PostDefine()
{
	AfterEach([this]()
	{
		if (IsLastTest())
		{
			CurrentContext = {};
		}
	});
}

void FTestSpecBase::BakeDefinitions()
{
	TArray<TSharedRef<FSpecDefinitionScope>> Stack;
	Stack.Push(RootDefinitionScope.ToSharedRef());

	TArray<TSharedRef<IAutomationLatentCommand>> BeforeEach;
	TArray<TSharedRef<IAutomationLatentCommand>> AfterEach;

	while (Stack.Num() > 0)
	{
		const TSharedRef<FSpecDefinitionScope> Scope = Stack.Last();

		BeforeEach.Append(Scope->BeforeEach);
		// ScopeAfter each are added reversed
		AfterEach.Reserve(AfterEach.Num() + Scope->AfterEach.Num());
		for(int32 i = Scope->AfterEach.Num() - 1; i >= 0; --i)
		{
			AfterEach.Add(Scope->AfterEach[i]);
		}

		for (int32 ItIndex = 0; ItIndex < Scope->It.Num(); ItIndex++)
		{
			TSharedRef<FSpecIt> It = Scope->It[ItIndex];

			TSharedRef<FSpec> Spec = MakeShared<FSpec>();
			Spec->Id = It->Id;
			Spec->Description = It->Description;
			Spec->Filename = It->Filename;
			Spec->LineNumber = It->LineNumber;
			Spec->Commands.Append(BeforeEach);
			Spec->Commands.Add(It->Command);

			// Add after each reversed
			for (int32 i = AfterEach.Num() - 1; i >= 0; --i)
			{
				Spec->Commands.Add(AfterEach[i]);
			}

			check(!IdToSpecMap.Contains(Spec->Id));
			IdToSpecMap.Add(Spec->Id, Spec);
		}
		Scope->It.Empty();

		if (Scope->Children.Num() > 0)
		{
			Stack.Append(Scope->Children);
			Scope->Children.Empty();
		}
		else
		{
			while (Stack.Num() > 0 && Stack.Last()->Children.Num() == 0 && Stack.Last()->It.Num() == 0)
			{
				const TSharedRef<FSpecDefinitionScope> PoppedScope = Stack.Pop();

				if (PoppedScope->BeforeEach.Num() > 0)
				{
					BeforeEach.RemoveAt(BeforeEach.Num() - PoppedScope->BeforeEach.Num(), PoppedScope->BeforeEach.Num());
				}

				if (PoppedScope->AfterEach.Num() > 0)
				{
					AfterEach.RemoveAt(AfterEach.Num() - PoppedScope->AfterEach.Num(), PoppedScope->AfterEach.Num());
				}
			}
		}
	}

	RootDefinitionScope.Reset();
	DefinitionScopeStack.Reset();
	bHasBeenDefined = true;
}

void FTestSpecBase::Redefine()
{
	Description.Empty();
	IdToSpecMap.Empty();
	RootDefinitionScope.Reset();
	DefinitionScopeStack.Empty();
	bHasBeenDefined = false;
}

FString FTestSpecBase::GetDescription() const
{
	FString CompleteDescription;
	for (int32 Index = 0; Index < Description.Num(); ++Index)
	{
		if (Description[Index].IsEmpty())
		{
			continue;
		}

		if (CompleteDescription.IsEmpty())
		{
			CompleteDescription = Description[Index];
		}
		else if (FChar::IsWhitespace(CompleteDescription[CompleteDescription.Len() - 1]) || FChar::IsWhitespace(Description[Index][0]))
		{
			CompleteDescription = CompleteDescription + TEXT(".") + Description[Index];
		}
		else
		{
			CompleteDescription = FString::Printf(TEXT("%s.%s"), *CompleteDescription, *Description[Index]);
		}
	}

	return CompleteDescription;
}

FString FTestSpecBase::GetId() const
{
	if (Description.Last().EndsWith(TEXT("]")))
	{
		FString ItDescription = Description.Last();
		ItDescription.RemoveAt(ItDescription.Len() - 1);

		int32 StartingBraceIndex = INDEX_NONE;
		if (ItDescription.FindLastChar(TEXT('['), StartingBraceIndex) && StartingBraceIndex != ItDescription.Len() - 1)
		{
			FString CommandId = ItDescription.RightChop(StartingBraceIndex + 1);
			return CommandId;
		}
	}

	FString CompleteId;
	for (int32 Index = 0; Index < Description.Num(); ++Index)
	{
		if (Description[Index].IsEmpty())
		{
			continue;
		}

		if (CompleteId.IsEmpty())
		{
			CompleteId = Description[Index];
		}
		else if (FChar::IsWhitespace(CompleteId[CompleteId.Len() - 1]) || FChar::IsWhitespace(Description[Index][0]))
		{
			CompleteId = CompleteId + Description[Index];
		}
		else
		{
			CompleteId = FString::Printf(TEXT("%s %s"), *CompleteId, *Description[Index]);
		}
	}

	return CompleteId;
}
