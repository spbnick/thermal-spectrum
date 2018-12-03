Thermal Spectrum
================
Thermal Spectrum is a thermal printer module interface for ZX Spectrum
computer, using the ZX Printer interface.

[![Development setup][development_setup_thumb]][development_setup]

[development_setup_thumb]: development_setup.thumb.jpg
[development_setup]: development_setup.jpg

Building the firmware
---------------------
First you'll need the compiler. If you're using a Debian-based Linux you can
get it by installing `gcc-arm-none-eabi` package. Then you'll need the
[libstammer][libstammer] library. Build it first, then export environment
variables pointing to it:

    export CFLAGS=-ILIBSTAMMER_DIR LDFLAGS=-LLIBSTAMMER_DIR

`LIBSTAMMER_DIR` above is the directory where libstammer build is located.

After that you can build the firmware using `make`.
