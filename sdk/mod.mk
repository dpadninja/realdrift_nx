# sdk/mod.mk -- builds a mod's src/*.c into mod.so with devkitA64.
#
#   make -C mods/<id> -f $(realpath sdk/mod.mk)          # with devkitA64 installed
#   uv run python -m tools.mod_build mods/<id>           # the same in the devkitPro docker image
#
# The mod links no libraries (-nostdlib): libc calls are resolved by the port at load time and
# everything else comes through the rd_mod_api table (rd_mod.h). Warnings are errors.
# MIT (see LICENSE).
DEVKITPRO ?= /opt/devkitpro
SDK       := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
CC        := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-gcc
CFLAGS    := -std=gnu11 -O2 -fPIC -ffreestanding -fvisibility=hidden -fno-stack-protector \
             -Wall -Wextra -Werror -I$(SDK)
LDFLAGS   := -shared -nostdlib
SRCS      := $(wildcard src/*.c)

mod.so: $(SRCS) $(SDK)rd_mod.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)

clean:
	rm -f mod.so

.PHONY: clean
