#ifndef MINICRAFT_CORE_ENTITY_DIRECTION_H_
#define MINICRAFT_CORE_ENTITY_DIRECTION_H_

#include <cstdint>

namespace minicraft::entity {

enum class Direction : uint8_t {
  kNone = 0,
  kDown = 1,
  kUp = 2,
  kLeft = 3,
  kRight = 4,
};

inline int32_t DirX(Direction dir) {
  switch (dir) {
    case Direction::kLeft:
      return -1;
    case Direction::kRight:
      return 1;
    default:
      return 0;
  }
}

inline int32_t DirY(Direction dir) {
  switch (dir) {
    case Direction::kDown:
      return 1;
    case Direction::kUp:
      return -1;
    default:
      return 0;
  }
}

inline Direction GetDirection(int32_t xd, int32_t yd) {
  if (xd == 0 && yd == 0) return Direction::kNone;
  if (xd != 0 && yd != 0) {
    if ( (xd < 0 ? -xd : xd) >= (yd < 0 ? -yd : yd) ) {
      return xd < 0 ? Direction::kLeft : Direction::kRight;
    }
    return yd < 0 ? Direction::kUp : Direction::kDown;
  }
  if (xd != 0) return xd < 0 ? Direction::kLeft : Direction::kRight;
  return yd < 0 ? Direction::kUp : Direction::kDown;
}

inline Direction DirectionFromIndex(int32_t idx) {
  switch (idx) {
    case 1:
      return Direction::kDown;
    case 2:
      return Direction::kUp;
    case 3:
      return Direction::kLeft;
    case 4:
      return Direction::kRight;
    default:
      return Direction::kNone;
  }
}

}  // namespace minicraft::entity

#endif  // MINICRAFT_CORE_ENTITY_DIRECTION_H_
