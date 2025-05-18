CC ?= cc
CFLAGS := -std=c17 -Wall -Wextra -D_POSIX_C_SOURCE=200809L

udb_calc_server: udb_calc_server.c
	$(CC) $(CFLAGS) $< -o $@ -lm

udp_calc_client: udp_calc_cli.c
	$(CC) $(CFLAGS) $< -o $@


bank_server: bank_udp_server.c
	$(CC) $(CFLAGS) $< -o $@ -lm

bank_server_client: bank_udp_client.c
	$(CC) $(CFLAGS) $< -o $@


.PHONY: all server client clean
all: udb_calc_server udp_calc_client bank_server bank_server_client
server: udb_calc_server
client: udp_calc_client
clean:
	rm -f udb_calc_server udp_calc_client bank_server bank_server_client