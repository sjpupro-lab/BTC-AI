/*
 * canvasos_types.h — CanvasOS Core Cell Type
 * ============================================
 * OS 코어 Cell 정의. 30+ 파일이 의존하므로 절대 수정 불가.
 *
 * Cell RGBA 구조:
 *   A (uint32_t) : Attribute / Alpha 채널
 *   G (uint16_t) : Grade / Green 채널 (빈도)
 *   R (uint8_t)  : Red 채널 (바이트 값)
 *   B (uint8_t)  : Blue 채널 (깊이/depth)
 *
 * 총 8바이트 / 셀 — 캐시 친화적 정렬.
 */

#ifndef CANVASOS_TYPES_H
#define CANVASOS_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ── 기본 셀 타입 ─────────────────────────────── */
typedef struct {
    uint32_t A;   /* Attribute: 의미 클러스터 + 메타 */
    uint16_t G;   /* Grade: 빈도/밝기 (0~65535)     */
    uint8_t  R;   /* Red: 바이트 값 (0~255)          */
    uint8_t  B;   /* Blue: 깊이 (depth)             */
} CanvasCell;

/* ── 캔버스 기본 크기 ──────────────────────────── */
#define CANVAS_DEFAULT_WIDTH   4096
#define CANVAS_DEFAULT_HEIGHT  4096

/* ── RGBA 채널 접근 매크로 ─────────────────────── */
#define CELL_GET_A(cell)   ((cell).A)
#define CELL_GET_G(cell)   ((cell).G)
#define CELL_GET_R(cell)   ((cell).R)
#define CELL_GET_B(cell)   ((cell).B)

#define CELL_SET_A(cell, v) ((cell).A = (uint32_t)(v))
#define CELL_SET_G(cell, v) ((cell).G = (uint16_t)(v))
#define CELL_SET_R(cell, v) ((cell).R = (uint8_t)(v))
#define CELL_SET_B(cell, v) ((cell).B = (uint8_t)(v))

/* ── 셀 초기화 ────────────────────────────────── */
#define CELL_ZERO  ((CanvasCell){0, 0, 0, 0})

/* ── 좌표 유틸리티 ─────────────────────────────── */
static inline uint32_t canvas_xy_to_idx(uint16_t x, uint16_t y, uint16_t width) {
    return (uint32_t)y * width + x;
}

static inline void canvas_idx_to_xy(uint32_t idx, uint16_t width,
                                     uint16_t *x, uint16_t *y) {
    *y = (uint16_t)(idx / width);
    *x = (uint16_t)(idx % width);
}

#endif /* CANVASOS_TYPES_H */
