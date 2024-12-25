/*
 * Tencent is pleased to support the open source community by making Puerts available.
 * Copyright (C) 2020 THL A29 Limited, a Tencent company.  All rights reserved.
 * Puerts is licensed under the BSD 3-Clause License, except for the third-party components listed in the file 'LICENSE' which may
 * be subject to their corresponding license terms. This file is subject to the terms and conditions defined in file 'LICENSE',
 * which is part of this source code package.
 */

#include "PuertsModule.h"
#include "JsEnv.h"
#include "JsEnvGroup.h"
#include "PuertsSetting.h"
#if WITH_EDITOR
#include "Editor.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Internationalization/Regex.h"
#include "LevelEditor.h"
#include "Misc/HotReloadInterface.h"
#include "GameDelegates.h"
#include "BlueprintActionDatabase.h"
#endif
#include "Commandlets/Commandlet.h"
#include "TypeScriptGeneratedClass.h"
#include "EditorJSHelper.h"
#include "JsInterface.h"
#include "JsMixinInterface.h"
#include "SourceFileWatcher.h"
#include "Blueprint/UserWidget.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "GMPBinding.h"
DEFINE_LOG_CATEGORY_STATIC(PuertsModule, Log, All);

#define LOCTEXT_NAMESPACE "FPuertsModule"




class FPuertsModule : public IPuertsModule,
                      public FUObjectArray::FUObjectCreateListener,
                      public FUObjectArray::FUObjectDeleteListener
{
    /** IModuleInterface implementation */
    void StartupModule() override;
    void ShutdownModule() override;
    
public:
    virtual void NotifyUObjectCreated(const class UObjectBase* InObject, int32 Index) override;
    virtual void NotifyUObjectDeleted(const class UObjectBase* InObject, int32 Index) override;

	// JYGame Begin
	virtual void CallMixinConstructor(UObject* Object) override;

	virtual void EvalJS(const FString& Source,UWorld* InWorld) override;

	virtual puerts::FJsEnv* GetEnv() override;

    virtual FString CurrentStackTrace() override;
	// JYGame End
	
#if ENGINE_MINOR_VERSION > 22 || ENGINE_MAJOR_VERSION > 4
    virtual void OnUObjectArrayShutdown() override;
#endif

	// JYGame Begin
	FDelegateHandle OnAsyncLoadingFlushUpdateDelHandle;
	
	FCriticalSection CandidatesCS;

	TArray<FWeakObjectPtr> Candidates;

	void OnAsyncLoadingFlushUpdate();
	// JYGame End

	// HotFix Begin
	
	/**
	 * 检查本地热更版本,是否有效,如果无效需要删除本地热更文件和Json信息
	 * @return 
	 */
	virtual bool CheckHotFixVersion();
 
	virtual void DeleteContentsInDirectory(const FString& DirectoryPath);
	
	// HotFix End
	
#if WITH_EDITOR
    bool bIsInPIE = false;

    virtual bool IsInPIE() override
    {
        return bIsInPIE;
    }

    void PreBeginPIE(bool bIsSimulating);
    void EndPIE(bool bIsSimulating);
    bool HandleSettingsSaved();
    void ChangeTabWorld(UWorld* World, EMapChangeType MapChangeType);
#endif

    void RegisterSettings();

    void UnregisterSettings();

    void Enable();

    void Disable();

#pragma region HotFix
	void EnableRecord(bool Enable);

	TArray<FString> GetLoadedModules();

	void ClearLoadedModules();
#pragma endregion 

    virtual bool IsEnabled() override
    {
        return Enabled;
    }

    virtual bool IsWatchEnabled() override
    {
        return Enabled && WatchEnabled;
    }

    void ReloadModule(FName ModuleName, const FString& JsSource) override
    {
        if (Enabled)
        {
        	UE_LOG(PuertsModule, Warning, TEXT("ModuleName:%s,JsSource:%s"), *(ModuleName.ToString()), *JsSource);
            if (JsEnv.IsValid())
            {
                UE_LOG(PuertsModule, Warning, TEXT("Normal Mode ReloadModule"));
                JsEnv->ReloadModule(ModuleName, JsSource);
            }
            else if (NumberOfJsEnv > 1 && JsEnvGroup.IsValid())
            {
                UE_LOG(PuertsModule, Warning, TEXT("Group Mode ReloadModule"));
                JsEnvGroup->ReloadModule(ModuleName, JsSource);
            }
        }
    }

    void InitExtensionMethodsMap() override
    {
        if (Enabled)
        {
            if (JsEnv.IsValid())
            {
                JsEnv->InitExtensionMethodsMap();
            }
            else if (NumberOfJsEnv > 1 && JsEnvGroup.IsValid())
            {
                JsEnvGroup->InitExtensionMethodsMap();
            }
        }
    }

    std::function<int(UObject*, int)> Selector;

    void SetJsEnvSelector(std::function<int(UObject*, int)> InSelector) override
    {
        if (Enabled && NumberOfJsEnv > 1 && JsEnvGroup.IsValid())
        {
            JsEnvGroup->SetJsEnvSelector(InSelector);
        }
        Selector = InSelector;
    }

    int32 GetDebuggerPortFromCommandLine()
    {
        int32 Result = -1;

        /**
         * get command line
         */
        TArray<FString> OutTokens;
        TArray<FString> OutSwitches;
        TMap<FString, FString> OutParams;
        UCommandlet::ParseCommandLine(FCommandLine::Get(), OutTokens, OutSwitches, OutParams);

#if WITH_EDITOR
        static const auto GetPIEInstanceID = [](const TArray<FString>& InTokens) -> int32
        {
            static const int32 Start = FString{TEXT("PIEGameUserSettings")}.Len();
            static const int32 BaseCount = FString{TEXT("PIEGameUserSettings.ini")}.Len();

            const FString* TokenPtr =
                InTokens.FindByPredicate([](const FString& InToken) { return InToken.StartsWith(TEXT("GameUserSettingsINI=")); });
            if (TokenPtr == nullptr)
            {
                return INDEX_NONE;
            }

            const FRegexPattern GameUserSettingsPattern{TEXT("PIEGameUserSettings[0-9]+\\.ini")};
            FRegexMatcher GameUserSettingsMatcher{GameUserSettingsPattern, *TokenPtr};
            if (GameUserSettingsMatcher.FindNext())
            {
                const FString GameUserSettingsFile = GameUserSettingsMatcher.GetCaptureGroup(0);
                return FCString::Atoi(*GameUserSettingsFile.Mid(Start, GameUserSettingsFile.Len() - BaseCount));
            }

            return INDEX_NONE;
        };

        const bool bPIEGame = OutSwitches.Find(TEXT("PIEVIACONSOLE")) != INDEX_NONE && OutSwitches.Find(TEXT("game")) != INDEX_NONE;
        if (bPIEGame)
        {
            const int32 Index = GetPIEInstanceID(OutTokens);
            if (OutSwitches.Find(TEXT("server")) != INDEX_NONE)
            {
                Result += 999;    // for server, we add 999, 8080 -> 9079
            }
            else
            {
                Result += 10 * (Index + 1);    //  for client, we add 10 for each new process, 8080 -> 8090, 8100, 8110
            }
        }
#endif

        // we can also specify the debug port via command line, -JsEnvDebugPort

        static const FString DebugPortParam{TEXT("JsEnvDebugPort")};
        if (OutParams.Contains(DebugPortParam))
        {
            Result = FCString::Atoi(*OutParams[DebugPortParam]);
        }

        return Result;
    }

    virtual void MakeSharedJsEnv() override
    {
        const UPuertsSetting& Settings = *GetDefault<UPuertsSetting>();

        JsEnv.Reset();
        JsEnvGroup.Reset();

        NumberOfJsEnv = (Settings.NumberOfJsEnv > 1 && Settings.NumberOfJsEnv < 10) ? Settings.NumberOfJsEnv : 1;

        if (NumberOfJsEnv > 1)
        {
            if (Settings.DebugEnable)
            {
                JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(NumberOfJsEnv,
                    std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
                    std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
                    DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine,
                    nullptr, FString(), nullptr, nullptr, false);

            }
            else
            {
                JsEnvGroup = MakeShared<PUERTS_NAMESPACE::FJsEnvGroup>(NumberOfJsEnv, Settings.RootPath);
            }

            if (Selector)
            {
                JsEnvGroup->SetJsEnvSelector(Selector);
            }

            // 这种不支持等待
            if (Settings.WaitDebugger)
            {
                UE_LOG(PuertsModule, Warning, TEXT("Do not support WaitDebugger in Group Mode!"));
            }

            JsEnvGroup->RebindJs();
            UE_LOG(PuertsModule, Log, TEXT("Group Mode started! Number of JsEnv is %d"), NumberOfJsEnv);
        }
        else
        {
            if (Settings.DebugEnable)
            {
                JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(
                    std::make_shared<PUERTS_NAMESPACE::DefaultJSModuleLoader>(Settings.RootPath),
                    std::make_shared<PUERTS_NAMESPACE::FDefaultLogger>(),
                    DebuggerPortFromCommandLine < 0 ? Settings.DebugPort : DebuggerPortFromCommandLine,
                    nullptr, FString(), nullptr, nullptr, false);
            }
            else
            {
                JsEnv = MakeShared<PUERTS_NAMESPACE::FJsEnv>(Settings.RootPath);
            }

            if (Settings.WaitDebugger)
            {
                JsEnv->WaitDebugger(Settings.WaitDebuggerTimeout);
            }

            JsEnv->RebindJs();
            UE_LOG(PuertsModule, Log, TEXT("Normal Mode started!"));
        }

    	if (!OnAsyncLoadingFlushUpdateDelHandle.IsValid())
    	{
    		OnAsyncLoadingFlushUpdateDelHandle = FCoreDelegates::OnAsyncLoadingFlushUpdate.AddRaw(this, &FPuertsModule::OnAsyncLoadingFlushUpdate);
    	}
    }
    
    virtual const TArray<FString>& GetIgnoreClassListOnDTS()
    {
        return GetDefault<UPuertsSetting>()->IgnoreClassListOnDTS;
    }

    virtual const TArray<FString>& GetIgnoreStructListOnDTS()
    {
        return GetDefault<UPuertsSetting>()->IgnoreStructListOnDTS;
    }

private:
    TSharedPtr<PUERTS_NAMESPACE::FJsEnv> JsEnv;

    bool Enabled = false;

    bool WatchEnabled = true;

    int32 NumberOfJsEnv = 1;

    TSharedPtr<PUERTS_NAMESPACE::FJsEnvGroup> JsEnvGroup;

    int32 DebuggerPortFromCommandLine = -1;
};

void FPuertsModule::DeleteContentsInDirectory(const FString& DirectoryPath)
{
    // 获取文件管理器
    IFileManager& FileManager = IFileManager::Get();
	

    // 检查目录是否存在
    if (FileManager.DirectoryExists(*DirectoryPath))
    {
        // 删除目录下的所有文件
        TArray<FString> Files;
        FileManager.FindFilesRecursive(Files, *DirectoryPath, TEXT("*.*"), true, false);

        for (const FString& File : Files)
        {
            if (FileManager.Delete(*File))
            {
                UE_LOG(LogTemp, Warning, TEXT("[FPuertsModule:DeleteContentsInDirectory]: 删除文件 %s"), *File);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[FPuertsModule:DeleteContentsInDirectory]: 删除文件失败 %s"), *File);
            }
        }

        // 删除目录下的所有子目录及其内容
        TArray<FString> Directories;
        FileManager.FindFilesRecursive(Directories, *DirectoryPath, TEXT("*"), false, true);

        for (const FString& Directory : Directories)
        {
            if (Directory != TEXT(".") && Directory != TEXT("..")) // 排除当前和父目录
            {
                // 递归调用以删除子目录中的所有内容  递归查找了这里不需要再次递归，一次找完了
                //DeleteContentsInDirectory(SubDirectoryPath);
                
                // 删除子目录
                if (FileManager.DeleteDirectory(*Directory, false, true)) // 递归删除
                {
                	UE_LOG(LogTemp, Log, TEXT("[FPuertsModule:DeleteContentsInDirectory]: 删除目录%s"), *Directory);
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("[FPuertsModule:DeleteContentsInDirectory]: 删除目录失败 %s"), *Directory);
                }
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[FPuertsModule:DeleteContentsInDirectory]: 文件夹不存在 %s"), *DirectoryPath);
    }

    UE_LOG(LogTemp, Warning, TEXT("[FPuertsModule:DeleteContentsInDirectory]: 清空目录 %s"), *DirectoryPath);
}

bool FPuertsModule::CheckHotFixVersion()
{
	UE_LOG(LogTemp, Log, TEXT("[FPuertsModule:CheckHotFixVersion]：检查本地热更版本是否有效"));

	FString ConfigSuffix = TEXT("DefaultGameVersion.ini");
	const TCHAR* SectionName = TEXT("/Script/JYFramework.JYGameVersionSetting");

	// Win包内的SHA
	FString WinGameVersion;
		
	const FString VersionConfigIniPath = FConfigCacheIni::NormalizeConfigIniPath(FPaths::SourceConfigDir().Append(ConfigSuffix));

	if (GConfig->DoesSectionExist(SectionName, VersionConfigIniPath))
	{
		// 这里由ClientSHA替换为了ContentSHA，ClientSHA是JYGame的Git号,ContentSHA是Content的Git号
		// 目前热更只关注Client的变动，Cos存储桶中的对象列表使用的名称也是ClientSHA
		GConfig->GetString(SectionName, TEXT("ContentSHA"), WinGameVersion, VersionConfigIniPath);
		UE_LOG(LogTemp, Log, TEXT("[FPuertsModule:CheckHotFixVersion]：ContentSHA:%s"), *WinGameVersion);
	}

#if WITH_EDITOR
	WinGameVersion = "0000";// 在编辑器下测试用，如果是Win包不会进入
#endif

	// 包内Json路径
	FString LocalJsonFilePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/HotFix/Encrypt.json"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			
	if (!PlatformFile.FileExists(*LocalJsonFilePath))
	{
		UE_LOG(LogTemp, Log, TEXT("[FPuertsModule:CheckHotFixVersion]:本地Json为空,没有热更信息,此版本有效,无需处理."));
		return false;
	}
	
	FString HotFixGameVersion;
	FString JsonString;
 	
	if (FFileHelper::LoadFileToString(JsonString, *LocalJsonFilePath))
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
 		
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
		{
			if (!JsonObject->TryGetStringField("GameVersion", HotFixGameVersion))
			{
				UE_LOG(LogTemp, Error, TEXT("[FPuertsModule:CheckHotFixVersion]:读取JSON:GameVersion失败,请检查：%s"), *LocalJsonFilePath);
				return false;
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[FPuertsModule:CheckHotFixVersion]:读取JSON失败，请检查：%s"), *LocalJsonFilePath);
			return false;
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[FPuertsModule:CheckHotFixVersion]:LoadFileToString失败，请检查：%s"), *LocalJsonFilePath);
		return false;
	}
 
	UE_LOG(LogTemp, Log, TEXT("[FPuertsModule:CheckHotFixVersion]:HotFixGameVersion值为：%s,WinGameVersion值为:%s"), *HotFixGameVersion, *WinGameVersion);

	if (HotFixGameVersion.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[FPuertsModule:CheckForHotFix]:HotFixGameVersion版本信息为空"));
		return false;
	}

	// 检查Win包GameVersion和热更的GameVersion是否一致
	if (WinGameVersion.Equals(HotFixGameVersion))
	{
		return false;
	}
	// 双方不一致版本无效
	return true;
}

IMPLEMENT_MODULE(FPuertsModule, Puerts)

void FPuertsModule::NotifyUObjectCreated(const class UObjectBase* InObject, int32 Index)
{
    if (Enabled)
    {
    	UClass* Class = InObject->GetClass();
    	if (Class->ImplementsInterface(UJsMixinInterface::StaticClass()))
    	{
    		UFunction* Func = Class->FindFunctionByName(FName("GetModuleName"));
    		check(Func);

    		if (!Func->GetNativeFunc() || !IsInGameThread())
    		{
    			FScopeLock Lock(&CandidatesCS);
    			Candidates.Add((UObject*)InObject);
	    		return;
    		}

    		if (JsEnv.IsValid())
    		{
    			// UE_LOG(PuertsModule, Warning, TEXT("Normal Mode TryBindJs"));
    			JsEnv->TryBindJs(InObject);
    		}
    		else if (NumberOfJsEnv > 1 && JsEnvGroup.IsValid())
    		{
    			// UE_LOG(PuertsModule, Warning, TEXT("Group Mode TryBindJs"));
    			JsEnvGroup->TryBindJs(InObject);
    		}
    	}
    }
}

void FPuertsModule::NotifyUObjectDeleted(const class UObjectBase* InObject, int32 Index)
{
    // UE_LOG(PuertsModule, Warning, TEXT("NotifyUObjectDeleted, %p"), InObject);
}

void FPuertsModule::CallMixinConstructor(UObject* Object)
{
	if (Enabled)
	{
		if (JsEnv.IsValid())
		{
			JsEnv->CallMixinConstructor(Object);
		}
		else if (NumberOfJsEnv > 1 && JsEnvGroup.IsValid())
		{
			JsEnvGroup->CallMixinConstructor(Object);
		}
	}
}

void FPuertsModule::EvalJS(const FString& Source,UWorld* InWorld)
{
	if (Enabled)
	{
		JsEnv->EvalJS(Source,InWorld);
	}
}

puerts::FJsEnv* FPuertsModule::GetEnv()
{
	if (JsEnv.IsValid())
	{
		return (JsEnv.Get());
	}
	return nullptr;
}

FString FPuertsModule::CurrentStackTrace()
{
    if (JsEnv.IsValid())
    {
        return FString::Printf(TEXT("Ts Stack: %s"), *JsEnv->CurrentStackTrace());
    }
    return TEXT("获取堆栈失败");
}

#if ENGINE_MINOR_VERSION > 22 || ENGINE_MAJOR_VERSION > 4
void FPuertsModule::OnUObjectArrayShutdown()
{
    if (Enabled)
    {
        GUObjectArray.RemoveUObjectCreateListener(static_cast<FUObjectArray::FUObjectCreateListener*>(this));
        GUObjectArray.RemoveUObjectDeleteListener(static_cast<FUObjectArray::FUObjectDeleteListener*>(this));
        Enabled = false;
    }
}
#endif

#if WITH_EDITOR
void FPuertsModule::PreBeginPIE(bool bIsSimulating)
{
    bIsInPIE = true;
    if (Enabled)
    {
        MakeSharedJsEnv();
        UE_LOG(PuertsModule, Display, TEXT("JsEnv created"));
    	PuertsSupport::SetJsEnvIsValid(true);
    }
}
void FPuertsModule::EndPIE(bool bIsSimulating)
{
    bIsInPIE = false;
    if (Enabled)
    {
        JsEnv.Reset();
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Class = *It;
            if (auto TsClass = Cast<UTypeScriptGeneratedClass>(Class))
            {
                TsClass->CancelRedirection();
                TsClass->DynamicInvoker.Reset();
            }
            if (Class->ClassConstructor == UTypeScriptGeneratedClass::StaticConstructor)
            {
                auto SuperClass = Class->GetSuperClass();
                while (SuperClass)
                {
                    if (SuperClass->ClassConstructor != UTypeScriptGeneratedClass::StaticConstructor)
                    {
                        Class->ClassConstructor = SuperClass->ClassConstructor;
                        break;
                    }
                    SuperClass = SuperClass->GetSuperClass();
                }
                if (Class->ClassConstructor == UTypeScriptGeneratedClass::StaticConstructor)
                {
                    Class->ClassConstructor = nullptr;
                }
            }
        }
    	// 如果是Editor，UnMixin蓝图后要刷新蓝图编辑器
    	FBlueprintActionDatabase& ActionDatabase = FBlueprintActionDatabase::Get();
    	ActionDatabase.RefreshAll();
        UE_LOG(PuertsModule, Display, TEXT("JsEnv reset"));
    	PuertsSupport::SetJsEnvIsValid(false);
    }
}
#endif

void FPuertsModule::RegisterSettings()
{
#if WITH_EDITOR
    if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        auto SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "Puerts",
            LOCTEXT("TileSetEditorSettingsName", "Puerts Settings"),
            LOCTEXT("TileSetEditorSettingsDescription", "Configure the setting of Puerts plugin."),
            GetMutableDefault<UPuertsSetting>());

        SettingsSection->OnModified().BindRaw(this, &FPuertsModule::HandleSettingsSaved);
    }
	
#endif

#if PLATFORM_LINUX
	// linux下使用另外的配置
	FString ConfigSuffix = TEXT("Linux/LinuxPuerts.ini");
#else
	FString ConfigSuffix = TEXT("DefaultPuerts.ini");
#endif
	
	
    UPuertsSetting& Settings = *GetMutableDefault<UPuertsSetting>();
    const TCHAR* SectionName = TEXT("/Script/Puerts.PuertsSetting");
#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1) || ENGINE_MAJOR_VERSION > 5
    const FString PuertsConfigIniPath =
        FConfigCacheIni::NormalizeConfigIniPath(FPaths::SourceConfigDir().Append(ConfigSuffix));
#else
    const FString PuertsConfigIniPath = FPaths::SourceConfigDir().Append(TEXT(ConfigSuffix));
#endif
    if (GConfig->DoesSectionExist(SectionName, PuertsConfigIniPath))
    {
        GConfig->GetBool(SectionName, TEXT("AutoModeEnable"), Settings.AutoModeEnable, PuertsConfigIniPath);
        FString Text;
        GConfig->GetBool(SectionName, TEXT("DebugEnable"), Settings.DebugEnable, PuertsConfigIniPath);
        GConfig->GetBool(SectionName, TEXT("WaitDebugger"), Settings.WaitDebugger, PuertsConfigIniPath);
        GConfig->GetDouble(SectionName, TEXT("WaitDebuggerTimeout"), Settings.WaitDebuggerTimeout, PuertsConfigIniPath);
        if (!GConfig->GetInt(SectionName, TEXT("DebugPort"), Settings.DebugPort, PuertsConfigIniPath))
        {
            Settings.DebugPort = 8080;
        }
        if (!GConfig->GetInt(SectionName, TEXT("NumberOfJsEnv"), Settings.NumberOfJsEnv, PuertsConfigIniPath))
        {
            Settings.NumberOfJsEnv = 1;
        }
        GConfig->GetBool(SectionName, TEXT("WatchDisable"), Settings.WatchDisable, PuertsConfigIniPath);
    }

    DebuggerPortFromCommandLine = GetDebuggerPortFromCommandLine();
}

void FPuertsModule::UnregisterSettings()
{
#if WITH_EDITOR
    if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingsModule->UnregisterSettings("Project", "Plugins", "Puerts");
    }
#endif
}

void FPuertsModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("FPuertsModule::StartupModule..."))
    // NonPak Game 打包下, Puerts ini的加载时间晚于模块加载, 因此依然要显式的执行ini的读入, 去保证CDO里的值是正确的
    RegisterSettings();

    const UPuertsSetting& Settings = *GetDefault<UPuertsSetting>();

	//虚拟机启动前进行一次检查
	if (CheckHotFixVersion())
	{
		FString DirectoryToClear = FPaths::Combine(FPaths::ProjectDir(), "Saved/PersistentDownloadDir/Content");
		DeleteContentsInDirectory(DirectoryToClear);
		UE_LOG(LogTemp, Log, TEXT("[FPuertsModule::StartupModule]：删除无效JavaScript"))
		FString LocalJsonFilePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/HotFix/"));
		DeleteContentsInDirectory(LocalJsonFilePath);
		UE_LOG(LogTemp, Log, TEXT("[FPuertsModule::StartupModule]：删除无效无效本地EncryptJson"))
	}

#if WITH_EDITOR
   
    if (!IsRunningGame())
    {
        FEditorDelegates::PreBeginPIE.AddRaw(this, &FPuertsModule::PreBeginPIE);
        FEditorDelegates::EndPIE.AddRaw(this, &FPuertsModule::EndPIE);

        // FEditorSupportDelegates::CleanseEditor.AddRaw(this, &FPuertsModule::CleanseEditor);
        // FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
        // LevelEditor.OnMapChanged().AddRaw(this, &FPuertsModule::HandleMapChanged);
    }
#endif

#if WITH_HOT_RELOAD
#if ENGINE_MAJOR_VERSION >= 5
    FCoreUObjectDelegates::ReloadCompleteDelegate.AddLambda(
        [&](EReloadCompleteReason)
#else
    IHotReloadInterface& HotReloadSupport = FModuleManager::LoadModuleChecked<IHotReloadInterface>("HotReload");
    HotReloadSupport.OnHotReload().AddLambda(
        [&](bool)
#endif
        {
            if (Enabled)
            {
                MakeSharedJsEnv();
            }
        });
#endif

    if (Settings.AutoModeEnable)
    {
        Enable();
    }

    WatchEnabled = !Settings.WatchDisable;

    // SetJsEnvSelector([this](UObject* Obj, int Size){
    //    return 1;
    //    });
#if !(UE_BUILD_SHIPPING)
    static FAutoConsoleCommandWithWorldAndArgs MyCustomCommand(
	TEXT("JsEnv"), 
	TEXT("Eval Any Js-Code"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([this](const TArray<FString>& Args, UWorld* World) // 命令的函数体
		{
			if (Args.Num() > 0)
			{
				FString Base = Args[0];
				for (int i = 1; i < Args.Num(); i++)
				{
					Base += Args[i];
				}
				if (this->JsEnv.IsValid())
				{
					this->JsEnv->EvalJS(Base,World);
					UE_LOG(LogTemp, Warning, TEXT("JsEnv custom command was executed!"));
				}
			}
		})
	);
#endif 	
}

void FPuertsModule::Enable()
{
    Enabled = true;

#if WITH_EDITOR
    if (IsRunningGame() || IsRunningDedicatedServer())
    {
        // 处理 Standalone 模式的情况
        MakeSharedJsEnv();
    }
#else
    MakeSharedJsEnv();
#endif

    if (JsEnv.IsValid())
    {
    	//暂时默认只有一个虚拟机，后续打包看看到底几个
    	//默认打开记录
	    JsEnv->EnableRecord(true);
    	UE_LOG(LogTemp, Log, TEXT("[PuertsModule:Enable]:开始记录已经加载模块"));
    }

    GUObjectArray.AddUObjectCreateListener(static_cast<FUObjectArray::FUObjectCreateListener*>(this));
    GUObjectArray.AddUObjectDeleteListener(static_cast<FUObjectArray::FUObjectDeleteListener*>(this));
}

void FPuertsModule::Disable()
{
    Enabled = false;

    if (JsEnv.IsValid())
    {
    	//不再记录
    	EnableRecord(false);
    	UE_LOG(LogTemp, Log, TEXT("[PuertsModule:Disable]:不再已经加载模块"));
    	//释放
    	ClearLoadedModules();
    }
	
    JsEnv.Reset();
    JsEnvGroup.Reset();
    GUObjectArray.RemoveUObjectCreateListener(static_cast<FUObjectArray::FUObjectCreateListener*>(this));
    GUObjectArray.RemoveUObjectDeleteListener(static_cast<FUObjectArray::FUObjectDeleteListener*>(this));
}

#if WITH_EDITOR
bool FPuertsModule::HandleSettingsSaved()
{
    const UPuertsSetting& Settings = *GetDefault<UPuertsSetting>();

    if (Settings.AutoModeEnable != Enabled)
    {
        if (Settings.AutoModeEnable)
        {
            Enable();
        }
        else
        {
            Disable();
        }
    }
    return true;
}

#endif


void FPuertsModule::ShutdownModule()
{
#if WITH_EDITOR
    UnregisterSettings();
    if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
    {
        LevelEditor->OnMapChanged().RemoveAll(this);
    }
#endif
    if (Enabled)
    {
        Disable();
    	if (OnAsyncLoadingFlushUpdateDelHandle.IsValid())
    	{
    		FCoreDelegates::OnAsyncLoadingFlushUpdate.Remove(OnAsyncLoadingFlushUpdateDelHandle);
    	}
    }
    
}

void FPuertsModule::OnAsyncLoadingFlushUpdate()
{
	if (!JsEnv) return;

	FScopeLock Lock(&CandidatesCS);
	for (int32 i = Candidates.Num() - 1; i >= 0; --i)
	{
		FWeakObjectPtr ObjectPtr = Candidates[i];
		if (!ObjectPtr.IsValid())
		{
			Candidates.RemoveAt(i);
			continue;;
		}

		UObject* Obj = ObjectPtr.Get();
		UFunction *Func = Obj->FindFunction(FName("GetModuleName"));
		if (!Func || !Func->GetNativeFunc())
		{
			continue;
		}

		Candidates.RemoveAt(i);
		JsEnv->TryBindJs(Obj);
	}
}
#pragma region HotFix
void FPuertsModule::EnableRecord(bool Enable)
{
	if (JsEnv.IsValid())
	{
		JsEnv->EnableRecord(Enable);
		UE_LOG(LogTemp, Log, TEXT("[PuertsModule:EnableRecord]:%s记录已经加载模块"), Enable ? TEXT("开始") : TEXT("停止"));
	}
}

TArray<FString> FPuertsModule::GetLoadedModules()
{
	if (JsEnv.IsValid())
	{
		return JsEnv->GetLoadedModules();
	}
	return TArray<FString>();
}

void FPuertsModule::ClearLoadedModules()
{
	if (JsEnv.IsValid())
	{
		JsEnv->ClearLoadedModules();
	}
}
#pragma endregion 
#undef LOCTEXT_NAMESPACE