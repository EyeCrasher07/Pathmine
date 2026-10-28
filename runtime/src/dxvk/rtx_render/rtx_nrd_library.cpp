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
#include "rtx_nrd_library.h"

namespace nrd {
  DispatchNRD dispatch{};

  std::unique_ptr<dxvk::util::SharedLibrary> initialize(std::string& error) {
    error.clear();
    std::filesystem::path path = dxvk::util::SharedLibrary::moduleDirectory(reinterpret_cast<const void*>(&initialize));
    if (path.empty()) {
      error = "Unable to locate the Remix runtime directory for NRD";
      return {};
    }
#ifdef _WIN32
    path.append("NRD.dll");
#else
    path.append("libNRD.so");
#endif
    auto library = std::make_unique<dxvk::util::SharedLibrary>(path);
    if (!*library) {
      error = "Unable to load NRD: " + library->error();
      return {};
    }

    DispatchNRD nextDispatch{};
#define GET_PROC_MACRO(proc) \
    nextDispatch.proc = reinterpret_cast<pfn##proc>(library->symbol(#proc)); \
    if (!nextDispatch.proc) { \
      error = "NRD is missing export " #proc; \
      return {}; \
    }
    GET_PROC_MACRO(CreateInstance);
    GET_PROC_MACRO(DestroyInstance);
    GET_PROC_MACRO(GetLibraryDesc);
    GET_PROC_MACRO(GetInstanceDesc);
    GET_PROC_MACRO(SetCommonSettings);
    GET_PROC_MACRO(SetDenoiserSettings);
    GET_PROC_MACRO(GetComputeDispatches);
    GET_PROC_MACRO(GetResourceTypeString);
    GET_PROC_MACRO(GetDenoiserString);
#undef GET_PROC_MACRO

    const LibraryDesc& desc = nextDispatch.GetLibraryDesc();
    if (desc.versionMajor != NRD_VERSION_MAJOR || desc.versionMinor != NRD_VERSION_MINOR || desc.versionBuild != NRD_VERSION_BUILD) {
      error = "Incorrect version of NRD. The matching NRD library must be beside the Remix runtime";
      return {};
    }

    dispatch = nextDispatch;
    return library;
  }
}
