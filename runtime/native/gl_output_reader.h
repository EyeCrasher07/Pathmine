// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once
#define GLFW_INCLUDE_NONE
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>

#include <array>
#include <stdexcept>
#include <vector>

#include "interop_info.h"
#include "util/util_error.h"
#include "util/util_scoped_fd.h"

namespace dxvk::native::test {
  inline void require(bool condition, const char* pMessage) {
    if (!condition) {
      throw std::runtime_error(pMessage);
    }
  }

  inline void checkGl() {
    require(glGetError() == GL_NO_ERROR, "native original-core OpenGL interop error");
  }

  template<typename F>
  void requireFailure(F&& work) {
    try {
      work();
    } catch (const dxvk::DxvkError&) {
      return;
    }
    throw std::runtime_error("native output accepted an invalid frame transition");
  }

  template<typename T>
  T load(const char* pName) {
    const auto function = reinterpret_cast<T>(glfwGetProcAddress(pName));
    require(function != nullptr, pName);
    return function;
  }

  struct Window {
    GLFWwindow* pHandle = nullptr;
    ~Window() {
      if (pHandle) {
        glfwDestroyWindow(pHandle);
      }
      glfwTerminate();
    }
  };

  class Reader {
    struct Image {
      GLuint memory = 0;
      GLuint texture = 0;
      GLuint framebuffer = 0;
      VkExtent3D extent { 0, 0, 1 };
      int fd = -1;
    };
    Image m_color;
    Image m_depth;
    Image m_overlay;
    std::vector<float> m_depthPixels;
    std::vector<unsigned char> m_overlayPixels;
    std::array<GLuint, 2> m_semaphores { };
    uint64_t m_clientValue = 0;
    PFNGLCREATEMEMORYOBJECTSEXTPROC m_createMemory = load<PFNGLCREATEMEMORYOBJECTSEXTPROC>("glCreateMemoryObjectsEXT");
    PFNGLDELETEMEMORYOBJECTSEXTPROC m_deleteMemory = load<PFNGLDELETEMEMORYOBJECTSEXTPROC>("glDeleteMemoryObjectsEXT");
    PFNGLMEMORYOBJECTPARAMETERIVEXTPROC m_memoryParameter = load<PFNGLMEMORYOBJECTPARAMETERIVEXTPROC>("glMemoryObjectParameterivEXT");
    PFNGLIMPORTMEMORYFDEXTPROC m_importMemory = load<PFNGLIMPORTMEMORYFDEXTPROC>("glImportMemoryFdEXT");
    PFNGLTEXTURESTORAGEMEM2DEXTPROC m_storage = load<PFNGLTEXTURESTORAGEMEM2DEXTPROC>("glTextureStorageMem2DEXT");
    PFNGLCREATESEMAPHORESNVPROC m_createSemaphores = load<PFNGLCREATESEMAPHORESNVPROC>("glCreateSemaphoresNV");
    PFNGLDELETESEMAPHORESEXTPROC m_deleteSemaphores = load<PFNGLDELETESEMAPHORESEXTPROC>("glDeleteSemaphoresEXT");
    PFNGLSEMAPHOREPARAMETERIVNVPROC m_semaphoreType = load<PFNGLSEMAPHOREPARAMETERIVNVPROC>("glSemaphoreParameterivNV");
    PFNGLSEMAPHOREPARAMETERUI64VEXTPROC m_semaphoreValue = load<PFNGLSEMAPHOREPARAMETERUI64VEXTPROC>("glSemaphoreParameterui64vEXT");
    PFNGLIMPORTSEMAPHOREFDEXTPROC m_importSemaphore = load<PFNGLIMPORTSEMAPHOREFDEXTPROC>("glImportSemaphoreFdEXT");
    PFNGLWAITSEMAPHOREEXTPROC m_wait = load<PFNGLWAITSEMAPHOREEXTPROC>("glWaitSemaphoreEXT");
    PFNGLSIGNALSEMAPHOREEXTPROC m_signal = load<PFNGLSIGNALSEMAPHOREEXTPROC>("glSignalSemaphoreEXT");

    void clearImage(Image& image) {
      if (image.framebuffer) {
        glDeleteFramebuffers(1, &image.framebuffer);
      }
      if (image.texture) {
        glDeleteTextures(1, &image.texture);
      }
      if (image.memory) {
        m_deleteMemory(1, &image.memory);
      }
      image = { };
    }

    void prepareImage(Image& image, int fd, uint64_t allocationSize, VkExtent3D extent, GLenum format) {
      if (image.fd == fd && image.extent.width == extent.width && image.extent.height == extent.height
          && image.extent.depth == extent.depth) {
        return;
      }
      clearImage(image);
      if (fd < 0) {
        return;
      }
      image.fd = fd;
      image.extent = extent;
      m_createMemory(1, &image.memory);
      const GLint dedicated = GL_TRUE;
      m_memoryParameter(image.memory, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
      checkGl();
      auto ownedFd = dxvk::util::ScopedFd::duplicate(fd);
      require(ownedFd.get() >= 3, "cannot duplicate original image FD");
      m_importMemory(image.memory, allocationSize, GL_HANDLE_TYPE_OPAQUE_FD_EXT, ownedFd.get());
      checkGl();
      ownedFd.release();
      glCreateTextures(GL_TEXTURE_2D, 1, &image.texture);
      m_storage(image.texture, 1, format, extent.width, extent.height, image.memory, 0);
      glCreateFramebuffers(1, &image.framebuffer);
      glNamedFramebufferTexture(image.framebuffer, GL_COLOR_ATTACHMENT0, image.texture, 0);
      require(glCheckNamedFramebufferStatus(image.framebuffer, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
        "incomplete original-core interop framebuffer");
      checkGl();
    }

  public:
    explicit Reader(const dxvk::native::OutputInteropInfo& info) {
      try {
        m_createSemaphores(2, m_semaphores.data());
        const GLint type = GL_SEMAPHORE_TYPE_TIMELINE_NV;
        const std::array<int, 2> fds { info.rendererDoneFd, info.clientDoneFd };
        for (size_t index = 0; index < fds.size(); index++) {
          m_semaphoreType(m_semaphores[index], GL_SEMAPHORE_TYPE_NV, &type);
          checkGl();
          auto ownedFd = dxvk::util::ScopedFd::duplicate(fds[index]);
          require(ownedFd.get() >= 3, "cannot duplicate original semaphore FD");
          m_importSemaphore(m_semaphores[index], GL_HANDLE_TYPE_OPAQUE_FD_EXT, ownedFd.get());
          checkGl();
          ownedFd.release();
        }
      } catch (...) {
        m_deleteSemaphores(2, m_semaphores.data());
        throw;
      }
    }

    ~Reader() {
      glFinish();
      clearImage(m_color);
      clearImage(m_depth);
      clearImage(m_overlay);
      m_deleteSemaphores(2, m_semaphores.data());
    }

    const std::vector<float>& depthPixels() const { return m_depthPixels; }
    const std::vector<unsigned char>& overlayPixels() const { return m_overlayPixels; }

    template<typename Output>
    std::vector<unsigned char> readAndPresent(Output& output) {
      const auto info = output.interopInfo();
      prepareImage(m_color, info.imageFd, info.allocationSize, info.extent, GL_RGBA8);
      prepareImage(m_depth, info.depthFd, info.depthAllocationSize, info.depthExtent, GL_R32F);
      prepareImage(m_overlay, info.overlayFd, info.overlayAllocationSize, info.overlayExtent, GL_RGBA8);
      require(m_color.texture || !m_depth.texture, "depth output has no matching color frame");
      std::array<GLuint, 3> textures { };
      const std::array<GLenum, 3> layouts { GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT };
      GLuint imageCount = 0;
      for (auto texture : { m_color.texture, m_depth.texture, m_overlay.texture }) {
        if (texture) textures[imageCount++] = texture;
      }
      const GLuint64 rendererValue = info.rendererDoneValue;
      m_semaphoreValue(m_semaphores[0], GL_TIMELINE_SEMAPHORE_VALUE_NV, &rendererValue);
      m_wait(m_semaphores[0], 0, nullptr, imageCount, textures.data(), layouts.data());
      checkGl();
      std::vector<unsigned char> pixels(m_color.extent.width * m_color.extent.height * 4);
      if (m_color.texture) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_color.framebuffer);
        glReadPixels(0, 0, m_color.extent.width, m_color.extent.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        checkGl();
      }
      m_depthPixels.clear();
      if (m_depth.texture) {
        m_depthPixels.resize(m_depth.extent.width * m_depth.extent.height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_depth.framebuffer);
        glReadPixels(0, 0, m_depth.extent.width, m_depth.extent.height, GL_RED, GL_FLOAT, m_depthPixels.data());
        checkGl();
      }
      m_overlayPixels.clear();
      if (m_overlay.texture) {
        m_overlayPixels.resize(m_overlay.extent.width * m_overlay.extent.height * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_overlay.framebuffer);
        glReadPixels(0, 0, m_overlay.extent.width, m_overlay.extent.height,
          GL_RGBA, GL_UNSIGNED_BYTE, m_overlayPixels.data());
        checkGl();
      }
      const GLuint64 clientValue = ++m_clientValue;
      m_semaphoreValue(m_semaphores[1], GL_TIMELINE_SEMAPHORE_VALUE_NV, &clientValue);
      m_signal(m_semaphores[1], 0, nullptr, imageCount, textures.data(), layouts.data());
      glFlush();
      checkGl();
      requireFailure([&] { output.present(clientValue - 1); });
      output.present(clientValue);
      requireFailure([&] { output.present(clientValue); });
      return pixels;
    }
  };
}
