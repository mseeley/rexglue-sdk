#pragma once
/**
 * @file        ui/surface_khr_display.h
 * @brief       A display presented to through VK_KHR_display
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/ui/surface.h>

namespace rex {
namespace ui {

// A display presented to directly, with no window system: the PS5's VideoOut.
// The size picks the display mode, so it should be one the display offers;
// the presenter takes the display's first mode otherwise.
class KhrDisplaySurface final : public Surface {
 public:
  KhrDisplaySurface(uint32_t width, uint32_t height) : width_(width), height_(height) {}

  TypeIndex GetType() const override { return kTypeIndex_KhrDisplay; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override {
    width_out = width_;
    height_out = height_;
    return true;
  }

 private:
  uint32_t width_;
  uint32_t height_;
};

}  // namespace ui
}  // namespace rex
