/**************************************************************************************************/
/*  Manette Game-hub : 2e ESP32 qui pilote le jeu par ESP-NOW (radio directe ESP a ESP, sans box) */
/*                                                                                                */
/*  Boutons vers la masse (rappel interne) :                                                     */
/*    GAUCHE GPIO 32, DROITE GPIO 33, FEU GPIO 25 (2e bouton FEU possible sur GPIO 26)            */
/*  Joystick analogique en option (module type KY-023) : axe X sur GPIO 34, VCC en 3,3 V.         */
/*    Activer JOYSTICK ci-dessous. Le bouton du joystick (SW) peut aller sur GPIO 26 (FEU).       */
/*  LED de la carte (GPIO 2) : clignote = recherche du jeu, allumee = reliee au jeu.             */
/*                                                                                                */
/*  Recherche : le jeu emet une balise 2 fois par seconde. La manette ecoute chaque canal wifi    */
/*  (1 a 13) jusqu'a la trouver, puis envoie l'etat des boutons a chaque changement et toutes    */
/*  les 100 ms. Sans reponse du jeu pendant 3 s, elle recommence la recherche.                   */
/*  Plusieurs manettes peuvent etre allumees : elles commandent le meme vaisseau.                */
/**************************************************************************************************/
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define BTN_LEFT 32
#define BTN_RIGHT 33
#define BTN_FIRE 25
#define BTN_FIRE2 26
#define LED_PIN 2
#define JOYSTICK 0      // 1 : joystick analogique branche (axe X sur JOY_X_PIN)
#define JOY_X_PIN 34
#define MANETTE_VERSION "1.00"

// paquets ESP-NOW (identiques cote jeu)
#define GH_MAGIC 0x42554847UL  // "GHUB"
enum : uint8_t { GH_BEACON = 1, GH_KEYS = 2 };
struct __attribute__((packed)) GhPacket {
  uint32_t magic;
  uint8_t type;
  uint8_t keys;  // 1 gauche, 2 droite, 4 feu
};

const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t gameMac[6];
volatile bool found = false;          // balise du jeu recue
volatile uint32_t lastAck = 0;        // dernier envoi confirme par le jeu
bool linked = false;
uint8_t channel = 1;
int16_t joyMid = 2048;

void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(GhPacket)) return;
  const GhPacket *p = (const GhPacket *)data;
  if (p->magic != GH_MAGIC || p->type != GH_BEACON) return;
  if (!found) { memcpy(gameMac, mac, 6); found = true; }
}
void onSent(const uint8_t *mac, esp_now_send_status_t st) {
  if (st == ESP_NOW_SEND_SUCCESS && memcmp(mac, gameMac, 6) == 0) lastAck = millis();
}

void setChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

uint8_t readKeys() {
  uint8_t k = 0;
  if (digitalRead(BTN_LEFT) == LOW) k |= 1;
  if (digitalRead(BTN_RIGHT) == LOW) k |= 2;
  if (digitalRead(BTN_FIRE) == LOW || digitalRead(BTN_FIRE2) == LOW) k |= 4;
#if JOYSTICK
  int x = analogRead(JOY_X_PIN) - joyMid;
  if (x < -700) k |= 1;
  if (x > 700) k |= 2;
#endif
  return k;
}

void search() {  // ecoute chaque canal jusqu'a entendre la balise du jeu
  found = false;
  linked = false;
  if (esp_now_is_peer_exist(gameMac)) esp_now_del_peer(gameMac);
  Serial.println("Recherche du jeu...");
  for (uint32_t n = 0; !found; n++) {
    setChannel(channel);
    uint32_t t0 = millis();
    while (!found && millis() - t0 < 600) {
      digitalWrite(LED_PIN, (millis() / 150) & 1);
      delay(10);
    }
    if (!found) channel = channel % 13 + 1;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, gameMac, 6);
  peer.channel = 0;  // canal actuel
  peer.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peer);
  linked = true;
  lastAck = millis();
  digitalWrite(LED_PIN, HIGH);
  Serial.printf("Jeu trouve sur le canal %u (%02X:%02X:%02X:%02X:%02X:%02X)\n", channel, gameMac[0], gameMac[1], gameMac[2],
                gameMac[3], gameMac[4], gameMac[5]);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Manette Game-hub v" MANETTE_VERSION);
  for (int p : { BTN_LEFT, BTN_RIGHT, BTN_FIRE, BTN_FIRE2 }) pinMode(p, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
#if JOYSTICK
  joyMid = analogRead(JOY_X_PIN);  // position de repos
#endif
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_now_init();
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  search();
}

void loop() {
  static uint8_t last = 0xFF;
  static uint32_t lastSend = 0;
  uint8_t k = readKeys();
  if (k != last || millis() - lastSend >= 100) {
    GhPacket p = { GH_MAGIC, GH_KEYS, k };
    esp_now_send(gameMac, (const uint8_t *)&p, sizeof(p));
    last = k;
    lastSend = millis();
  }
  if (millis() - lastAck > 3000) search();  // jeu eteint ou change de canal
  delay(5);
}
