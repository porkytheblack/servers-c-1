# ========= tiny-bank & calc demo – Makefile (explicit) =========
CC      ?= cc
CFLAGS  := -std=c17 -Wall -Wextra -D_POSIX_C_SOURCE=200809L

# ---- server binaries -------------------------------------------------
bank_server: bank_server.c
	$(CC) $(CFLAGS) $< -o $@ -lm          # fmod() needs -lm

calc_server: calc_server.c
	$(CC) $(CFLAGS) $< -o $@

# ---- CLI binaries ----------------------------------------------------
bank_cli: bank_client.c
	$(CC) $(CFLAGS) $< -o $@

calc_cli: calc_client.c
	$(CC) $(CFLAGS) $< -o $@

# ---- convenience targets --------------------------------------------
.PHONY: all bank client clean
all: bank_server calc_server bank_cli calc_cli
bank: bank_server calc_server
client: bank_cli calc_cli

clean:
	rm -f bank_server calc_server bank_cli calc_cli
