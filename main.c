#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <poll.h>

#include "alsa_play.h"

#define GPIO_IN  249
#define GPIO_OUT 247

void print_instructions(void)
{
    printf("---------------------------------------------------------------\n");
    printf("Perform before running:\n");
    printf("\n");
    printf("sudo sh -c \"echo $GPIO_IN  > /sys/class/gpio/export\"\n");
    printf("sudo sh -c \"echo in        > /sys/class/gpio/gpio$GPIO_IN/direction\"\n");
    printf("sudo sh -c \"echo rising    > /sys/class/gpio/gpio$GPIO_IN/edge\"\n");
    printf("\n");
    printf("sudo sh -c \"echo $GPIO_OUT > /sys/class/gpio/export\"\n");
    printf("sudo sh -c \"echo out       > /sys/class/gpio/gpio$GPIO_OUT/direction\"\n");
    printf("sudo sh -c \"chmod 777      /sys/class/gpio/gpio$GPIO_OUT/value\"\n");
    printf("---------------------------------------------------------------\n");
}

int main(int argc, char *argv[])
{
    char str[256], buf[8];
    char *wav_file = NULL;
    char *alsa_device = NULL;
    struct pollfd pfd;
    int opt, gpio_trigger_fd, gpio_response_fd;
    int gpio_trigger = -1, gpio_response = -1;
    int period = -1;

    while ((opt = getopt(argc, argv, "f:g:r:d:p:")) != -1) {
        switch (opt) {
        case 'f':
            wav_file = strdup(optarg);
            printf("wav file %s\n", wav_file);
            break;
        case 'g':
            gpio_trigger = atoi(optarg);
            break;
        case 'r':
            gpio_response = atoi(optarg);
            break;
        case 'p':
            period = atoi(optarg);
            break;
        case 'd':
            alsa_device = strdup(optarg);
            break;
        case '?':
        /* fall though */
        default:
            fprintf(stderr, "unknown/invalid option: '-%c'\n", optopt);
            exit(-1);
        }
    }

    if ((wav_file == NULL) | (gpio_trigger == -1)) {
        printf("Usage: %s -f path/to/file.wav -g trigger GPIO [-r response "
               "GPIO] [-d ALSA device name] [-p period size]\n",
               argv[0]);
        printf("  (-f) wav file must be 32-bits 48 kHz\n");
        printf("  (-g) exported GPIO number to use as sound trigger\n");
        printf("  (-r) exported GPIO number to use as trigger response\n");
        printf("  (-d) ALSA device name\n");
        printf("  (-p) period size is specified in frames\n");
        exit(-1);
    }

    sprintf(str, "/sys/class/gpio/gpio%d/value", gpio_trigger);
    if ((gpio_trigger_fd = open(str, O_RDONLY)) < 0) {
        fprintf(stderr, "Failed, gpio %d not exported.\n", gpio_trigger);
        print_instructions();
        exit(-1);
    }

    if (gpio_response > 0) {
        sprintf(str, "/sys/class/gpio/gpio%d/value", gpio_response);
        if ((gpio_response_fd = open(str, O_WRONLY)) < 0) {
            fprintf(stderr, "Failed, gpio %d not exported.\n", gpio_response);
            print_instructions();
            exit(-1);
        }
    }

    if (alsa_init(alsa_device, wav_file, period) != 0) {
        printf("alsa init failed\n");
        exit(-1);
    }

    pfd.fd = gpio_trigger_fd;
    pfd.events = POLLPRI;

    /* consume any prior interrupt */
    lseek(gpio_trigger_fd, 0, SEEK_SET);
    read(gpio_trigger_fd, buf, sizeof buf);

    /* wait for interrupt */
    poll(&pfd, 1, -1);

    /* interrupt triggered: toggle response GPIO and play audio */
    printf("GPIO triggered\n");

    if (gpio_response > 0)
        write(gpio_response_fd, "1", 1);

    alsa_play();

    if (gpio_response > 0)
        write(gpio_response_fd, "0", 1);

    /* consume interrupt */
    lseek(gpio_trigger_fd, 0, SEEK_SET);
    read(gpio_trigger_fd, buf, sizeof buf);

    alsa_deinit();

    close(gpio_trigger_fd);

    if (gpio_response > 0)
        close(gpio_response_fd);

    return 0;
}
