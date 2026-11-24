#include "core/gfx/geometry.h"

namespace minicraft {
namespace gfx {

Rectangle::Rectangle(int32_t x, int32_t y, int32_t x1, int32_t y1, int type) {
  if (type < 0 || type > 2) type = 0;
  if (type != kCenterDims) {
    x_ = x;
    y_ = y;
  } else {
    x_ = x - x1 / 2;
    y_ = y - y1 / 2;
  }
  if (type != kCorners) {
    w_ = x1;
    h_ = y1;
  } else {
    w_ = x1 - x;
    h_ = y1 - y;
  }
}

Point Rectangle::Position(RelPos rel_pos) const {
  return {x_ + RelPosXIndex(rel_pos) * w_ / 2,
          y_ + RelPosYIndex(rel_pos) * h_ / 2};
}

bool Rectangle::Intersects(const Rectangle& other) const {
  return !(Left() > other.Right() || other.Left() > Right() ||
           Bottom() < other.Top() || other.Bottom() < Top());
}

void Rectangle::SetPosition(int32_t x, int32_t y, RelPos rel_pos) {
  x_ = x - RelPosXIndex(rel_pos) * w_ / 2;
  y_ = y - RelPosYIndex(rel_pos) * h_ / 2;
}

void Rectangle::SetSize(int32_t width, int32_t height, RelPos anchor) {
  Point p = Position(anchor);
  w_ = width;
  h_ = height;
  SetPosition(p, anchor);
}

Point PositionRect(RelPos rp, Dimension rect_size, Point anchor) {
  Rectangle bounds(anchor.x, anchor.y, rect_size.width * 2,
                   rect_size.height * 2, Rectangle::kCenterDims);
  return PositionRect(rp, rect_size, bounds);
}

Rectangle PositionRect(RelPos rp, Dimension rect_size, Point anchor,
                       Rectangle* out_rect) {
  Rectangle temp;
  Rectangle* dst = out_rect ? out_rect : &temp;
  Point pos = PositionRect(rp, rect_size, anchor);
  dst->SetSize(rect_size, RelPos::kTopLeft);
  dst->SetPosition(pos, RelPos::kTopLeft);
  return *dst;
}

Point PositionRect(RelPos rp, Dimension rect_size, const Rectangle& container) {
  Point tl = container.Center();
  tl.x += ((RelPosXIndex(rp) - 1) * container.Width() / 2) -
          (RelPosXIndex(rp) * rect_size.width / 2);
  tl.y += ((RelPosYIndex(rp) - 1) * container.Height() / 2) -
          (RelPosYIndex(rp) * rect_size.height / 2);
  return tl;
}

Rectangle PositionRect(RelPos rp, Dimension rect_size,
                       const Rectangle& container, Rectangle* out_rect) {
  Rectangle temp;
  Rectangle* dst = out_rect ? out_rect : &temp;
  Point pos = PositionRect(rp, rect_size, container);
  dst->SetSize(rect_size, RelPos::kTopLeft);
  dst->SetPosition(pos, RelPos::kTopLeft);
  return *dst;
}

}  // namespace gfx
}  // namespace minicraft
