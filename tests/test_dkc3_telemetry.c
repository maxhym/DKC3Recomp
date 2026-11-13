/* Synthetic checks of the DKCountry telemetry bridge. No ROM is used: WRAM
 * images and snapshots are built by hand. */
#include "dkc3_telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

typedef struct {
  char text[16384];
  int lines;
} Capture;

static void Sink(void *context, const char *line) {
  Capture *c = (Capture *)context;
  strncat(c->text, line, sizeof c->text - strlen(c->text) - 2);
  strncat(c->text, "\n", sizeof c->text - strlen(c->text) - 1);
  c->lines++;
}

static int Count(const Capture *c, const char *needle) {
  int n = 0;
  for (const char *p = c->text; (p = strstr(p, needle)) != NULL; ++p) ++n;
  return n;
}

static Dkc3TelemetrySnapshot Base(void) {
  Dkc3TelemetrySnapshot s;
  memset(&s, 0, sizeof s);
  s.in_level = true;
  s.level = 0x25;
  s.lives = 5;
  s.kong_sprite = 0x0878;
  s.kong_x = 1000;
  return s;
}

static void TestSnapshotReadsDocumentedAddresses(void) {
  static uint8_t wram[0x20000];
  memset(wram, 0, sizeof wram);
  wram[0x004E] = 0x76; wram[0x004F] = 0x80; wram[0x0050] = 0xB3;
  wram[0x00C0] = 0x25;
  wram[0x05D3] = 0x47;
  wram[0x05D5] = 5;
  wram[0x18D3] = 2;   /* lost in this level */
  wram[0x18D4] = 1;   /* gained in this level */
  wram[0x05C9] = 12;
  wram[0x05CB] = 3;
  wram[0x05CD] = 2;
  wram[0x05CF] = 1;
  wram[0x05D7] = 0x05;
  wram[0x007E] = 0x94; wram[0x007F] = 0x01;
  wram[0x04F9] = 0x78; wram[0x04FA] = 0x08;
  wram[0x0878 + 0x12] = 0x34; wram[0x0878 + 0x13] = 0x12;
  Dkc3TelemetrySnapshot s;
  Dkc3TelemetryReadSnapshot(wram, &s);
  CHECK(s.in_level);
  CHECK(s.level == 0x25);
  CHECK(s.bananas_bcd == 0x47);
  CHECK(s.lives == 4);
  CHECK(s.bear_coins == 12 && s.bonus_coins == 3 && s.banana_birds == 2 && s.dk_coins == 1);
  CHECK(s.letters == 0x05);
  CHECK(s.animal == 0x0194);
  CHECK(s.kong_sprite == 0x0878 && s.kong_x == 0x1234);
  /* Another main loop (map, menus) is not a level, and a null sprite
   * pointer is not dereferenced into a position. */
  wram[0x004E] = 0x00;
  wram[0x04F9] = 0; wram[0x04FA] = 0;
  Dkc3TelemetryReadSnapshot(wram, &s);
  CHECK(!s.in_level && s.kong_x == 0);
  CHECK(Dkc3TelemetryBcdToInt(0x0099) == 99);
  CHECK(Dkc3TelemetryBcdToInt(0x00A0) == -1);
}

static void TestEvents(void) {
  Capture c;
  memset(&c, 0, sizeof c);
  Dkc3Telemetry t;
  Dkc3TelemetryInit(&t, Sink, &c);
  Dkc3TelemetryHello(&t, "0.0.6\"x", 0);
  CHECK(strstr(c.text, "{\"e\":\"hello\",\"proto\":1,\"game\":\"dkc3\",\"build\":\"0.0.6x\",\"f\":0}") != NULL);

  Dkc3TelemetrySnapshot s = Base();
  uint64_t f = 1;
  Dkc3TelemetryStep(&t, &s, f++);     /* baseline: nothing reported */
  CHECK(c.lines == 1);

  s.bananas_bcd = 0x01;               /* one banana */
  s.kong_x += 3;
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(Count(&c, "\"e\":\"bananas\"") == 1 && strstr(c.text, "\"n\":1,\"level\":37") != NULL);

  s.bananas_bcd = 0x98;               /* a file load, not a pickup */
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(Count(&c, "\"e\":\"bananas\"") == 1);
  s.bananas_bcd = 0x03;               /* 98 -> 103 wraps with a bunch */
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(strstr(c.text, "\"e\":\"bananas\",\"f\":4,\"n\":5") != NULL);

  s.bear_coins = 1;
  s.bonus_coins = 1;
  s.dk_coins = 1;
  s.banana_birds = 1;
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(Count(&c, "\"kind\":\"bear_coin\",\"n\":1") == 1);
  CHECK(Count(&c, "\"kind\":\"bonus_coin\",\"n\":1") == 1);
  CHECK(Count(&c, "\"kind\":\"dk_coin\",\"n\":1") == 1);
  CHECK(Count(&c, "\"kind\":\"banana_bird\",\"n\":1") == 1);
  /* One pickup counts as one whether the counter is BCD (9 -> $10) or
   * binary (9 -> $0A). */
  s.bear_coins = 0x09;
  Dkc3TelemetryStep(&t, &s, f++);
  s.bear_coins = 0x10;
  Dkc3TelemetryStep(&t, &s, f++);
  s.bonus_coins = 0x09;
  Dkc3TelemetryStep(&t, &s, f++);
  s.bonus_coins = 0x0A;
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(Count(&c, "\"kind\":\"bear_coin\",\"n\":1") == 2);
  CHECK(Count(&c, "\"kind\":\"bonus_coin\",\"n\":1") == 2);

  s.letters = 0x01;
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(Count(&c, "\"e\":\"letters\"") == 1 && strstr(c.text, "\"mask\":1,\"level\":37") != NULL);

  s.animal = 0x0194;
  Dkc3TelemetryStep(&t, &s, f++);
  s.animal = 0;
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(Count(&c, "\"e\":\"animal\"") == 2 && strstr(c.text, "\"animal\":404") != NULL);

  s.lives = 4;                        /* a life lost */
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(Count(&c, "\"e\":\"death\"") == 1 && strstr(c.text, "\"cause\":\"life_lost\"") != NULL);
  CHECK(strstr(c.text, "\"e\":\"lives\"") != NULL && strstr(c.text, "\"lives\":4") != NULL);
  s.lives = 5;                        /* an extra life is not a death */
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(Count(&c, "\"e\":\"death\"") == 1 && Count(&c, "\"e\":\"lives\"") == 2);

  s.level = 0x26;
  s.kong_x = 200;                     /* a new level is not distance */
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(Count(&c, "\"e\":\"level\"") == 1 && strstr(c.text, "\"level\":38") != NULL);

  /* Outside a level the counters are someone else's: nothing is reported
   * on the way out or on the way back in. */
  const int before = c.lines;
  s.in_level = false;
  s.bananas_bcd = 0x50;
  s.lives = 9;
  s.bear_coins = 40;
  Dkc3TelemetryStep(&t, &s, f++);
  s.in_level = true;
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(c.lines == before);

  /* A loaded state is a new baseline. */
  Dkc3TelemetryStateLoaded(&t, f);
  s.bananas_bcd = 0x51;
  s.lives = 8;
  Dkc3TelemetryStep(&t, &s, f++);
  CHECK(Count(&c, "\"e\":\"state_load\"") == 1);
  CHECK(Count(&c, "\"e\":\"death\"") == 1);

  /* Ticks every 60 frames carry the distance; time outside a level is
   * flagged so that it does not count as play. */
  for (int i = 0; i < 70; ++i) {
    s.kong_x += 2;
    Dkc3TelemetryStep(&t, &s, f++);
  }
  CHECK(Count(&c, "\"e\":\"tick\"") >= 1 && strstr(c.text, "\"demo\":false") != NULL);
  CHECK(Count(&c, "\"e\":\"move\"") >= 1);
  s.in_level = false;
  for (int i = 0; i < 61; ++i) Dkc3TelemetryStep(&t, &s, f++);
  CHECK(strstr(c.text, "\"demo\":true") != NULL);
  Dkc3TelemetryBye(&t, f);
  CHECK(Count(&c, "\"e\":\"bye\"") == 1);
}

int main(void) {
  TestSnapshotReadsDocumentedAddresses();
  TestEvents();
  if (failures) {
    fprintf(stderr, "%d telemetry check(s) failed\n", failures);
    return 1;
  }
  puts("dkc3 telemetry tests passed");
  return 0;
}
