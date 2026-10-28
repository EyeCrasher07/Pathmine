// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <vector>

#include "rtx_lights.h"

namespace dxvk {
  class DxvkContext;

  class DomeLightCompositor {
  public:
    DomeLight compose(const Rc<DxvkContext>& ctx, const std::vector<const DomeLight*>& layers);

  private:
    void resize(const Rc<DxvkContext>& ctx, VkExtent3D extent);

    std::array<Rc<DxvkImageView>, 2> m_views;
    VkExtent3D m_extent { 0, 0, 1 };
  };
}
