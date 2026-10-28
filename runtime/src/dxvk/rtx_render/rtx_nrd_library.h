/*
* Copyright (c) 2022-2026, NVIDIA CORPORATION. All rights reserved.
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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

#include <memory>
#include <string>

#include <NRD.h>

#include "../../util/util_library.h"

namespace nrd {
  using pfnCreateInstance = Result(*)(const InstanceCreationDesc& instanceCreationDesc, Instance*& instance);
  using pfnDestroyInstance = void (*)(Instance& instance);
  using pfnGetLibraryDesc = const LibraryDesc& (*)();
  using pfnGetInstanceDesc = const InstanceDesc& (*)(const Instance& instance);
  using pfnSetCommonSettings = Result (*)(Instance& instance, const CommonSettings& commonSettings);
  using pfnSetDenoiserSettings = Result (*)(Instance& instance, Identifier identifier, const void* denoiserSettings);
  using pfnGetComputeDispatches = Result (*)(Instance& instance, const Identifier* identifiers, uint32_t identifiersNum, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescsNum);
  using pfnGetResourceTypeString = const char* (*)(ResourceType resourceType);
  using pfnGetDenoiserString = const char* (*)(Denoiser denoiser);

  struct DispatchNRD {
    pfnCreateInstance CreateInstance;
    pfnDestroyInstance DestroyInstance;
    pfnGetLibraryDesc GetLibraryDesc;
    pfnGetInstanceDesc GetInstanceDesc;
    pfnSetCommonSettings SetCommonSettings;
    pfnSetDenoiserSettings SetDenoiserSettings;
    pfnGetComputeDispatches GetComputeDispatches;
    pfnGetResourceTypeString GetResourceTypeString;
    pfnGetDenoiserString GetDenoiserString;
  };

  extern DispatchNRD dispatch;

  std::unique_ptr<dxvk::util::SharedLibrary> initialize(std::string& error);
}
