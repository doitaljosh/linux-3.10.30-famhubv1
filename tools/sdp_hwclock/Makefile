# Makefile for sdp_hwclock tool

CC = $(CROSS_COMPILE)gcc
DEFINES=-D_GNU_SOURCE

CFLAGS = -std=c99 -g -Wall -Wextra $(DEFINES)

all: sdp_hwclock
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) sdp_hwclock *~
