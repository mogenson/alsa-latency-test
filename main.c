#include <stdio.h>
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
    printf("sudo sh -c \"echo 249     > /sys/class/gpio/export\"\n");
    printf("sudo sh -c \"echo in      > /sys/class/gpio/gpio249/direction\"\n");
    printf("sudo sh -c \"echo rising  > /sys/class/gpio/gpio249/edge\"\n");
    printf("\n");
    printf("sudo sh -c \"echo 247     > /sys/class/gpio/export\"\n");
    printf("sudo sh -c \"echo out     > /sys/class/gpio/gpio247/direction\"\n");
    printf("sudo sh -c \"chmod 777      /sys/class/gpio/gpio247/value\"\n");
    printf("---------------------------------------------------------------\n");
}

int main(int argc, char *argv[])
{
    char str[256];
    struct pollfd pfd;
    int gpio_trigger_fd, gpio_response_fd, gpio_trigger, gpio_response;
    char buf[8];

    /*
     *if (argc < 2) {
     *    printf("Usage: %s path/to/file.wav [-p period size]\n", argv[0]);
     *    printf("  wav file must be 32-bits 48 kHz\n");
     *    printf("  period size is specified in frames\n");
     *    exit(-1);
     *}
     */

    gpio_trigger = GPIO_IN;
    gpio_response = GPIO_OUT;

    sprintf(str, "/sys/class/gpio/gpio%d/value", gpio_trigger);

    if ((gpio_trigger_fd = open(str, O_RDONLY)) < 0) {
        fprintf(stderr, "Failed, gpio %d not exported.\n", gpio_trigger);
        print_instructions();
        exit(-1);
    }

    sprintf(str, "/sys/class/gpio/gpio%d/value", gpio_response);

    if ((gpio_response_fd = open(str, O_WRONLY)) < 0) {
        fprintf(stderr, "Failed, gpio %d not exported.\n", gpio_response);
        print_instructions();
        exit(-1);
    }

    if (alsa_init() !=0) {
        printf("alsa init failed\n");
        exit(1);
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
    write(gpio_response_fd, "1", 1);
    alsa_play();
    write(gpio_response_fd, "0", 1);

    /* consume interrupt */
    lseek(gpio_trigger_fd, 0, SEEK_SET);
    read(gpio_trigger_fd, buf, sizeof buf);

    alsa_deinit();

    close(gpio_trigger_fd);
    close(gpio_response_fd);

    return 0;
}
