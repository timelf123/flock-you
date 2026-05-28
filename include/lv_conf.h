#pragma once

// LVGL checks for LV_CONF_H to confirm this file was included.
#define LV_CONF_H 1

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1

#if defined(BOARD_HAS_PSRAM)
/* CONFIG_SPIRAM_USE_MALLOC routes libc malloc to PSRAM — no esp headers here (LVGL .S files) */
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING   LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_CLIB
#else
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_BUILTIN
#endif

#define LV_DEF_REFR_PERIOD 33
#define LV_DPI_DEF 130

#define LV_USE_LOG 0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_DEFAULT &lv_font_montserrat_12

#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_LIST 1
#define LV_USE_MSGBOX 1
#define LV_USE_TABVIEW 0
#define LV_USE_FLEX 1
#define LV_USE_GRID 0
#define LV_USE_SCROLLBAR 1

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
