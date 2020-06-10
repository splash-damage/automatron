// Copyright 2020 Splash Damage, Ltd. - All Rights Reserved.

#include <CoreMinimal.h>
#include <Misc/AutomationTest.h>


struct AUTOMATRON_API FTestContext
{
private:

	int32 Id = 0;

public:
	FTestContext(int32 Id = 0) : Id(Id) {}

	FTestContext NextContext() const { return { Id + 1 }; }
	int32 GetId() const { return Id; }

	friend uint32 GetTypeHash(const FTestContext& Item) { return Item.Id; }
	bool operator==(const FTestContext& Other) const { return Id == Other.Id; }
	operator bool() const { return Id > 0; }
};


class AUTOMATRON_API FTestSpecBase
	: public FAutomationTestBase
	, public TSharedFromThis<FTestSpecBase>
{
private:

	class FSingleExecuteLatentCommand : public IAutomationLatentCommand
	{
	private:

		const FTestSpecBase* const Spec;
		const TFunction<void()> Predicate;
		const bool bSkipIfErrored;

	public:
		FSingleExecuteLatentCommand(const FTestSpecBase* const InSpec, TFunction<void()> InPredicate, bool bInSkipIfErrored = false)
			: Spec(InSpec)
			, Predicate(MoveTemp(InPredicate))
			, bSkipIfErrored(bInSkipIfErrored)
		{ }
		virtual ~FSingleExecuteLatentCommand() {}

		virtual bool Update() override;
	};

	class FUntilDoneLatentCommand : public IAutomationLatentCommand
	{
	private:

		FTestSpecBase* const Spec;
		const TFunction<void(const FDoneDelegate&)> Predicate;
		const FTimespan Timeout;
		const bool bSkipIfErrored;

		bool bIsRunning;
		FDateTime StartedRunning;
		FThreadSafeBool bDone;

	public:

		FUntilDoneLatentCommand(FTestSpecBase* const InSpec, TFunction<void(const FDoneDelegate&)> InPredicate, const FTimespan& InTimeout, bool bInSkipIfErrored = false)
			: Spec(InSpec)
			, Predicate(MoveTemp(InPredicate))
			, Timeout(InTimeout)
			, bSkipIfErrored(bInSkipIfErrored)
			, bIsRunning(false)
			, bDone(false)
		{}
		virtual ~FUntilDoneLatentCommand() {}

		virtual bool Update() override;

	private:

		void Done()
		{
			bDone = true;
		}

		void Reset()
		{
			// Reset the done for the next potential run of this command
			bDone = false;
			bIsRunning = false;
		}
	};

	class FAsyncUntilDoneLatentCommand : public IAutomationLatentCommand
	{
	private:

		FTestSpecBase* const Spec;
		const EAsyncExecution Execution;
		const TFunction<void(const FDoneDelegate&)> Predicate;
		const FTimespan Timeout;
		const bool bSkipIfErrored;

		FThreadSafeBool bDone;
		FDateTime StartedRunning;
		TFuture<void> Future;

	public:

		FAsyncUntilDoneLatentCommand(FTestSpecBase* const InSpec, EAsyncExecution InExecution, TFunction<void(const FDoneDelegate&)> InPredicate, const FTimespan& InTimeout, bool bInSkipIfErrored = false)
			: Spec(InSpec)
			, Execution(InExecution)
			, Predicate(MoveTemp(InPredicate))
			, Timeout(InTimeout)
			, bSkipIfErrored(bInSkipIfErrored)
			, bDone(false)
		{}
		virtual ~FAsyncUntilDoneLatentCommand() {}

		virtual bool Update() override;

	private:

		void Done()
		{
			bDone = true;
		}

		void Reset()
		{
			// Reset the done for the next potential run of this command
			bDone = false;
			Future = TFuture<void>();
		}
	};

	class FAsyncLatentCommand : public IAutomationLatentCommand
	{
	private:

		FTestSpecBase* const Spec;
		const EAsyncExecution Execution;
		const TFunction<void()> Predicate;
		const FTimespan Timeout;
		const bool bSkipIfErrored;

		FThreadSafeBool bDone;
		FDateTime StartedRunning;
		TFuture<void> Future;

	public:

		FAsyncLatentCommand(FTestSpecBase* const InSpec, EAsyncExecution InExecution, TFunction<void()> InPredicate, const FTimespan& InTimeout, bool bInSkipIfErrored = false)
			: Spec(InSpec)
			, Execution(InExecution)
			, Predicate(MoveTemp(InPredicate))
			, Timeout(InTimeout)
			, bSkipIfErrored(bInSkipIfErrored)
			, bDone(false)
		{}
		virtual ~FAsyncLatentCommand() {}

		virtual bool Update() override;

	private:

		void Done()
		{
			bDone = true;
		}

		void Reset()
		{
			// Reset the done for the next potential run of this command
			bDone = false;
			Future = TFuture<void>();
		}
	};

	struct FSpecIt
	{
		FString Description;
		FString Id;
		FString Filename;
		int32 LineNumber;
		TSharedRef<IAutomationLatentCommand> Command;

		FSpecIt(FString InDescription, FString InId, FString InFilename, int32 InLineNumber, TSharedRef<IAutomationLatentCommand> InCommand)
			: Description(MoveTemp(InDescription))
			, Id(MoveTemp(InId))
			, Filename(InFilename)
			, LineNumber(MoveTemp(InLineNumber))
			, Command(MoveTemp(InCommand))
		{ }
	};

	struct FSpecDefinitionScope
	{
		FString Description;

		TArray<TSharedRef<IAutomationLatentCommand>> BeforeEach;
		TArray<TSharedRef<FSpecIt>> It;
		TArray<TSharedRef<IAutomationLatentCommand>> AfterEach;

		TArray<TSharedRef<FSpecDefinitionScope>> Children;
	};

	struct FSpec
	{
		FString Id;
		FString Description;
		FString Filename;
		int32 LineNumber;
		TArray<TSharedRef<IAutomationLatentCommand>> Commands;
	};


protected:

	/* The timespan for how long a block should be allowed to execute before giving up and failing the test */
	FTimespan DefaultTimeout = FTimespan::FromSeconds(30);

	/* Whether or not BeforeEach and It blocks should skip execution if the test has already failed */
	bool bEnableSkipIfError = true;

private:

	TArray<FString> Description;

	TMap<FString, TSharedRef<FSpec>> IdToSpecMap;

	TSharedPtr<FSpecDefinitionScope> RootDefinitionScope;

	TArray<TSharedRef<FSpecDefinitionScope>> DefinitionScopeStack;

	bool bHasBeenDefined = false;

	int32 TestsRemaining = 0;

	// The context of the active test
	FTestContext CurrentContext;


public:

	FTestSpecBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
		, RootDefinitionScope(MakeShared<FSpecDefinitionScope>())
	{
		DefinitionScopeStack.Push(RootDefinitionScope.ToSharedRef());
	}

	virtual ~FTestSpecBase() {}

	virtual bool RunTest(const FString& InParameters) override;

	virtual bool IsStressTest() const { return false; }
	virtual uint32 GetRequiredDeviceNum() const override { return 1; }

	virtual FString GetTestSourceFileName(const FString& InTestName) const override;

	virtual int32 GetTestSourceFileLine(const FString& InTestName) const override;

	virtual void GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const override;


	// BEGIN Disabled Scopes
	void xDescribe(const FString& InDescription, TFunction<void()> DoWork) {}

	void xIt(const FString& InDescription, TFunction<void()> DoWork) {}
	void xIt(const FString& InDescription, EAsyncExecution Execution, TFunction<void()> DoWork) {}
	void xIt(const FString& InDescription, EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void()> DoWork) {}

	void xLatentIt(const FString& InDescription, TFunction<void(const FDoneDelegate&)> DoWork) {}
	void xLatentIt(const FString& InDescription, const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork) {}
	void xLatentIt(const FString& InDescription, EAsyncExecution Execution, TFunction<void(const FDoneDelegate&)> DoWork) {}
	void xLatentIt(const FString& InDescription, EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork) {}

	void xBeforeEach(TFunction<void()> DoWork) {}
	void xBeforeEach(EAsyncExecution Execution, TFunction<void()> DoWork) {}
	void xBeforeEach(EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void()> DoWork) {}

	void xLatentBeforeEach(TFunction<void(const FDoneDelegate&)> DoWork) {}
	void xLatentBeforeEach(const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork) {}
	void xLatentBeforeEach(EAsyncExecution Execution, TFunction<void(const FDoneDelegate&)> DoWork) {}
	void xLatentBeforeEach(EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork) {}

	void xAfterEach(TFunction<void()> DoWork) {}
	void xAfterEach(EAsyncExecution Execution, TFunction<void()> DoWork) {}
	void xAfterEach(EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void()> DoWork) {}

	void xLatentAfterEach(TFunction<void(const FDoneDelegate&)> DoWork) {}
	void xLatentAfterEach(const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork) {}
	void xLatentAfterEach(EAsyncExecution Execution, TFunction<void(const FDoneDelegate&)> DoWork) {}
	void xLatentAfterEach(EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork) {}
	// END Disabled Scopes


	// BEGIN Enabled Scopes
	void Describe(const FString& InDescription, TFunction<void()> DoWork);

	void It(const FString& InDescription, TFunction<void()> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		const TArray<FProgramCounterSymbolInfo> Stack = FPlatformStackWalk::GetStack(1, 1);

		PushDescription(InDescription);
		CurrentScope->It.Push(MakeShared<FSpecIt>(GetDescription(), GetId(), Stack[0].Filename, Stack[0].LineNumber, MakeShared<FSingleExecuteLatentCommand>(this, DoWork, bEnableSkipIfError)));
		PopDescription(InDescription);
	}

	void It(const FString& InDescription, EAsyncExecution Execution, TFunction<void()> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		const TArray<FProgramCounterSymbolInfo> Stack = FPlatformStackWalk::GetStack(1, 1);

		PushDescription(InDescription);
		CurrentScope->It.Push(MakeShared<FSpecIt>(GetDescription(), GetId(), Stack[0].Filename, Stack[0].LineNumber, MakeShared<FAsyncLatentCommand>(this, Execution, DoWork, DefaultTimeout, bEnableSkipIfError)));
		PopDescription(InDescription);
	}

	void It(const FString& InDescription, EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void()> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		const TArray<FProgramCounterSymbolInfo> Stack = FPlatformStackWalk::GetStack(1, 1);

		PushDescription(InDescription);
		CurrentScope->It.Push(MakeShared<FSpecIt>(GetDescription(), GetId(), Stack[0].Filename, Stack[0].LineNumber, MakeShared<FAsyncLatentCommand>(this, Execution, DoWork, Timeout, bEnableSkipIfError)));
		PopDescription(InDescription);
	}

	void LatentIt(const FString& InDescription, TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		const TArray<FProgramCounterSymbolInfo> Stack = FPlatformStackWalk::GetStack(1, 1);

		PushDescription(InDescription);
		CurrentScope->It.Push(MakeShared<FSpecIt>(GetDescription(), GetId(), Stack[0].Filename, Stack[0].LineNumber, MakeShared<FUntilDoneLatentCommand>(this, DoWork, DefaultTimeout, bEnableSkipIfError)));
		PopDescription(InDescription);
	}

	void LatentIt(const FString& InDescription, const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		const TArray<FProgramCounterSymbolInfo> Stack = FPlatformStackWalk::GetStack(1, 1);

		PushDescription(InDescription);
		CurrentScope->It.Push(MakeShared<FSpecIt>(GetDescription(), GetId(), Stack[0].Filename, Stack[0].LineNumber, MakeShared<FUntilDoneLatentCommand>(this, DoWork, Timeout, bEnableSkipIfError)));
		PopDescription(InDescription);
	}

	void LatentIt(const FString& InDescription, EAsyncExecution Execution, TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		const TArray<FProgramCounterSymbolInfo> Stack = FPlatformStackWalk::GetStack(1, 1);

		PushDescription(InDescription);
		CurrentScope->It.Push(MakeShared<FSpecIt>(GetDescription(), GetId(), Stack[0].Filename, Stack[0].LineNumber, MakeShared<FAsyncUntilDoneLatentCommand>(this, Execution, DoWork, DefaultTimeout, bEnableSkipIfError)));
		PopDescription(InDescription);
	}

	void LatentIt(const FString& InDescription, EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		const TArray<FProgramCounterSymbolInfo> Stack = FPlatformStackWalk::GetStack(1, 1);

		PushDescription(InDescription);
		CurrentScope->It.Push(MakeShared<FSpecIt>(GetDescription(), GetId(), Stack[0].Filename, Stack[0].LineNumber, MakeShared<FAsyncUntilDoneLatentCommand>(this, Execution, DoWork, Timeout, bEnableSkipIfError)));
		PopDescription(InDescription);
	}

	void BeforeEach(TFunction<void()> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->BeforeEach.Push(MakeShareable(new FSingleExecuteLatentCommand(this, DoWork, bEnableSkipIfError)));
	}

	void BeforeEach(EAsyncExecution Execution, TFunction<void()> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->BeforeEach.Push(MakeShared<FAsyncLatentCommand>(this, Execution, DoWork, DefaultTimeout, bEnableSkipIfError));
	}

	void BeforeEach(EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void()> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->BeforeEach.Push(MakeShared<FAsyncLatentCommand>(this, Execution, DoWork, Timeout, bEnableSkipIfError));
	}

	void LatentBeforeEach(TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->BeforeEach.Push(MakeShared<FUntilDoneLatentCommand>(this, DoWork, DefaultTimeout, bEnableSkipIfError));
	}

	void LatentBeforeEach(const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->BeforeEach.Push(MakeShared<FUntilDoneLatentCommand>(this, DoWork, Timeout, bEnableSkipIfError));
	}

	void LatentBeforeEach(EAsyncExecution Execution, TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->BeforeEach.Push(MakeShared<FAsyncUntilDoneLatentCommand>(this, Execution, DoWork, DefaultTimeout, bEnableSkipIfError));
	}

	void LatentBeforeEach(EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->BeforeEach.Push(MakeShared<FAsyncUntilDoneLatentCommand>(this, Execution, DoWork, Timeout, bEnableSkipIfError));
	}

	void AfterEach(TFunction<void()> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->AfterEach.Push(MakeShareable(new FSingleExecuteLatentCommand(this, DoWork)));
	}

	void AfterEach(EAsyncExecution Execution, TFunction<void()> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->AfterEach.Push(MakeShared<FAsyncLatentCommand>(this, Execution, DoWork, DefaultTimeout));
	}

	void AfterEach(EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void()> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->AfterEach.Push(MakeShared<FAsyncLatentCommand>(this, Execution, DoWork, Timeout));
	}

	void LatentAfterEach(TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->AfterEach.Push(MakeShared<FUntilDoneLatentCommand>(this, DoWork, DefaultTimeout));
	}

	void LatentAfterEach(const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->AfterEach.Push(MakeShared<FUntilDoneLatentCommand>(this, DoWork, Timeout));
	}

	void LatentAfterEach(EAsyncExecution Execution, TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->AfterEach.Push(MakeShared<FAsyncUntilDoneLatentCommand>(this, Execution, DoWork, DefaultTimeout));
	}

	void LatentAfterEach(EAsyncExecution Execution, const FTimespan& Timeout, TFunction<void(const FDoneDelegate&)> DoWork)
	{
		const TSharedRef<FSpecDefinitionScope> CurrentScope = DefinitionScopeStack.Last();
		CurrentScope->AfterEach.Push(MakeShared<FAsyncUntilDoneLatentCommand>(this, Execution, DoWork, Timeout));
	}

	int32 GetNumTests() const { return IdToSpecMap.Num(); }
	int32 GetTestsRemaining() const { return GetNumTests() - CurrentContext.GetId(); }
	FTestContext GetCurrentContext() const { return CurrentContext; }
	bool IsFirstTest() const { return CurrentContext.GetId() == 1; }
	bool IsLastTest() const { return CurrentContext.GetId() == GetNumTests(); }

protected:

	void EnsureDefinitions() const;

	virtual void RunDefine()
	{
		PreDefine();
		Define();
		PostDefine();
	}
	virtual void PreDefine();
	virtual void Define() = 0;
	virtual void PostDefine();

	void BakeDefinitions();

	void Redefine();

private:

	void PushDescription(const FString& InDescription)
	{
		Description.Add(InDescription);
	}

	void PopDescription(const FString& InDescription)
	{
		Description.RemoveAt(Description.Num() - 1);
	}

	FString GetDescription() const;

	FString GetId() const;
};

inline void FTestSpecBase::EnsureDefinitions() const
{
	if (!bHasBeenDefined)
	{
		const_cast<FTestSpecBase*>(this)->RunDefine();
		const_cast<FTestSpecBase*>(this)->BakeDefinitions();
	}
}
