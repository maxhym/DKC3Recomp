#ifndef DKC3_TELEMETRY_H
#define DKC3_TELEMETRY_H

/* Optional, read-only session telemetry for the DKCountry launcher.
 *
 * When DKCOUNTRY_TELEMETRY names a file, the host appends one JSON object per
 * line describing what happens in the game (protocol version 1, specified in
 * the DKCountry repository's docs/TELEMETRY_PROTOCOL.md). Without that
 * variable nothing is opened and nothing is written. The module only reads
 * WRAM after a completed frame; it never writes emulated state, so enabling
 * it cannot change game behaviour, timing or save data. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  kDkc3TelemetryProtocol = 1,
  kDkc3TelemetryFramesPerTick = 60,
};

/* WRAM fields the bridge observes. The names are those of the H4v0c21 DKC3
 * disassembly's ram.asm. */
typedef struct Dkc3TelemetrySnapshot {
  bool in_level;          /* the main-loop vector at $4E is the level loop */
  uint16_t level;         /* $00C0 level_number */
  uint16_t bananas_bcd;   /* $05D3 banana_count, BCD, wraps at 100 */
  /* $05D5 lives_count plus the lives gained ($18D4) less the lives lost
   * ($18D3) since the level began: the count the HUD animates towards. */
  int lives;
  uint16_t bear_coins;    /* $05C9 */
  uint16_t bonus_coins;   /* $05CB */
  uint16_t banana_birds;  /* $05CD */
  uint16_t dk_coins;      /* $05CF */
  uint16_t letters;       /* $05D7 kong_letter_flags */
  uint16_t animal;        /* $007E current_animal_type, 0 on foot */
  uint16_t kong_sprite;   /* $04F9 active_kong_sprite (pointer to its record) */
  uint16_t kong_x;        /* x_position ($12) of that record */
} Dkc3TelemetrySnapshot;

typedef void (*Dkc3TelemetrySink)(void *context, const char *line);

typedef struct Dkc3Telemetry {
  Dkc3TelemetrySink sink;
  void *context;
  Dkc3TelemetrySnapshot previous;
  bool have_previous;
  bool said_hello;
  uint32_t frames_since_tick;
  uint32_t move_px;
} Dkc3Telemetry;

void Dkc3TelemetryReadSnapshot(const uint8_t *wram, Dkc3TelemetrySnapshot *out);
int Dkc3TelemetryBcdToInt(uint16_t bcd);

void Dkc3TelemetryInit(Dkc3Telemetry *t, Dkc3TelemetrySink sink, void *context);
void Dkc3TelemetryHello(Dkc3Telemetry *t, const char *build, uint64_t frame);
void Dkc3TelemetryStep(Dkc3Telemetry *t, const Dkc3TelemetrySnapshot *now, uint64_t frame);
void Dkc3TelemetryStateLoaded(Dkc3Telemetry *t, uint64_t frame);
void Dkc3TelemetryBye(Dkc3Telemetry *t, uint64_t frame);

/* Process-wide file-backed instance used by the hosts. All calls are no-ops
 * unless DKCOUNTRY_TELEMETRY was set when the first frame ran. */
void Dkc3TelemetryHostFrame(const uint8_t *wram, uint64_t frame);
void Dkc3TelemetryHostStateLoaded(void);

/* Boot sound capture for the DKCountry launcher. When DKCOUNTRY_BOOT_AUDIO
 * names a file, the sound the host renders from power-on (logos, title
 * screen) is written there as a 16-bit PCM WAV, without its leading
 * silence, until gameplay starts or DKCOUNTRY_BOOT_AUDIO_SECONDS (default
 * 70) have been written. The sizes in the header stay zero until the file
 * is complete. Nothing from the ROM is written besides that sound, and the
 * file stays on the player's machine. */
typedef struct Dkc3BootAudio {
  FILE *file;
  uint32_t frames;      /* written so far */
  uint32_t max_frames;
  int channels, rate;
  bool heard;           /* past the leading silence */
  bool stop;            /* gameplay started */
  bool done;
} Dkc3BootAudio;

bool Dkc3BootAudioOpen(Dkc3BootAudio *b, const char *path, int channels,
                       int rate, double max_seconds);
void Dkc3BootAudioWrite(Dkc3BootAudio *b, const int16_t *samples, int frames);
/* Gameplay started: the capture ends, unless it has barely begun. */
void Dkc3BootAudioGameplay(Dkc3BootAudio *b);
bool Dkc3BootAudioDone(const Dkc3BootAudio *b);

/* Host hook: call with each block of game audio, before any music pack is
 * mixed in. Inert unless the launcher asked for a capture; during a capture
 * run the block is silenced after it is recorded, because that run is
 * hidden. */
void Dkc3TelemetryHostAudio(int16_t *samples, int frames, int channels,
                            int rate);


#ifdef __cplusplus
}
#endif

#endif
