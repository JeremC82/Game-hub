// Types partages (dans un .h pour que l'IDE Arduino les connaisse avant ses prototypes automatiques)
#pragma once
#include <Arduino.h>

struct Note { uint16_t hz, ms; };  // note d'un jingle (hz 0 = silence)
struct Voice { float ph, f, df, vol, dv; uint32_t left; bool noise; };  // voix du synthetiseur
struct HiScore { uint32_t score; uint8_t stage, diff; };  // tableau des meilleurs scores
