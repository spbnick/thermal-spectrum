CCPFX=arm-none-eabi-

TARGET_CFLAGS = -mcpu=cortex-m3 -mthumb
COMMON_CFLAGS = $(TARGET_CFLAGS) -Wall -Wextra -Werror -g3
LIBS = -lstammer

# Program name
NAME = ts

all: $(NAME).bin

# Module names in order of symbol resolution
MODS = \
    circular_buf \
    serial \
    $(NAME)

# Object files
OBJS = $(addsuffix .o, $(MODS))
# Make dependency rules
DEPS = $(OBJS:.o=.d)
-include $(DEPS)

.PHONY: clean

%.o: %.c
	$(CCPFX)gcc $(COMMON_CFLAGS) $(CFLAGS) -c -o $@ $<
	$(CCPFX)gcc $(COMMON_CFLAGS) $(CFLAGS) -MM $< > $*.d

%.bin: %.elf
	$(CCPFX)objcopy -O binary $< $@

$(NAME).elf: $(OBJS) $(LDSCRIPTS)
	$(CCPFX)ld $(LDFLAGS) -T libstammer.ld -o $@ $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS)
	rm -f $(DEPS)
	rm -f $(NAME).elf
	rm -f $(NAME).bin
