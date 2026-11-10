# Variables to provide (example values):
#
# TOOLCHAIN=/usr/local/bin/arm-none-eabi-
# TRACKER_DIR=../Tracker/
# TARGET_POSTFIX=mybuild
# DEBUG=1 (any value turns on)

# Target configuration
PRODUCT		?= ORIGINAL
TRACKER_DIR	?= ../../core
-include ../../version.mk
-include ../version.mk
TARGET_POSTFIX	?= $(DEFAULT_TARGET_POSTFIX)
BUILD_ID_STR	?= $(TARGET_POSTFIX)
TARGET		:= PolyendTracker
# Directory holding libTracker.a (the top level Makefile overrides it for SYNTH_OPEN=1)
CORE_BUILD_DIR	?= $(TRACKER_DIR)/build-MK66-$(PRODUCT)

# Build environment setup
.DEFAULT_GOAL := build

# Real flash image = text + data (.data load image sits after .text). 0xFF000 keeps the last 4 KB sector free.
FLASH_IMG_MAX	?= 1044480
# TRIM=1 (top Makefile, SYNTH_OPEN=1 builds only): the platform at -Os, -DFW_TRIM, linked against newlib-nano. The top
# Makefile also gives it its own BUILD_DIR (build-trim) so the objects never mix with a plain build.
TRIM		?=
# Extra linker flags (the SYNTH_OPEN build asks for a link map: -Xlinker -Map=...)
LINK_EXTRA	?=
# binutils 2.39 and newer warn about the RWX LOAD segment (the AUDIO_CORE_RAM code is meant to be there, the MK66 has no
# MMU or NX). Older ld (the pinned 10.3, binutils 2.35) rejects the flag, so ask ld whether it knows it. Image bytes are unchanged.
NO_WARN_RWX	:= $(shell $(TOOLCHAIN)ld --help 2>/dev/null | grep -q -e --no-warn-rwx-segments && echo -Xlinker --no-warn-rwx-segments)
BUILD_DIR	:= build
OBJ_DIR 	:= $(BUILD_DIR)/obj/
RM 			:= rm -rf

CPP_SRCS 	+= $(shell find ./3rdparty/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./source/ -name *.cpp)
# Development console image (-DENABLE_DEV_CONSOLE): the platform part comes from the private poly-devtools (../../devtools.mk). It takes
# the place in the link order and the object name it had as source/<name>.cpp, so the image is the one of a tree that still held it
DEVTOOLS_WANT := $(if $(filter -DENABLE_DEV_CONSOLE -DENABLE_DEV_CONSOLE=%,$(CUSTOM_DEFINES)),1)
include ../../devtools.mk
ifneq ($(DEVTOOLS_MK66_SRCS),)
ifeq ($(filter $(DEVTOOLS_MK66_AFTER),$(CPP_SRCS)),)
$(error poly-devtools: $(DEVTOOLS_MK66_AFTER) (DEVTOOLS_MK66_AFTER) is not a platform source)
endif
CPP_SRCS	:= $(patsubst $(DEVTOOLS_MK66_AFTER),$(DEVTOOLS_MK66_AFTER) $(addprefix ./source/,$(DEVTOOLS_MK66_SRCS)),$(CPP_SRCS))
endif
C_SRCS 	 	+= $(shell find ./3rdparty/ -name *.c)
# USB_AUDIO=1 (see DEFINES below): the platform side of the USB audio input
ifeq ($(USB_AUDIO),1)
CPP_SRCS	+= $(wildcard ./usbaudio/*.cpp)
endif
ASM_SRCS 	+= $(shell find ./3rdparty/ -name *.S)
# TRIM=1 only: word-wise memcpy / memset, newlib-nano's are byte loops (about 8 cycles per byte) and the audio interrupt uses them
# every block. TRIM_FASTMEM=0 leaves them out (A/B builds that isolate the nano link)
TRIM_FASTMEM ?= 1
ifeq ($(TRIM),1)
ifneq ($(TRIM_FASTMEM),0)
ASM_SRCS 	+= ./source/fastmem.S
endif
endif

_CPP_OBJS	= $(patsubst ./%.cpp, %.o, $(CPP_SRCS))
_C_OBJS		= $(patsubst ./%.c, %.o, $(C_SRCS))
_ASM_OBJS	= $(patsubst ./%.S, %.o, $(ASM_SRCS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_CPP_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_C_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_ASM_OBJS))

OBJ_DIRS	:= $(dir $(OBJS))
DEPS		:= $(OBJS:.o=.d)

NOW := $(shell date +"%Y%m%d_%H%M")

INCLUDES=-I./source -I./config -I./3rdparty/Audio -I./3rdparty/Encoder -I./3rdparty/Buttons -I./3rdparty/BlinkLed -I./3rdparty/radio -I./3rdparty/FastCRC -I./3rdparty/MIDI -I./3rdparty/I2C -I./3rdparty/Leds -I./3rdparty/Battery -I./3rdparty/teensy_core -I./3rdparty/SDRAM -I./3rdparty/Snooze -I./3rdparty/SPI -I./3rdparty/FT812 -I./3rdparty/EEPROM -I./3rdparty/SD -I./3rdparty/SD/drivers -I./3rdparty/SD/fatfs/source -I./3rdparty/SD/fatfs/source/fsl_sd_disk -I./3rdparty/SD/fatfs/source/fsl_ram_disk -I./3rdparty/SD/sdmmc/inc -I${TRACKER_DIR}/include
INCLUDES+=$(DEVTOOLS_INC)
LIBS=-lTracker -larm_cortexM4lf_math

DEFINES=-D__NEWLIB__ -D__MK66FX1M0__ -DF_CPU=180000000 -DUSB_MIDI -DTEENSYDUINO=142 -DARDUINO=10805 -DLAYOUT_US_ENGLISH -DCPU_MK66FX1M0VMD18 -DBUILD_ID=\"$(BUILD_ID_STR)\"
# CUSTOM_DEFINES reaches core/makefile too. -DENABLE_DEV_CONSOLE builds the development console image, which needs the private
# poly-devtools checkout (../../devtools.mk, core/include/devhooks.h).
# make does not track flags: wipe the core build dir of the SYNTH_TAG and platforms/mk66/build* when it changes.
DEFINES+=$(CUSTOM_DEFINES)

# USB_AUDIO=1: the image can also be a USB audio input for the computer (UAC 1.0, 44.1 kHz stereo, Tracker -> computer).
# Compiled in, switched at start: the descriptor set and the USB interrupt level are picked in usb_init from the EEPROM boot
# flag the Features page writes (Config > Features > USB audio, off by default). Off enumerates exactly like a build without it.
USB_AUDIO	?= 0
ifeq ($(USB_AUDIO),1)
DEFINES+=-DFEATURE_USB_AUDIO
INCLUDES+=-I./usbaudio
endif

COMPILER_OPTIONS=-fno-common -g -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -fstack-usage -specs=nano.specs -Wall -Wextra -c -ffunction-sections -fdata-sections -ffreestanding -fno-builtin 
# Crash-class warnings, warn only, see core/makefile (CRASH_WARN= switches them off)
CRASH_WARN ?= -Wstack-usage=512 -Wvla
COMPILER_OPTIONS+=$(CRASH_WARN)
ifdef DEBUG
COMPILER_OPTIONS+=-Og -DDEBUG
else
COMPILER_OPTIONS+=-O2 -DNDEBUG
endif

ifeq ($(TRIM),1)
# The compile flags above already say -specs=nano.specs, the stock link line does not (full newlib and libstdc++).
# -u _printf_float keeps %f (about 50 UI call sites), scanf float is not used
COMPILER_OPTIONS+=-Os
DEFINES+=-DFW_TRIM
TRIM_LINK	:= -specs=nano.specs -Xlinker -u -Xlinker _printf_float
endif

# SDRAM_FILE_POOLS=1 (top Makefile, SYNTH_OPEN=1 builds): the FatFs FIL pool and the SdFile pool stay in the SDRAM
# as before tracker-ce 5b6b0f2, which moved them (about 19.6 KB) into the internal RAM the plugin slots and heap need.
# The stock build leaves it off and stays byte-identical to upstream.
ifeq ($(SDRAM_FILE_POOLS),1)
DEFINES+=-DFW_SDRAM_FILE_POOLS
endif

ifeq ($(PRODUCT), MINI)
DEFINES+=-DTRACKER_MINI
else ifeq ($(PRODUCT), ORIGINAL)
else 
$(error Invalid PRODUCT value $(PRODUCT))
endif

CPP_OPTIONS=-std=gnu++17 -felide-constructors -includecstdint -fno-rtti -fno-exceptions 
CC_OPTIONS=

# Build rules

$(OBJ_DIR)/%.o: %.cpp
	$(TOOLCHAIN)c++ -MMD -MP $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

ifneq ($(DEVTOOLS_MK66_SRCS),)
$(addprefix $(OBJ_DIR)/source/,$(DEVTOOLS_MK66_SRCS:.cpp=.o)): $(OBJ_DIR)/source/%.o: $(DEVTOOLS_MK66_DIR)/%.cpp
	$(TOOLCHAIN)c++ -MMD -MP $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"
endif

$(OBJ_DIR)/%.o: %.c
	$(TOOLCHAIN)gcc -MMD -MP $(DEFINES) $(INCLUDES) $(CC_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/%.o: %.S
	$(TOOLCHAIN)gcc -c -x assembler-with-cpp $(DEFINES) $(INCLUDES) -g -o "$@" "$<"
 
$(TARGET): $(OBJS)
	$(TOOLCHAIN)c++ -L ./3rdparty/teensy_core -L ./3rdparty/Audio -L $(CORE_BUILD_DIR) -Xlinker --defsym=__rtc_localtime=0 -Xlinker -print-memory-usage -Xlinker --gc-sections -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -T mk66fx1m0.ld $(LINK_EXTRA) $(NO_WARN_RWX) $(TRIM_LINK) -o "$(BUILD_DIR)/$(TARGET).axf" $(OBJS) $(LIBS) 
	$(MAKE) --no-print-directory post-build
	
# Main rules

build:
	$(MAKE) --no-print-directory pre-build
	$(MAKE) --no-print-directory main-build

main-build: $(TARGET)

pre-build:
	$(foreach dir,$(OBJ_DIRS),mkdir -p $(dir);)

post-build: 
	cp "$(BUILD_DIR)/$(TARGET).axf" "$(BUILD_DIR)/$(TARGET)_$(NOW)_$(TARGET_POSTFIX).axf"
	$(TOOLCHAIN)objcopy --gap-fill=0xFF -v -O ihex "$(BUILD_DIR)/$(TARGET).axf" "$(BUILD_DIR)/$(TARGET)_$(TARGET_POSTFIX).ptf" 
	$(TOOLCHAIN)objdump -S -C "$(BUILD_DIR)/$(TARGET).axf" > "$(BUILD_DIR)/$(TARGET)_$(NOW)_$(TARGET_POSTFIX).lss" 
	$(TOOLCHAIN)size "$(BUILD_DIR)/$(TARGET).axf"
	@$(TOOLCHAIN)size "$(BUILD_DIR)/$(TARGET).axf" | awk -v max=$(FLASH_IMG_MAX) 'NR==2 { img=$$1+$$2; printf "image (text+data) %d of %d B\n", img, max; if (img > max) { printf "ERROR: image (text+data) %d B exceeds FLASH_IMG_MAX %d B (%d over)\n", img, max, img-max; exit 1 } }'

clean:
	-$(RM) $(BUILD_DIR)

-include $(DEPS)
	
.PHONY: build
