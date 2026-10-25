#include "theme.h"
#include "layout.h"

typedef struct { uint16_t base[11], badge[6], dot[8]; } palette_t;

static const CODE palette_t PAL[TH_COUNT] = {
    /* CLASSIC: the original look (values in layout.h) */
    { { UI_C_BG, UI_C_BOX, UI_C_TEXT, UI_C_VIN, UI_C_VOLT, UI_C_DIM, UI_C_RUN, UI_C_WARN, UI_C_GOOD, UI_C_BAD, UI_C_INFO },
      UI_C_BADGE,
      UI_C_DOT },
    /* CALM: colour only where it means something */
    { { 0x0000, 0x10C4, 0xE73D, 0xE73D, 0xE73D, 0x6BB0, 0x88C3, 0xF547, 0x5E4F, 0xEA8A, 0x5DFB },
      { 0xCB6D, 0x6DAF, 0x647A, 0xD5EC, 0xBBD9, 0x65F7 },
      { 0x10C4, 0xD5EC, 0xEB2C, 0xF547, 0xF547, 0x5E4F, 0x647A, 0xCB79 } },
    /* AMBER: one hue on black, red for trouble */
    { { 0x0000, 0x28C0, 0xFD80, 0xFD80, 0xFD80, 0x92E0, 0x90E0, 0xFBC0, 0xFEEC, 0xF942, 0xCC65 },
      { 0xFD80, 0xDCA2, 0xFE8B, 0xBBC0, 0xFC65, 0xE5AC },
      { 0x28C0, 0xCC00, 0xF942, 0xFBC0, 0xFBC0, 0xFEEC, 0xAB63, 0xF942 } },
    /* HIGH CONTRAST: pure colours on black */
    { { 0x0000, 0x3186, 0xFFFF, 0xFFE0, 0xFFFF, 0xBDF7, 0xD800, 0xFE40, 0x07E0, 0xF800, 0x07FF },
      { 0xF800, 0x07E0, 0x033F, 0xFFE0, 0xF81F, 0x07FF },
      { 0x3186, 0xFFE0, 0xF800, 0xFE40, 0xFE40, 0x07E0, 0x033F, 0xF81F } }
};

static const char *const NAME[TH_COUNT] = { "CLASSIC", "CALM", "AMBER", "HIGH CONTRAST" };

static uint8_t XD active;

void th_set(uint8_t theme) { active = theme < TH_COUNT ? theme : TH_CLASSIC; }
uint8_t th_get(void) { return active; }
uint16_t th(uint8_t role) { return ((const CODE uint16_t *)&PAL[active])[role]; }
const char *th_name(uint8_t theme) { return NAME[theme < TH_COUNT ? theme : TH_CLASSIC]; }
