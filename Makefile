CCPFX=arm-none-eabi-

TARGET_CFLAGS = -mcpu=cortex-m3 -mthumb
COMMON_CFLAGS = $(TARGET_CFLAGS) -Wall -Wextra -Werror -g3
LIBS = -lstammer

# Program name
NAME = printer

# Module names in order of symbol resolution
MODS = \
    $(NAME)

# Object files
OBJS = $(addsuffix .o, $(MODS))
# Make dependency rules
DEPS = $(OBJS:.o=.d)

.PHONY: clean

all: $(NAME).bin

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
