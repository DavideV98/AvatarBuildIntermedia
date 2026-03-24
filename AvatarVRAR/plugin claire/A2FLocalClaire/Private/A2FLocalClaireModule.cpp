/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 - 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "A2FLocalClaireModule.h"

// plugin includes
#include "A2FLocal.h"

// engine includes
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"


DEFINE_LOG_CATEGORY_STATIC(LogACEA2FLocalClaire, Log, All);

static FString GetModelDir()
{
	// Make sure we have the absolute path to the plugin directory
	const FString PluginBaseDir = IPluginManager::Get().FindPlugin(TEXT(UE_PLUGIN_NAME))->GetBaseDir();
	FString PluginBaseDirAbsolute = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*PluginBaseDir);

	return FPaths::Combine(*PluginBaseDirAbsolute, TEXT("ThirdParty"), TEXT("Nvigi"), TEXT("Models"));
}

void FA2FLocalClaireModule::StartupModule()
{
	TSharedPtr<IPlugin> ThisPlugin = IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME);
	UE_LOG(LogACEA2FLocalClaire, Log, TEXT("Loaded %s plugin version %s"), *ThisPlugin->GetName(), *ThisPlugin->GetDescriptor().VersionName);

	FString ModelDir = GetModelDir();
	FString ModelGUID = "{5816ADA6-AF13-4031-A473-F7939C7A37C6}";

	FString A2FModelDir = FPaths::Combine(ModelDir, "nvaim.plugin.a2f.trt", ModelGUID);
	TMap <FString, float> DefaultFaceParams = GetDefaultFaceParams30(A2FModelDir);
	Provider = MakeUnique<FA2FLocal>(ModelDir, ModelGUID, FName(TEXT("LocalA2F-Claire")), DefaultFaceParams);
	if (Provider.IsValid() && Provider->IsAvailable())
	{
		Provider->Register();
	}
}

void FA2FLocalClaireModule::ShutdownModule()
{
	if (Provider.IsValid())
	{
		Provider.Reset();
	}
}


IMPLEMENT_MODULE(FA2FLocalClaireModule, A2FLocalClaire)

