# alsa-latency-test

This program is used to test the latency between a GPIO rising edge trigger and audio sample playback using the ALSA sound library. Most relevant for an embedded computer like a Raspberry Pi or Odroid.

Build and run:
```
cd build
cmake ../
make
./latency-test -f path/to/file.wav -g 249 -r 247 -d default -p 128
```

```
Usage: ./latency-test -f path/to/file.wav -g trigger GPIO [-r response GPIO] [-d ALSA device name] [-p period size]
  (-f) wav file must be 32-bits 48 kHz
  (-g) exported GPIO number to use as sound trigger
  (-r) exported GPIO number to use as trigger response
  (-d) ALSA device name
  (-p) period size is specified in frames
```

Clean with:
`make clean`

Enable kernel trace:
```
cd build
ccmake ../
FTRACE ON
g
make
```

Check `/sys/kernel/debug/tracing/trace` for printed message tags and call stack.
