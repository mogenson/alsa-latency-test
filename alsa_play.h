#pragma once

int alsa_play(void);
int alsa_init(char *device_name, char *wav_file, int period);
void alsa_deinit(void);
