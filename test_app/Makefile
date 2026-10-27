#-------------------------------------------------------------------------------
# Wii U Homebrew Test App Makefile (NotifyTest)
#-------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

WUT_ROOT ?= $(DEVKITPRO)/wut
CXX ?= powerpc-eabi-g++
CXXFLAGS := -std=c++20 -O2 -Wall -DESPRESSO -mcpu=750 -meabi -mhard-float \
            -D__WIIU__ -D__WUT__ \
            -I$(WUT_ROOT)/include -I$(WUT_ROOT)/usr/include

LDFLAGS := -specs=$(WUT_ROOT)/share/wut.specs \
           -L$(WUT_ROOT)/usr/lib -lnotifications \
           -L$(WUT_ROOT)/lib -lwut

TARGET := NotifyTest

.PHONY: all clean

all: $(TARGET).wuhb

$(TARGET).elf: main.cpp
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

$(TARGET).rpx: $(TARGET).elf
	elf2rpl $< $@

$(TARGET).wuhb: $(TARGET).rpx
	wuhbtool $< $@ --name "Community Notification Test" --short-name "NotifyTest" --author "programmerBober"

clean:
	rm -f $(TARGET).elf $(TARGET).rpx $(TARGET).wuhb main.o
