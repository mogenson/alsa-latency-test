CC=gcc
CFLAGS=-Wall
LIBS=-lasound
DEFINES=#-DFTRACE #Uncomment me to trace kernel calls during execution

all: latency-test

latency-test:
	$(CC) $(CFLAGS) main.c alsa_play.c ftrace.c -o latency-test $(LIBS) $(DEFINES)

clean:
	@rm latency-test || true
