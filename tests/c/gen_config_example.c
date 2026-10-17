/* sources: source/game/config.c source/game/rd_settings.c */
/* Writes config.txt with every key at its default value (used by tests/test_config_example.py
 * to keep config.example.txt in sync with write_config()). MIT (see LICENSE). */
#include "config.h"

int main(int argc, char **argv) {
  return argc > 1 ? (write_config(argv[1]) != 0) : 2;
}
