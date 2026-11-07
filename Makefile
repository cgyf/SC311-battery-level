CC     ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
AR     ?= ar
PREFIX ?= /usr/local

all: libaula_battery.a aula-battery

aula_battery.o: aula_battery.c aula_battery.h
	$(CC) $(CFLAGS) -c $< -o $@

libaula_battery.a: aula_battery.o
	$(AR) rcs $@ $<

aula-battery: aula_battery_cli.c libaula_battery.a
	$(CC) $(CFLAGS) $< libaula_battery.a -o $@

clean:
	rm -f aula_battery.o libaula_battery.a aula-battery

# 普通用户安装：make install PREFIX=$HOME/.local
install: aula-battery
	install -Dm755 aula-battery $(DESTDIR)$(PREFIX)/bin/aula-battery

.PHONY: all clean install
