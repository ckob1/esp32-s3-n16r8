#pragma once
#include "config.h"

#define MUSIC_MAX_FILES 12

void music_init();
int music_get_count();
String music_get_name(int idx);
String music_get_path(int idx);
bool music_is_cloud(int idx);
int music_get_cloud_count();
String music_get_cloud_name(int idx);
String music_get_cloud_url(int idx);
bool music_play(int idx);
void music_stop();
bool music_is_playing();
String music_get_current_name();
