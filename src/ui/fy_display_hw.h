#pragma once

#include <lvgl.h>

bool fyDisplayHwInit();
void fyDisplayHwTick();
lv_display_t* fyDisplayGetLvglDisplay();
