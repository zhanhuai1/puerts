/*
 * Tencent is pleased to support the open source community by making Puerts available.
 * Copyright (C) 2020 THL A29 Limited, a Tencent company.  All rights reserved.
 * Puerts is licensed under the BSD 3-Clause License, except for the third-party components listed in the file 'LICENSE' which may
 * be subject to their corresponding license terms. This file is subject to the terms and conditions defined in file 'LICENSE',
 * which is part of this source code package.
 */

#pragma once

#include <cstdio>
#include <functional>

#include "Modules/ModuleManager.h"
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "EditorJSHelper.h"

class IPuertsEditorModule : public IModuleInterface
{
public:
    static inline IPuertsEditorModule& Get()
    {
        return FModuleManager::LoadModuleChecked<IPuertsEditorModule>("PuertsEditor");
    }

    static inline bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded("PuertsEditor");
    }

    virtual void SetCmdImpl(std::function<void(const FString&, const FString&)> Func) = 0;

	virtual void SetCompileJSFunc(std::function<int()>) = 0;
	
	virtual FString EditorJsEnvCurrentStack() = 0;

	virtual int GenAndCompileJS() = 0;
	
	virtual TStrongObjectPtr<UEditorJSHelper> GetEditorJSHelper() = 0;
};
