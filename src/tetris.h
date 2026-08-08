#pragma once
#include "config.h"

void tetris_start();
bool tetris_is_active();
void tetris_draw();
bool tetris_handle_touch(int x, int y);
bool tetris_update();
