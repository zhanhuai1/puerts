/*
 * Tencent is pleased to support the open source community by making Puerts available.
 * Copyright (C) 2020 THL A29 Limited, a Tencent company.  All rights reserved.
 * Puerts is licensed under the BSD 3-Clause License, except for the third-party components listed in the file 'LICENSE' which may
 * be subject to their corresponding license terms. This file is subject to the terms and conditions defined in file 'LICENSE',
 * which is part of this source code package.
 */

#include "PuertsEditorModule.h"
#include "JsEnv.h"
#include "Editor.h"
#include "Misc/FileHelper.h"
#include "PuertsModule.h"
#include "TypeScriptCompilerContext.h"
#include "TypeScriptBlueprint.h"
#include "SourceFileWatcher.h"
#include "JSLogger.h"
#include "JSModuleLoader.h"
#include "Binding.hpp"
#include "EditorJSHelper.h"
#include "IDeclarationGenerator.h"
#include "JsInterface.h"
#include "LevelEditor.h"
#include "UEDataBinding.hpp"
#include "Object.hpp"
#include "PuertsSetting.h"
#include "UnrealEdGlobals.h"
#include "Blueprint/UserWidget.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "Editor/UnrealEdEngine.h"
#include "Kismet/KismetSystemLibrary.h"
#include "PString.h"

class UEditorJSHelper;

UE_DISABLE_OPTIMIZATION

class FPuertsEditorModule : public IPuertsEditorModule, public FUObjectArray::FUObjectCreateListener, public FUObjectArray::FUObjectDeleteListener
{
    /** IModuleInterface implementation */
    void StartupModule() override;
    void ShutdownModule() override;
    
    DECLARE_FUNCTION(EditorWidgetConstruct);
    DECLARE_FUNCTION(EditorWidgetDestruct);
    
    void SetCmdImpl(std::function<void(const FString&, const FString&)> Func) override
    {
        CmdImpl = Func;
    }

	void SetCompileJSFunc(std::function<int(void)> Func) override
    {
    	CompileJSFunc = Func;
    }

	
public:
    virtual void NotifyUObjectCreated(const class UObjectBase* InObject, int32 Index) override;
    virtual void NotifyUObjectDeleted(const class UObjectBase* InObject, int32 Index) override;

#if ENGINE_MINOR_VERSION > 22 || ENGINE_MAJOR_VERSION > 4
    virtual void OnUObjectArrayShutdown() override;
#endif
    
    static void SetCmdCallback(std::function<void(const FString&, const FString&)> Func)
    {
        Get().SetCmdImpl(Func);
    }

	static void ExportCompileJSFunc(std::function<int(void)> Func)
    {
    	Get().SetCompileJSFunc(Func);
    }

	virtual int GenAndCompileJS() override;
	
	virtual FString EditorJsEnvCurrentStack() override;

	virtual TStrongObjectPtr<UEditorJSHelper> GetEditorJSHelper() override { return EditorHelper; }
private:
    //
    void OnPreBeginPIE(bool bIsSimulating);

    void OnPreEndPIE(bool bIsSimulating);

    void EndPIE(bool bIsSimulating);

    void OnPostEngineInit();

    void MakeEditorJsEnv();

    void BindToEditorEnv(UObject* Object);

    void UnbindToEditorEnv(UObject* Object);
    
    void ChangeTabWorld(UWorld* World, EMapChangeType MapChangeType);

    void OnBlueprintPreCompile(UBlueprint* Blueprint);
    
    TSharedPtr<PUERTS_NAMESPACE::FJsEnv> JsEnv;

    TSharedPtr<PUERTS_NAMESPACE::FSourceFileWatcher> SourceFileWatcher;

    bool Enabled = false;

    std::function<void(const FString&, const FString&)> CmdImpl;

	std::function<int(void)> CompileJSFunc;

    TUniquePtr<FAutoConsoleCommand> ConsoleCommand;

    TStrongObjectPtr<UEditorJSHelper> EditorHelper;

    TSharedPtr<PUERTS_NAMESPACE::FSourceFileWatcher> EditorSourceFileWatcher;
    
    TSharedPtr<PUERTS_NAMESPACE::FJsEnv> EditorEnv;

	TSet<UObjectBase*> CachedBindToEditorObjects;
};

UsingCppType(FPuertsEditorModule);

struct AutoRegisterForPEM
{
    AutoRegisterForPEM()
    {
        PUERTS_NAMESPACE::DefineClass<FPuertsEditorModule>()
            .Function("SetCmdCallback", MakeFunction(&FPuertsEditorModule::SetCmdCallback))
            .Function("ExportCompileJSFunc", MakeFunction(&FPuertsEditorModule::ExportCompileJSFunc))
            .Register();
    }
};

AutoRegisterForPEM _AutoRegisterForPEM__;

IMPLEMENT_MODULE(FPuertsEditorModule, PuertsEditor)

void FPuertsEditorModule::StartupModule()
{
	FString tmpPath = FString();
	bool bIsExportJs = false;

	//判断Cook环境似乎不生效,用环境变量区分
	FString ExportJsEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("ExportJsEnv"));
	if (!ExportJsEnv.IsEmpty())
	{
		bIsExportJs = true;
	}
	
// #ifdef MY_BUILD_SCRIPT_RUNNING
// 	bIsBuildBinary = true;
// #endif
// #if ENABLE_COOK_STATS
// 	bIsBuildBinary = true;
// #endif
// 	if(GUnrealEd)
// 	{
// 		if(GUnrealEd->CookServer)
// 		{
// 			if(GUnrealEd->CookServer->IsCookByTheBookRunning() || GUnrealEd->CookServer->IsCookingInEditor())
// 			{
// 				bIsBuildBinary = true;
// 			}
// 		}
// 	}

	if (!bIsExportJs)
	{
		if (IsRunningCommandlet())
		{
			FString CmdLine(FCommandLine::Get());
			if (CmdLine.Contains(TEXT("run=JyExportJs")))
			{
				bIsExportJs = true;
			}
		}
	}

    Enabled = IPuertsModule::Get().IsWatchEnabled() && (!IsRunningCommandlet() || bIsExportJs); // && !IsRunningCommandlet()
	UE_LOG(LogTemp, Display, TEXT("FPuertsEditorModule StartupModule ..%s!"),(Enabled?TEXT("Enable"):TEXT("Disable")));
	
    FEditorDelegates::PreBeginPIE.AddRaw(this, &FPuertsEditorModule::OnPreBeginPIE);
	FEditorDelegates::PrePIEEnded.AddRaw( this, &FPuertsEditorModule::OnPreEndPIE);
    FEditorDelegates::EndPIE.AddRaw(this, &FPuertsEditorModule::EndPIE);

    ConsoleCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Puerts"), TEXT("Puerts action"),
        FConsoleCommandWithArgsDelegate::CreateLambda(
            [this](const TArray<FString>& Args)
            {
                if (Args.Num() > 0)
                {
                    if (Args[0].ToLower() == TEXT("editorgc") && EditorEnv.IsValid())
                    {
                        EditorEnv->RequestFullGarbageCollectionForTesting();
                        UE_LOG(Puerts, Warning, TEXT("Puerts Editor GC!"))
                        if (Args.Num() <= 1 || Args[1].ToLower() != TEXT("ts"))
                        {
                            UWorld* World = GEditor ? GEditor->GetEditorWorldContext(false).World() : nullptr;
                            UKismetSystemLibrary::ExecuteConsoleCommand(World, FString::Printf(TEXT("obj gc")));
                        }
                        return;
                    }
                }
                
                if (CmdImpl)
                {
                    FString CmdForJs = TEXT("");
                    FString ArgsForJs = TEXT("");

                    if (Args.Num() > 0)
                    {
                        CmdForJs = Args[0];
                    }
                    if (Args.Num() > 1)
                    {
                        ArgsForJs = Args[1];
                    }
                    CmdImpl(CmdForJs, ArgsForJs);
                }
                else
                {
                    UE_LOG(Puerts, Error, TEXT("Puerts command not initialized"));
                }
            }));
    this->OnPostEngineInit();

    if (Enabled)
    {
        GUObjectArray.AddUObjectCreateListener(static_cast<FUObjectArray::FUObjectCreateListener*>(this));
        GUObjectArray.AddUObjectDeleteListener(static_cast<FUObjectArray::FUObjectDeleteListener*>(this));

        MakeEditorJsEnv();
        GEditor->OnBlueprintPreCompile().AddRaw(this, &FPuertsEditorModule::OnBlueprintPreCompile);
    }
    
}

TSharedPtr<FKismetCompilerContext> MakeCompiler(
    UBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
{
    return MakeShared<FTypeScriptCompilerContext>(CastChecked<UTypeScriptBlueprint>(InBlueprint), InMessageLog, InCompileOptions);
}

void FPuertsEditorModule::OnPostEngineInit()
{
    if (Enabled)
    {
        FKismetCompilerContext::RegisterCompilerForBP(UTypeScriptBlueprint::StaticClass(), &MakeCompiler);

        SourceFileWatcher = MakeShared<PUERTS_NAMESPACE::FSourceFileWatcher>(
            [this](const FString& InPath)
            {
                if (JsEnv.IsValid())
                {
                    TArray<uint8> Source;
                    if (FFileHelper::LoadFileToArray(Source, *InPath))
                    {
                        JsEnv->ReloadSource(InPath, puerts::PString((const char*) Source.GetData(), Source.Num()));
                    }
                    else
                    {
                        UE_LOG(Puerts, Error, TEXT("read file fail for %s"), *InPath);
                    }
                }
            });
        JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
            std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(TEXT("JavaScript")),
            std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(), -1,
            [this](const FString& InPath)
            {
                if (SourceFileWatcher.IsValid())
                {
                    SourceFileWatcher->OnSourceLoaded(InPath);
                }
            },
            TEXT("--max-old-space-size=2048"), nullptr, nullptr, true);

        JsEnv->Start("PuertsEditor/CodeAnalyze");
    }
}

void FPuertsEditorModule::MakeEditorJsEnv()
{
    // FEditorDelegates::PrePIEEnded.AddSP(this, &FAblAbilityEditor::OnPIEPreEnded);
    // FEditorDelegates::EndPIE.AddSP(this, &FAblAbilityEditor::OnPIEEnded);
    FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
    LevelEditor.OnMapChanged().AddRaw(this, &FPuertsEditorModule::ChangeTabWorld);

    EditorSourceFileWatcher = MakeShared<PUERTS_NAMESPACE::FSourceFileWatcher>(
        [this](const FString& InPath)
        {
            if (EditorEnv.IsValid())
            {
                FString FileName = FPaths::GetCleanFilename(InPath);
                UE_LOG(LogTemp, Warning, TEXT("EditorSourceFileWatcher Reload:%s"), *FileName);
                TArray<uint8> Source;
                if (FFileHelper::LoadFileToArray(Source, *InPath))
                {
                    EditorEnv->ReloadSource(InPath, puerts::PString((const char*) Source.GetData(), Source.Num()));
                }
                else
                {
                    UE_LOG(Puerts, Error, TEXT("read file fail for %s"), *InPath);
                }
            }
        });

	const UPuertsSetting& Settings = *GetDefault<UPuertsSetting>();
	EditorEnv = MakeShared<puerts::FJsEnv>(std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(TEXT("JavaScript")),
		std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(), Settings.DebugEnable?Settings.DebugEditorPort:-1,
		[this](const FString& InPath)
		{
			if (EditorSourceFileWatcher.IsValid())
			{
				FString FileName = FPaths::GetCleanFilename(InPath);
				EditorSourceFileWatcher->OnSourceLoaded(InPath);
			}
		}, FString(), nullptr, nullptr, true);
	if (Settings.WaitDebugger)
	{
		EditorEnv->WaitDebugger(Settings.WaitDebuggerTimeout);
	}
	
    TArray<TPair<FString, UObject*>> Arguments;
    EditorHelper = TStrongObjectPtr(NewObject<UEditorJSHelper>(GEditor->GetEditorWorldContext().World()));
    EditorHelper->SetFlags(RF_Transient);
    Arguments.Add(TPair<FString, UEditorJSHelper*>(TEXT("EditorHelper"), EditorHelper.Get())); // 可选步骤
    EditorEnv->Start("Editor/EditorStart", Arguments);
}

void FPuertsEditorModule::ChangeTabWorld(UWorld* World, EMapChangeType MapChangeType)
{
    if (!EditorHelper)
    {
        return;
    }
	
    if (MapChangeType == EMapChangeType::TearDownWorld)
    {
        // We need to Delete the UMG widget if we are tearing down the World it was built with.
        if (World == EditorHelper->GetWorld())
        {
            EditorHelper->Rename(nullptr, GetTransientPackage());
        }
    	EditorEnv->ForceReleaseWorldReference(World);
        EditorEnv->RequestFullGarbageCollectionForTesting();
    }
    else if (MapChangeType != EMapChangeType::SaveMap)
    {
        // 重新指定World
        if (EditorHelper->GetOutermost() == GetTransientPackage())
        {
            EditorHelper->Rename(TEXT("JSEditorHelper"), GEditor->GetEditorWorldContext().World());
        }
    }
}

void FPuertsEditorModule::OnBlueprintPreCompile(UBlueprint* Blueprint)
{
    if (EditorEnv.IsValid() && Blueprint->GeneratedClass)
    {
        EditorEnv->ClearClassInfo(Blueprint->GeneratedClass);
    }
}

// void OnPIEPreEnded(bool bIsSimulating)
// {
//     if (EditorEnv.IsValid())
//     {
//         EditorEnv->RequestFullGarbageCollectionForTesting();
//     }
// }
//
// void OnPIEEnded(bool bIsSimulating)
// {
//     // 编辑对象将要没了，提前设置广播一下
//     // UBlueprint* Blueprint = GetBlueprintObj();
//     // if (Blueprint)
//     // {
//     //     Blueprint->SetObjectBeingDebugged(nullptr);
//     // }
//     // OnSetDebugObject();
//     if (JsEnv.IsValid())
//     {
//         JsEnv->RequestFullGarbageCollectionForTesting();
//     }
// }

void FPuertsEditorModule::NotifyUObjectCreated(const class UObjectBase* InObject, int32 Index)
{
    if (EditorEnv.IsValid() && InObject->GetClass()->ImplementsInterface(UTsInterface::StaticClass()))
    {
        UObject* Object = (UObject*)InObject;
        if (!Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
        {
            UObject* Outermost = Object->GetOutermostObject();
            UWorld* World = Outermost->GetWorld();
            UWorld* EWorld = GEditor->GetEditorWorldContext().World();
            if (World == EWorld)
            {
                if (UUserWidget* Widget = Cast<UUserWidget>(Object))
                {
                    if (Widget->IsDesignTime())
                        return;
                }
                BindToEditorEnv(Object);
            	CachedBindToEditorObjects.Add((UObjectBase*)InObject);
            }
        }
    }
}

void FPuertsEditorModule::NotifyUObjectDeleted(const class UObjectBase* InObject, int32 Index)
{
    if (EditorEnv.IsValid() && CachedBindToEditorObjects.Contains(InObject) && !GExitPurge)
    {
        UObject* Object = (UObject*)InObject;
    	CachedBindToEditorObjects.Remove(InObject);
        UnbindToEditorEnv(Object);
    }
}

#if ENGINE_MINOR_VERSION > 22 || ENGINE_MAJOR_VERSION > 4
void FPuertsEditorModule::OnUObjectArrayShutdown()
{
    if (Enabled)
    {
        GUObjectArray.RemoveUObjectCreateListener(static_cast<FUObjectArray::FUObjectCreateListener*>(this));
        GUObjectArray.RemoveUObjectDeleteListener(static_cast<FUObjectArray::FUObjectDeleteListener*>(this));
    }
}
#endif

void FPuertsEditorModule::BindToEditorEnv(UObject* Object)
{
    UClass* Class = Object->GetClass();

    // 一般UObject创建通知到TS
    static FName Name_GetModuleName("GetModuleName");
    UFunction *Func = Class->FindFunctionByName(Name_GetModuleName);
    check(Func);
    if (Func->GetNativeFunc() && IsInGameThread() && Func->Script.Num() > 0)
    {
        FString ModuleName;
        UObject *DefaultObject = Class->GetDefaultObject();
        DefaultObject->UObject::ProcessEvent(Func, &ModuleName);
        if (ModuleName.Len() < 1)
        {
            ModuleName = Object->GetName();
        }

        EditorHelper->OnTsBindObjectCreated.Broadcast(Object, Object->GetUniqueID(), ModuleName);

        // Widget 相关绑定
        if (UUserWidget* Widget = Cast<UUserWidget>(Object))
        {
            // TODO waynnewu Bugfix: 这里找到的Construct是UUserWidget本身的,应该自行添加UFunction
            
            UFunction* ConstructFunc = Widget->FindFunction("Construct");
            ConstructFunc->FunctionFlags |= FUNC_BlueprintEvent | FUNC_Public | FUNC_Native;
            if (ConstructFunc->Script.Num() > 0)
            {
                UE_LOG(Puerts, Error, TEXT("BindToEditorEnv with existed \"Construct\" function class:%s"), *Class->GetName());
            }
            ConstructFunc->SetNativeFunc(&FPuertsEditorModule::EditorWidgetConstruct);

            UFunction* DestructFunc = Widget->FindFunction("Destruct");
            DestructFunc->FunctionFlags |= FUNC_BlueprintEvent | FUNC_Public | FUNC_Native;
            if (DestructFunc->Script.Num() > 0)
            {
                UE_LOG(Puerts, Error, TEXT("BindToEditorEnv with existed \"Destruct\" function class:%s"), *Class->GetName());
            }
            DestructFunc->SetNativeFunc(&FPuertsEditorModule::EditorWidgetDestruct);
        }
    }
}

void FPuertsEditorModule::UnbindToEditorEnv(UObject* Object)
{
    UClass* Class = Object->GetClass();

    // 一般UObject销毁通知到TS
    static FName Name_GetModuleName("GetModuleName");
    UFunction *Func = Class->FindFunctionByName(Name_GetModuleName);
    check(Func);
    if (Func->GetNativeFunc() && IsInGameThread() && Func->Script.Num() > 0)
    {
        FString ModuleName;
        UObject *DefaultObject = Class->GetDefaultObject();
    	if(!DefaultObject->IsUnreachable())
    	{
    		DefaultObject->UObject::ProcessEvent(Func, &ModuleName);
    		if (ModuleName.Len() < 1)
    		{
    			ModuleName = Object->GetName();
    		}
    	}
    	EditorHelper->OnTsBindObjectDeleted.Broadcast(Object->GetUniqueID(), ModuleName);
    }
}


DEFINE_FUNCTION(FPuertsEditorModule::EditorWidgetConstruct)
{
    // TODO waynnewu 移除判断
    if(Context->GetClass()->ImplementsInterface(UTsInterface::StaticClass()))
    {
        ((FPuertsEditorModule*)&IPuertsEditorModule::Get())->EditorHelper->OnTsUserWidgetConstruct.Broadcast(Cast<UUserWidget>(Context));
    }
}

DEFINE_FUNCTION(FPuertsEditorModule::EditorWidgetDestruct)
{
    // TODO waynnewu 移除判断
    if(Context->GetClass()->ImplementsInterface(UTsInterface::StaticClass()))
    {
        FPuertsEditorModule* Module = (FPuertsEditorModule*)&IPuertsEditorModule::Get();
    
        Module->EditorHelper->OnTsUserWidgetDestruct.Broadcast(Cast<UUserWidget>(Context));
        // Module->JsEnvEditor
    }
}

void FPuertsEditorModule::ShutdownModule()
{
    CmdImpl = nullptr;
    if (JsEnv.IsValid())
    {
        JsEnv.Reset();
    }
    if (SourceFileWatcher.IsValid())
    {
        SourceFileWatcher.Reset();
    }

    if (EditorEnv.IsValid())
    {
        EditorEnv.Reset();
    }
    if (EditorSourceFileWatcher.IsValid())
    {
        EditorSourceFileWatcher.Reset();
    }
}

int FPuertsEditorModule::GenAndCompileJS()
{
	if (CompileJSFunc)
	{
		IDeclarationGenerator::Get().CommandGenUeDts(true,NAME_None);
		return CompileJSFunc();
	}
	UE_LOG(Puerts, Error, TEXT("GenAndCompileJS not Bind"));
	return 0;
}

FString FPuertsEditorModule::EditorJsEnvCurrentStack()
{
	return EditorEnv->CurrentStackTrace();
}

void FPuertsEditorModule::OnPreBeginPIE(bool bIsSimulating)
{
    if (Enabled)
    {
    	EditorHelper->PreBeginPIE();
    }
}

void FPuertsEditorModule::OnPreEndPIE(bool bIsSimulating)
{
    if (Enabled)
    {
    	EditorHelper->PreEndPIE();
        EditorEnv->RequestFullGarbageCollectionForTesting();

    	const TIndirectArray<FWorldContext>& WorldContextList = GEditor->GetWorldContexts();
    	for (const FWorldContext& WorldContext : WorldContextList)
    	{
    		if (WorldContext.World() && WorldContext.World()->WorldType == EWorldType::PIE)
    		{
		    	EditorEnv->ForceReleaseWorldReference(WorldContext.World());
    		}
    	}
    }
}

void FPuertsEditorModule::EndPIE(bool bIsSimulating)
{
}

UE_ENABLE_OPTIMIZATION