/*
 * Tencent is pleased to support the open source community by making Puerts available.
 * Copyright (C) 2020 THL A29 Limited, a Tencent company.  All rights reserved.
 * Puerts is licensed under the BSD 3-Clause License, except for the third-party components listed in the file 'LICENSE' which may
 * be subject to their corresponding license terms. This file is subject to the terms and conditions defined in file 'LICENSE',
 * which is part of this source code package.
 */

#include "JsEnv.h"
#include "JsEnvImpl.h"

namespace PUERTS_NAMESPACE
{
FJsEnv::FJsEnv(const FString& ScriptRoot)
{
    GameScript = std::make_unique<FJsEnvImpl>(ScriptRoot);
}

FJsEnv::FJsEnv(std::shared_ptr<IJSModuleLoader> InModuleLoader, std::shared_ptr<ILogger> InLogger, int InDebugPort,
    std::function<void(const FString&)> InOnSourceLoadedCallback, const FString InFlags, void* InExternalRuntime,
    void* InExternalContext, bool IsEditorEnv)
{
    GameScript = std::make_unique<FJsEnvImpl>(
        std::move(InModuleLoader), InLogger, InDebugPort, InOnSourceLoadedCallback, InFlags, InExternalRuntime, InExternalContext, IsEditorEnv);
}

void FJsEnv::Start(const FString& ModuleName, const TArray<TPair<FString, UObject*>>& Arguments)
{
    GameScript->Start(ModuleName, Arguments);
}

bool FJsEnv::IdleNotificationDeadline(double DeadlineInSeconds)
{
    return GameScript->IdleNotificationDeadline(DeadlineInSeconds);
}

void FJsEnv::LowMemoryNotification()
{
    GameScript->LowMemoryNotification();
}

void FJsEnv::RequestMinorGarbageCollectionForTesting()
{
    GameScript->RequestMinorGarbageCollectionForTesting();
}

void FJsEnv::RequestFullGarbageCollectionForTesting()
{
    GameScript->RequestFullGarbageCollectionForTesting();
}

void FJsEnv::ForceReleaseWorldReference(UWorld* World)
{
	GameScript->ForceReleaseWorldReference(World);
}

void FJsEnv::WaitDebugger(double timeout)
{
    GameScript->WaitDebugger(timeout);
}

#if !defined(ENGINE_INDEPENDENT_JSENV)
void FJsEnv::TryBindJs(const class UObjectBase* InObject)
{
    GameScript->TryBindJs(InObject);
}

void FJsEnv::RebindJs()
{
    GameScript->RebindJs();
}
#endif

FString FJsEnv::CurrentStackTrace()
{
    return GameScript->CurrentStackTrace();
}

void FJsEnv::InitExtensionMethodsMap()
{
    GameScript->InitExtensionMethodsMap();
}

void FJsEnv::ClearClassInfo(UClass* Class)
{
    GameScript->ClearClassInfo(Class);
}

void FJsEnv::CallMixinConstructor(UObject *Object)
{
	GameScript->CallMixinConstructor(Object);
}

void FJsEnv::EvalJS(const FString& JsSource,UWorld* InWorld)
{
	GameScript->EvalJS(JsSource,InWorld);
}

#pragma region HotFix
void FJsEnv::EnableRecord(bool Enable)
{
	GameScript->EnableRecord(Enable);
}

TArray<FString> FJsEnv::GetLoadedModules()
{
	return GameScript->GetLoadedModules();
}

void FJsEnv::ClearLoadedModules()
{
	GameScript->ClearLoadedModules();
}
#pragma endregion 

void FJsEnv::ReloadModule(FName ModuleName, const FString& JsSource)
{
    GameScript->ReloadModule(ModuleName, JsSource);
}

void FJsEnv::ReloadSource(const FString& Path, const PString& JsSource)
{
    GameScript->ReloadSource(Path, JsSource);
}

void FJsEnv::OnSourceLoaded(std::function<void(const FString&)> Callback)
{
    GameScript->OnSourceLoaded(Callback);
}
}    // namespace PUERTS_NAMESPACE
