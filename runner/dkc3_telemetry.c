#include "dkc3_telemetry.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  /* $80:84BA installs $B3:8076 in the main-loop vector for a level. */
  kMainLoopVector = 0x004E,
  kMainLoopLevel = 0xB38076,
  /* Larger single-frame jumps are file loads or resets, not pickups. A
   * banana bunch is the largest ordinary award. */
  kMaxBananaStep = 10,
  kMaxCoinStep = 10,
  /* Larger horizontal jumps are room changes or teleports. */
  kMaxMoveStep = 48,
  kSpriteXOffset = 0x12,
};

static uint16_t Read16(const uint8_t *wram, uint32_t address) {
  return (uint16_t)(wram[address] | ((uint16_t)wram[address + 1] << 8));
}

void Dkc3TelemetryReadSnapshot(const uint8_t *wram, Dkc3TelemetrySnapshot *out) {
  memset(out, 0, sizeof *out);
  const uint32_t loop = (uint32_t)Read16(wram, kMainLoopVector) |
                        ((uint32_t)wram[kMainLoopVector + 2] << 16);
  out->in_level = loop == kMainLoopLevel;
  out->level = Read16(wram, 0x00C0);
  out->bananas_bcd = Read16(wram, 0x05D3);
  out->lives = (int)Read16(wram, 0x05D5) + (int)wram[0x18D4] - (int)wram[0x18D3];
  out->bear_coins = Read16(wram, 0x05C9);
  out->bonus_coins = Read16(wram, 0x05CB);
  out->banana_birds = Read16(wram, 0x05CD);
  out->dk_coins = Read16(wram, 0x05CF);
  out->letters = Read16(wram, 0x05D7);
  out->animal = Read16(wram, 0x007E);
  out->kong_sprite = Read16(wram, 0x04F9);
  /* Sprite records live in low WRAM; anything else is not a record yet. */
  if (out->kong_sprite >= 0x0100 && out->kong_sprite < 0x1F00)
    out->kong_x = Read16(wram, (uint32_t)out->kong_sprite + kSpriteXOffset);
}

int Dkc3TelemetryBcdToInt(uint16_t bcd) {
  int value = 0;
  int scale = 1;
  for (int digit = 0; digit < 4; ++digit) {
    int nibble = (bcd >> (digit * 4)) & 15;
    if (nibble > 9) return -1;
    value += nibble * scale;
    scale *= 10;
  }
  return value;
}

static void Emit(Dkc3Telemetry *t, const char *line) {
  if (t->sink) t->sink(t->context, line);
}

void Dkc3TelemetryInit(Dkc3Telemetry *t, Dkc3TelemetrySink sink, void *context) {
  memset(t, 0, sizeof *t);
  t->sink = sink;
  t->context = context;
}

void Dkc3TelemetryHello(Dkc3Telemetry *t, const char *build, uint64_t frame) {
  char line[160];
  char safe[48];
  size_t n = 0;
  for (const char *p = build ? build : ""; *p && n + 1 < sizeof safe; ++p)
    if (*p != '"' && *p != '\\' && (unsigned char)*p >= 0x20) safe[n++] = *p;
  safe[n] = 0;
  snprintf(line, sizeof line,
           "{\"e\":\"hello\",\"proto\":%d,\"game\":\"dkc3\",\"build\":\"%s\",\"f\":%" PRIu64 "}",
           kDkc3TelemetryProtocol, safe, frame);
  Emit(t, line);
  t->said_hello = true;
}

static int BananaDelta(uint16_t before_bcd, uint16_t after_bcd) {
  int before = Dkc3TelemetryBcdToInt(before_bcd);
  int after = Dkc3TelemetryBcdToInt(after_bcd);
  if (before < 0 || after < 0 || before > 99 || after > 99 || before == after) return 0;
  int delta = after > before ? after - before : after + 100 - before;
  return delta <= kMaxBananaStep ? delta : 0;
}

/* The disassembly does not say whether the coin counters are binary or BCD.
 * Read as decimal when both values are valid BCD and as binary otherwise:
 * a single pickup then counts as one in either encoding. */
static int CoinDelta(uint16_t before, uint16_t after) {
  if (after <= before) return 0;
  const int before_bcd = Dkc3TelemetryBcdToInt(before);
  const int after_bcd = Dkc3TelemetryBcdToInt(after);
  const int delta = before_bcd >= 0 && after_bcd >= 0 ? after_bcd - before_bcd
                                                      : (int)after - (int)before;
  return delta > 0 && delta <= kMaxCoinStep ? delta : 0;
}

static void EmitCoins(Dkc3Telemetry *t, const char *kind, uint16_t before, uint16_t after,
                      uint64_t frame) {
  const int n = CoinDelta(before, after);
  if (n <= 0) return;
  char line[128];
  snprintf(line, sizeof line, "{\"e\":\"coins\",\"f\":%" PRIu64 ",\"kind\":\"%s\",\"n\":%d}",
           frame, kind, n);
  Emit(t, line);
}

void Dkc3TelemetryStep(Dkc3Telemetry *t, const Dkc3TelemetrySnapshot *now, uint64_t frame) {
  char line[192];
  const Dkc3TelemetrySnapshot *was = &t->previous;

  /* Only frames inside a level, with a previous level frame to compare to:
   * menus, the map and file loads rewrite the same counters. */
  if (t->have_previous && now->in_level && was->in_level) {
    if (now->level != was->level) {
      snprintf(line, sizeof line, "{\"e\":\"level\",\"f\":%" PRIu64 ",\"level\":%u}", frame,
               (unsigned)now->level);
      Emit(t, line);
    }
    const int bananas = BananaDelta(was->bananas_bcd, now->bananas_bcd);
    if (bananas > 0) {
      snprintf(line, sizeof line, "{\"e\":\"bananas\",\"f\":%" PRIu64 ",\"n\":%d,\"level\":%u}",
               frame, bananas, (unsigned)now->level);
      Emit(t, line);
    }
    EmitCoins(t, "bear_coin", was->bear_coins, now->bear_coins, frame);
    EmitCoins(t, "bonus_coin", was->bonus_coins, now->bonus_coins, frame);
    EmitCoins(t, "banana_bird", was->banana_birds, now->banana_birds, frame);
    EmitCoins(t, "dk_coin", was->dk_coins, now->dk_coins, frame);
    if (now->lives != was->lives && now->level == was->level) {
      if (now->lives == was->lives - 1) {
        snprintf(line, sizeof line,
                 "{\"e\":\"death\",\"f\":%" PRIu64 ",\"level\":%u,\"x\":%u,\"cause\":\"life_lost\"}",
                 frame, (unsigned)now->level, (unsigned)was->kong_x);
        Emit(t, line);
      }
      if (now->lives >= 0 && now->lives <= 99) {
        snprintf(line, sizeof line, "{\"e\":\"lives\",\"f\":%" PRIu64 ",\"lives\":%d}", frame,
                 now->lives);
        Emit(t, line);
      }
    }
    if (now->letters != was->letters && now->level == was->level) {
      snprintf(line, sizeof line, "{\"e\":\"letters\",\"f\":%" PRIu64 ",\"mask\":%u,\"level\":%u}",
               frame, (unsigned)now->letters, (unsigned)now->level);
      Emit(t, line);
    }
    if (now->animal != was->animal) {
      snprintf(line, sizeof line, "{\"e\":\"animal\",\"f\":%" PRIu64 ",\"animal\":%u}", frame,
               (unsigned)now->animal);
      Emit(t, line);
    }
    if (now->kong_sprite == was->kong_sprite && now->level == was->level) {
      int dx = (int)now->kong_x - (int)was->kong_x;
      if (dx < 0) dx = -dx;
      if (dx <= kMaxMoveStep) t->move_px += (uint32_t)dx;
    }
  }

  if (++t->frames_since_tick >= kDkc3TelemetryFramesPerTick) {
    t->frames_since_tick = 0;
    /* DKC3 has no attract-mode gameplay; time outside a level is reported
     * as demo so that only play counts. */
    snprintf(line, sizeof line, "{\"e\":\"tick\",\"f\":%" PRIu64 ",\"demo\":%s,\"level\":%u}",
             frame, now->in_level ? "false" : "true", (unsigned)now->level);
    Emit(t, line);
    if (t->move_px) {
      snprintf(line, sizeof line, "{\"e\":\"move\",\"f\":%" PRIu64 ",\"dx\":%u}", frame,
               (unsigned)t->move_px);
      Emit(t, line);
      t->move_px = 0;
    }
  }
  t->previous = *now;
  t->have_previous = true;
}

void Dkc3TelemetryStateLoaded(Dkc3Telemetry *t, uint64_t frame) {
  char line[64];
  snprintf(line, sizeof line, "{\"e\":\"state_load\",\"f\":%" PRIu64 "}", frame);
  Emit(t, line);
  /* The loaded state is a new baseline: nothing that differs from the
   * pre-load frame was earned by playing. */
  t->have_previous = false;
}

void Dkc3TelemetryBye(Dkc3Telemetry *t, uint64_t frame) {
  char line[64];
  if (t->move_px) {
    snprintf(line, sizeof line, "{\"e\":\"move\",\"f\":%" PRIu64 ",\"dx\":%u}", frame,
             (unsigned)t->move_px);
    Emit(t, line);
    t->move_px = 0;
  }
  snprintf(line, sizeof line, "{\"e\":\"bye\",\"f\":%" PRIu64 "}", frame);
  Emit(t, line);
}

/* ---- File-backed host instance ---- */

#ifndef DKC3_TELEMETRY_BUILD
#define DKC3_TELEMETRY_BUILD "0.0.6"
#endif

static Dkc3Telemetry s_host;
static FILE *s_file;
static bool s_checked;
static uint64_t s_last_frame;
static unsigned s_lines_since_flush;

static void FileSink(void *context, const char *line) {
  FILE *file = (FILE *)context;
  fputs(line, file);
  fputc('\n', file);
  if (++s_lines_since_flush >= 16) {
    fflush(file);
    s_lines_since_flush = 0;
  }
}

static void HostClose(void) {
  if (!s_file) return;
  Dkc3TelemetryBye(&s_host, s_last_frame);
  fclose(s_file);
  s_file = NULL;
}

static bool HostReady(void) {
  if (s_checked) return s_file != NULL;
  s_checked = true;
  const char *path = getenv("DKCOUNTRY_TELEMETRY");
  if (!path || !path[0]) return false;
  s_file = fopen(path, "ab");
  if (!s_file) {
    fprintf(stderr, "telemetry: cannot open %s; continuing without it\n", path);
    return false;
  }
  Dkc3TelemetryInit(&s_host, FileSink, s_file);
  atexit(HostClose);
  return true;
}

void Dkc3TelemetryHostFrame(const uint8_t *wram, uint64_t frame) {
  if (!wram || !HostReady()) return;
  if (!s_host.said_hello) Dkc3TelemetryHello(&s_host, DKC3_TELEMETRY_BUILD, frame);
  Dkc3TelemetrySnapshot snapshot;
  Dkc3TelemetryReadSnapshot(wram, &snapshot);
  Dkc3TelemetryStep(&s_host, &snapshot, frame);
  s_last_frame = frame;
  if (s_host.frames_since_tick == 0) {
    fflush(s_file);
    s_lines_since_flush = 0;
  }
}

void Dkc3TelemetryHostStateLoaded(void) {
  if (s_file) Dkc3TelemetryStateLoaded(&s_host, s_last_frame);
}
