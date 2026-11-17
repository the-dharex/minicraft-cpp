#ifndef MINICRAFT_CORE_GFX_GEOMETRY_H_
#define MINICRAFT_CORE_GFX_GEOMETRY_H_

#include <algorithm>
#include <cstdint>

namespace minicraft {
namespace gfx {

struct Point {
  int32_t x = 0;
  int32_t y = 0;

  constexpr Point() = default;
  constexpr Point(int32_t x_val, int32_t y_val) : x(x_val), y(y_val) {}

  void Translate(int32_t xoff, int32_t yoff) {
    x += xoff;
    y += yoff;
  }

  constexpr bool operator==(const Point& other) const {
    return x == other.x && y == other.y;
  }
  constexpr bool operator!=(const Point& other) const {
    return !(*this == other);
  }
};

struct Dimension {
  int32_t width = 0;
  int32_t height = 0;

  constexpr Dimension() = default;
  constexpr Dimension(int32_t w, int32_t h) : width(w), height(h) {}
};

struct Insets {
  int32_t left = 0;
  int32_t top = 0;
  int32_t right = 0;
  int32_t bottom = 0;

  constexpr Insets() = default;
  explicit constexpr Insets(int32_t dist)
      : left(dist), top(dist), right(dist), bottom(dist) {}
  constexpr Insets(int32_t l, int32_t t, int32_t r, int32_t b)
      : left(l), top(t), right(r), bottom(b) {}
};

class Rectangle;

enum class RelPos : uint8_t {
  kTopLeft = 0,
  kTop = 1,
  kTopRight = 2,
  kLeft = 3,
  kCenter = 4,
  kRight = 5,
  kBottomLeft = 6,
  kBottom = 7,
  kBottomRight = 8,
};

inline constexpr int RelPosXIndex(RelPos rp) {
  return static_cast<int>(rp) % 3;
}
inline constexpr int RelPosYIndex(RelPos rp) {
  return static_cast<int>(rp) / 3;
}
inline constexpr RelPos GetRelPos(int x_index, int y_index) {
  int cx = std::clamp(x_index, 0, 2);
  int cy = std::clamp(y_index, 0, 2);
  return static_cast<RelPos>(cx + cy * 3);
}
inline constexpr RelPos GetOppositeRelPos(RelPos rp) {
  int nx = -(RelPosXIndex(rp) - 1) + 1;
  int ny = -(RelPosYIndex(rp) - 1) + 1;
  return GetRelPos(nx, ny);
}

class Rectangle {
 public:
  static constexpr int kCornerDims = 0;
  static constexpr int kCorners = 1;
  static constexpr int kCenterDims = 2;

  Rectangle() = default;
  Rectangle(int32_t x, int32_t y, int32_t x1, int32_t y1, int type = kCornerDims);
  Rectangle(Point p, Dimension d) : Rectangle(p.x, p.y, d.width, d.height) {}
  Rectangle(bool is_center, Point p, Dimension d)
      : Rectangle(p.x, p.y, d.width, d.height,
                  is_center ? kCenterDims : kCornerDims) {}

  int32_t Left() const { return x_; }
  int32_t Right() const { return x_ + w_; }
  int32_t Top() const { return y_; }
  int32_t Bottom() const { return y_ + h_; }
  int32_t Width() const { return w_; }
  int32_t Height() const { return h_; }

  Point Center() const { return {x_ + w_ / 2, y_ + h_ / 2}; }
  Dimension Size() const { return {w_, h_}; }
  Point Position(RelPos rel_pos) const;

  bool Intersects(const Rectangle& other) const;

  void SetPosition(Point p, RelPos rel_pos) { SetPosition(p.x, p.y, rel_pos); }
  void SetPosition(int32_t x, int32_t y, RelPos rel_pos);
  void Translate(int32_t xoff, int32_t yoff) {
    x_ += xoff;
    y_ += yoff;
  }
  void SetSize(Dimension d, RelPos anchor) { SetSize(d.width, d.height, anchor); }
  void SetSize(int32_t width, int32_t height, RelPos anchor);

 private:
  int32_t x_ = 0;
  int32_t y_ = 0;
  int32_t w_ = 0;
  int32_t h_ = 0;
};

Point PositionRect(RelPos rp, Dimension rect_size, Point anchor);
Rectangle PositionRect(RelPos rp, Dimension rect_size, Point anchor,
                       Rectangle* out_rect);
Point PositionRect(RelPos rp, Dimension rect_size, const Rectangle& container);
Rectangle PositionRect(RelPos rp, Dimension rect_size,
                       const Rectangle& container, Rectangle* out_rect);

}  // namespace gfx
}  // namespace minicraft

#endif  // MINICRAFT_CORE_GFX_GEOMETRY_H_
