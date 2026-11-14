// Types partages (dans un .h pour que l'IDE Arduino les connaisse avant ses prototypes automatiques)
#pragma once
#include <Arduino.h>

struct Note { uint16_t hz, ms; };  // note d'un jingle (hz 0 = silence)

struct Voice { float ph, f, df, vol, dv; uint32_t left; bool noise; uint32_t id; };  // voix du synthetiseur (id : son lance)
struct HiScore { uint32_t score; uint8_t stage, diff; char name[4]; };  // tableau des meilleurs scores (nom : 3 lettres)
struct HiScoreV1 { uint32_t score; uint8_t stage, diff; };              // ancien format (V2.02 a V2.19), repris au chargement
struct LMob { float x, y; int8_t c, r, dx, dy; uint8_t state; int16_t t; bool fr; };  // LABYRINTHE : scarabee ou creature (state 0 chasse, 2 attente, 3 sortie)
struct OEn { float a, r, a0, sa; int16_t t; int8_t dir; uint8_t type, state; bool on, carrier, shot; };  // ORBITE : ennemi (0 attente, 1 spirale, 2 anneau, 3 pique, 4 retour)
struct OShot { float a, r; bool on; };  // ORBITE : tir, tir ennemi, etoile ou capsule (coordonnees polaires)
