#ifndef ALSA_PLAY_H
#define ALSA_PLAY_H

int alsa_play(void);
int alsa_init(char *device_name, char *wav_file, int period);
void alsa_deinit(void);

#endif /* ALSA_PLAY_H */
