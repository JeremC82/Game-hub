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
struct KBall { float x, y, vx, vy; bool on; };  // BRIQUES : balle
struct KCap { float x, y; uint8_t type; bool on; };  // BRIQUES : capsule (0 large, 1 lente, 2 trois balles, 3 vie, 4 tir, 5 reduite, 6 porte)
struct DEn { float x, y; int8_t c, r, dx, dy; uint8_t type, stun, ghost; int16_t t; bool on; };  // TAUPE : ver (0) ou chauve-souris (1)
struct DRock { float y, vy; int8_t c; uint8_t wob, state; bool on; };                         // TAUPE : rocher (0 pose, 1 tremble, 2 tombe)
struct AEn { float x, y, vx, vy; int16_t t; uint8_t type, pat, var; int8_t hp, flash; bool on, carrier; };  // ESCADRILLE : chasseur (0), bombardier (1), vedette (2), destroyer (3), porte-avions (4) ; var : couleur
struct ZMob { float x, y, hd; int16_t hp; uint16_t t, walk; uint8_t type, state, flash, cool, dodgeT, burst, atk, strafeT, guard; int8_t dx; bool on; };  // BUNKER 3D : drone (0), sentinelle (1), colosse (2), colonne (3), baril (4)
struct ZShot { float x, y, vx, vy; uint16_t t; bool on; };  // BUNKER 3D : plasma ennemi
struct ZItem { uint8_t x, y, type; bool on; };  // BUNKER 3D : soin, munitions, armure, carte rouge, arme lourde
struct VCar { float x, y, vy, tx; uint8_t type, col; int8_t lane; bool on, passed; };  // TURBO ROUTE : 0 voiture, 1 camion, 2 cone, 3 huile, 4 piece, 5 turbo
struct VDeco { float y; int16_t x; uint8_t type; bool on; };  // TURBO ROUTE : decor du bord de la route
struct RuObj { float x; int8_t type, h; bool on; };  // RENARD : 0 rocher, 1 buissons (h : nombre), 2 tronc, 3 chauve-souris (h : hauteur), 4 gemme, 5 etoile
