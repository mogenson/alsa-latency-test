#include <errno.h> /* error codes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* malloc() */
#include <unistd.h> /* read() */

#include <alsa/asoundlib.h>

#include "ftrace.h"

/*
 * PCM device name.
 * First number is sound card ID, second number is device ID
 *
 * Odroid Hi-Fi shield is card 1, device 0
 * "hw:1,0" device does not support S24_3LE
 */
#define PCM_DEVICE "hw:1,0"

/*
 * 1 frame = 1 analog sample * number of channels
 *         = 4 bytes * 2
 *         = 8 bytes
 */
#define SAMPLE_SIZE 4
#define NUM_CHANNELS 2
#define FRAME_SIZE (SAMPLE_SIZE * NUM_CHANNELS)

/*
 * ALSA specifies period and buffer size in frames. Sample format, playback
 * rate, and period size determine interrupt period and latency.
 *
 * data rate = channels * audio sample * rate
 *           = 2 * 4 bytes/sample * 48000 samples/second
 *           = 384000 bytes/second
 *
 * Let's choose a period size of 128 frames.
 *
 * interrupt period = period size * frame size / data rate
 *                  = 128 frames * ( 8 bytes/frame ) / ( 384000 bytes/second )
 *                  = 2.66 ms
 *
 * A good rule of thumb is to have a buffer size >= 2 * period size
 */

/* looks like period size has to be a power of 2 */
#define PERIOD_SIZE 128
#define BUFFER_SIZE (3 * PERIOD_SIZE)

/*
 * The Microsoft WAV PCM soundfile format has a 44 bytes header.
 * We'll just skip past this header with a hardcoded offset when accessing PCM
 * data.
 */
#define WAV_HEADER 44

/* pcm handle */
static snd_pcm_t *pcm_handle;

/* audio sample data */
long wav_size;
char *wav_buffer;

int pcm_set_sw_params(snd_pcm_t *handle, snd_pcm_sw_params_t *params, int period)
{
    int ret;
    snd_pcm_uframes_t period_size, threshold;

    /* get a fully populated configuration space */
    ret = snd_pcm_sw_params_current(handle, params);
    if (ret) {
        fprintf(stderr, "Cannot init sw params struct: %s\n",
                snd_strerror(ret));
        return ret;
    }

    /* set the software wakeup period in frames */
    period_size = PERIOD_SIZE; // NOTE: this may need to be a power of 2
    ret = snd_pcm_sw_params_set_avail_min(handle, params, period_size);
    if (ret) {
        fprintf(stderr, "Cannot set min available frames: %s\n",
                snd_strerror(ret));
        return ret;
    }

    /* set start threshold. make this equal to period size to avoid underrun
     * during first playback */
    threshold = (period < 0) ? PERIOD_SIZE : period; // needs to be at least 1 frame
    ret = snd_pcm_sw_params_set_start_threshold(handle, params, threshold);
    if (ret) {
        fprintf(stderr, "Couldn't set start threshold: %s\n",
                snd_strerror(ret));
        return ret;
    }

    /* write sw params to PCM device*/
    ret = snd_pcm_sw_params(handle, params);
    if (ret) {
        fprintf(stderr, "Couldn't write sw params to PCM device: %s\n",
                snd_strerror(ret));
        return ret;
    }

    /* get start threshold */
    ret = snd_pcm_sw_params_get_start_threshold(params, &threshold);
    if (ret) {
        fprintf(stderr, "Couldn't get start threshold: %s\n",
                snd_strerror(ret));
        return ret;
    }
    printf("Start threshold is %lu frames\n", threshold);

    return 0;
}

/*
 * ALSA will calculate the minimum recommended buffer and period size in
 * frames. Let's check these values to make sure we're operating at the
 * threshold.
 */
int pcm_print_hw_params(snd_pcm_hw_params_t *params)
{
    int ret, dir;
    snd_pcm_uframes_t buffer_frames, period_frames;

    /* get min buffer and period size */

    ret = snd_pcm_hw_params_get_period_size_min(params, &period_frames, &dir);
    if (ret) {
        fprintf(stderr, "Failed to get min period size: %s\n",
                snd_strerror(ret));
        return ret;
    }
    printf("Minimum period size = %lu frames, %lu bytes\n", period_frames,
           period_frames * FRAME_SIZE);

    ret = snd_pcm_hw_params_get_buffer_size_min(params, &buffer_frames);
    if (ret) {
        fprintf(stderr, "Failed to get min buffer size: %s\n",
                snd_strerror(ret));
        return ret;
    }
    printf("Minimum buffer size = %lu frames, %lu bytes\n", buffer_frames,
           buffer_frames * FRAME_SIZE);

    /* get max buffer and period size */

    ret = snd_pcm_hw_params_get_period_size_max(params, &period_frames, &dir);
    if (ret) {
        fprintf(stderr, "Failed to get max period size: %s\n",
                snd_strerror(ret));
        return ret;
    }
    printf("Maximum period size = %lu frames, %lu bytes\n", period_frames,
           period_frames * FRAME_SIZE);

    ret = snd_pcm_hw_params_get_buffer_size_max(params, &buffer_frames);
    if (ret) {
        fprintf(stderr, "Failed to get max buffer size: %s\n",
                snd_strerror(ret));
        return ret;
    }
    printf("Maximum buffer size = %lu frames, %lu bytes\n", buffer_frames,
           buffer_frames * FRAME_SIZE);

    return 0;
}

void pcm_print_state(snd_pcm_t *handle)
{
    printf("PCM device state: %s\n", snd_pcm_state_name(snd_pcm_state(handle)));
}

int pcm_set_hw_params(snd_pcm_t *handle, snd_pcm_hw_params_t *params, int period)
{
    int ret;
    unsigned int requested_rate, set_rate;
    snd_pcm_uframes_t buffer_size, period_size;

    /* Get a fully populated configuration space */
    ret = snd_pcm_hw_params_any(pcm_handle, params);
    if (ret) {
        fprintf(stderr, "Couldn't initialize hw params: %s\n",
                snd_strerror(ret));
        return ret;
    }

    /*
     * Hardware parameters must be set in this order:
     * 1. access
     * 2. format
     * 3. subformat
     * 4. channels
     * 5. rate
     * 6. min period
     * 7. max buffer
     * 8. tick time <- where's this set?
     */

    /* set interleaved write format */
    ret = snd_pcm_hw_params_set_access(handle, params,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (ret) {
        fprintf(stderr, "Access type not available: %s\n", snd_strerror(ret));
        return ret;
    }

    /* set sample format: Signed 32 bit Little Endian */
    ret = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S32_LE);
    if (ret) {
        fprintf(stderr, "Sample format not available: %s\n", snd_strerror(ret));
        return ret;
    }

    /* set subformat */
    ret =
        snd_pcm_hw_params_set_subformat(handle, params, SND_PCM_SUBFORMAT_STD);
    if (ret) {
        fprintf(stderr, "Subformat not available: %s\n", snd_strerror(ret));
        return ret;
    }

    /* disable hardware resampling */
    ret = snd_pcm_hw_params_set_rate_resample(handle, params, 0);
    if (ret) {
        fprintf(stderr, "Resampling setup failed: %s\n", snd_strerror(ret));
        return ret;
    }

    /* set channel count */
    ret = snd_pcm_hw_params_set_channels(handle, params, NUM_CHANNELS);
    if (ret) {
        fprintf(stderr, "Channel count setup failed: %s\n", snd_strerror(ret));
        return ret;
    }

    /* set playback rate */
    requested_rate = set_rate = 48000; // 48 kHz
    ret = snd_pcm_hw_params_set_rate_near(handle, params, &set_rate, 0);
    if (ret) {
        fprintf(stderr, "Rate no available for playback: %s\n",
                snd_strerror(ret));
        return ret;
    }
    if (set_rate != requested_rate) {
        fprintf(stderr,
                "Set rate ( %u Hz ) does not match requested rate ( %u Hz )\n",
                set_rate, requested_rate);
        return -EINVAL;
    }

    pcm_print_hw_params(params);

    /* set period size */
    period_size = (period < 0) ? PERIOD_SIZE : period;
    ret = snd_pcm_hw_params_set_period_size(handle, params, period_size, 0);
    if (ret) {
        fprintf(stderr, "Period size not available: %s\n", snd_strerror(ret));
        return ret;
    }
    printf("Period size set to %lu frames\n", period_size);

    /* set buffer size */
    buffer_size = (period < 0) ? BUFFER_SIZE : (3*period);
    ret = snd_pcm_hw_params_set_buffer_size(handle, params, buffer_size);
    if (ret) {
        fprintf(stderr, "Buffer size not available: %s\n", snd_strerror(ret));
        return ret;
    }
    printf("Buffer size set to %lu frames\n", buffer_size);

    /* write hardware parameters to PCM device */
    ret = snd_pcm_hw_params(handle, params);
    if (ret) {
        fprintf(stderr,
                "Unable to write hardware parameters to PCM device: %s\n",
                snd_strerror(ret));
        return ret;
    }

    /* snd_pcm_hw_params() should call snd_pcm_prepare() */
    pcm_print_state(handle);

    /* get period size */
    int dir;
    ret = snd_pcm_hw_params_get_period_size(params, &period_size, &dir);
    if (ret) {
        fprintf(stderr, "Can't get period size\n");
    }
    printf("Actual period size = %lu\n", period_size);

    /* get period time */
    unsigned int period_time;
    ret = snd_pcm_hw_params_get_period_time(params, &period_time, &dir);
    if (ret) {
        fprintf(stderr, "Can't get period time\n");
    }
    printf("Actual period time = % f\n", period_time / 1000.0);

    /* get buffer size */
    ret = snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
    if (ret) {
        fprintf(stderr, "Can't get buffer size\n");
    }
    printf("Actual buffer size = %lu\n", buffer_size);

    return 0;
}

static void show_available_sample_formats(snd_pcm_t *handle,
                                          snd_pcm_hw_params_t *params)
{
    snd_pcm_format_t format;

    printf("Available formats:\n");
    for (format = 0; format <= SND_PCM_FORMAT_LAST; format++) {
        // printf("%d\n", format);
        if (snd_pcm_hw_params_test_format(handle, params, format) == 0)
            printf("- %s\n", snd_pcm_format_name(format));
    }
    printf("\n");
}

int alsa_play(void)
{
    int ret;
    static int index = WAV_HEADER; // skip header and start at PCM data
    snd_pcm_sframes_t frames_requested, frames_written;

    while (1) {
        ret = snd_pcm_wait(pcm_handle, 1000);
        if (ret == 0) {
            fprintf(stderr, "PCM timeout occurred\n");
            return -1;
        } else if (ret < 0) {
            fprintf(stderr, "PCM device error: %s\n", snd_strerror(ret));
            return ret;
        }

        frames_requested = snd_pcm_avail_update(pcm_handle);
        if (frames_requested < 0) {
            fprintf(stderr, "PCM error requesting frames: %s\n",
                    snd_strerror(ret));
            return ret;
        }

        /* deliver data one period at a time */
        frames_requested =
            (frames_requested > PERIOD_SIZE) ? PERIOD_SIZE : frames_requested;

        /* don't overrun wav file buffer */
        frames_requested = (frames_requested * FRAME_SIZE + index > wav_size)
                               ? (wav_size - index) / FRAME_SIZE
                               : frames_requested;

#ifdef FTRACE
        trace_start("START_TRACE\n");
#endif
        frames_written =
            snd_pcm_writei(pcm_handle, &wav_buffer[index], frames_requested);
#ifdef FTRACE
        trace_stop("STOP_TRACE\n");
#endif

        if (frames_written == -EAGAIN) {
            continue;
        }

        if (frames_written == -EPIPE) { // underrun
            fprintf(stderr, "PCM write error: Underrun event\n");
            frames_written = snd_pcm_prepare(pcm_handle);
            if (frames_written < 0) {
                fprintf(stderr,
                        "Can't recover from underrun, prepare failed: %s\n",
                        snd_strerror(frames_written));
                return frames_written;
            }
        } else if (frames_written == -ESTRPIPE) {
            fprintf(stderr, "PCM write error: Stream is suspended\n");
            while ((frames_written = snd_pcm_resume(pcm_handle)) == -EAGAIN) {
                sleep(1); // wait until the suspend flag is released
            }
            if (frames_written < 0) {
                frames_written = snd_pcm_prepare(pcm_handle);
                if (frames_written < 0) {
                    fprintf(stderr,
                            "Can' recover from suspend, prepare failed: %s\n",
                            snd_strerror(frames_written));
                    return frames_written;
                }
            }
        }

        /* update current index */
        index += frames_requested * FRAME_SIZE;

        if (index >= wav_size) {
            printf("End of file\n");
            return 0;
        }

#ifdef FTRACE
        exit(-1); // bail after one write
#endif
    }

    return -1; // we should never get here
}

int read_wav_file(char *wav_file)
{
    int ret;
    FILE *wav_fd;

    /* open wave file and get size */
    wav_fd = fopen(wav_file, "rb");
    fseek(wav_fd, 0, SEEK_END);
    wav_size = ftell(wav_fd);
    rewind(wav_fd);

    /* allocate a buffer for audio samples and fill it */
    wav_buffer = malloc(wav_size);
    if (!wav_buffer) {
        ret = -ENOMEM;
        fprintf(stderr, "Cannot allocate audio sample buffer: %s\n",
                strerror(ret));
        return ret;
    }
    if (fread(wav_buffer, 1, wav_size, wav_fd) != wav_size) {
        ret = -1;
        fprintf(stderr, "Error reading wave file\n");
        free(wav_buffer);
        return ret;
    }
    fclose(wav_fd);

    return 0;
}

int alsa_init(char *device_name, char *wav_file, int period)
{
    /* return values / errors */
    int ret;

    /* ALSA variables */
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_sw_params_t *sw_params;

    ret = read_wav_file(wav_file);
    if (ret) {
        return ret;
    }

    /* open PCM playback device */
    if (device_name != NULL) {
        ret =
            snd_pcm_open(&pcm_handle, device_name, SND_PCM_STREAM_PLAYBACK, 0);
    } else {
        ret = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
    }
    if (ret) {
        fprintf(stderr, "PCM device open error: %s\n", snd_strerror(ret));
        return ret;
    }

    pcm_print_state(pcm_handle);

    /* configure hardware parameters */
    ret = snd_pcm_hw_params_malloc(&hw_params);
    if (ret) {
        fprintf(stderr, "Cannot allocate hw param struct: %s\n",
                snd_strerror(ret));
        return ret;
    }
    ret = pcm_set_hw_params(pcm_handle, hw_params, period);
    if (ret) {
        return ret;
    }
    show_available_sample_formats(pcm_handle, hw_params);
    snd_pcm_hw_params_free(hw_params);

    /* configure software parameters */
    ret = snd_pcm_sw_params_malloc(&sw_params);
    if (ret) {
        fprintf(stderr, "Cannot allocate sw param struct: %s\n",
                snd_strerror(ret));
        return ret;
    }
    ret = pcm_set_sw_params(pcm_handle, sw_params, period);
    if (ret) {
        return ret;
    }
    snd_pcm_sw_params_free(sw_params);

    pcm_print_state(pcm_handle);

    /* print some hardware info */
    printf("PCM device name: %s\n", snd_pcm_name(pcm_handle));

#ifdef FTRACE
    return trace_init();
#endif

    return 0;
}

void alsa_deinit(void)
{
    snd_pcm_drain(pcm_handle);
    snd_pcm_close(pcm_handle);
    free(wav_buffer);
}
