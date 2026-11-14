#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ps4.h"
#include "ps4_int.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "stack/gap_api.h"
#include "stack/bt_types.h"
#include "stack/l2c_api.h"
#include "osi/allocator.h"

#define PS4_TAG "PS4_L2CAP"

/********************************************************************************/
/*              L O C A L    F U N C T I O N     P R O T O T Y P E S            */
/********************************************************************************/

static void ps4_l2cap_init_service(const char *name, uint16_t psm, uint8_t security_id);
static void ps4_l2cap_deinit_service(const char *name, uint16_t psm);
/* Game hub (modification, GPL-3) : quelques secondes apres la connexion, le lien passait en veille
   Bluetooth (sniff) et la manette n'arrivait plus qu'a ~1 rapport/s. On interdit la veille sur ce lien
   et on le force en mode actif des le premier rapport recu (fonctions internes de Bluedroid). */
typedef struct { uint16_t max, min, attempt, timeout; uint8_t mode; } gh_pm_pwr_md_t;  // = tBTM_PM_PWR_MD
extern uint8_t BTM_SetPowerMode(uint8_t pm_id, BD_ADDR remote_bda, gh_pm_pwr_md_t *p_mode);
extern uint8_t BTM_SetLinkPolicy(BD_ADDR remote_bda, uint16_t *settings);
static BD_ADDR ps4PeerAddr;
static volatile int ps4PmDone = 0;
volatile int ps4DbgPmPolicy = -1, ps4DbgPmMode = -1;  // codes retour (0 = ok), affiches par le jeu
static void ps4ForceActive(void) {
  uint16_t policy = 0x0001;  // changement de role seul : sniff, hold et park interdits
  gh_pm_pwr_md_t md = { 0, 0, 0, 0, 0x00 };  // BTM_PM_MD_ACTIVE
  ps4DbgPmPolicy = BTM_SetLinkPolicy(ps4PeerAddr, &policy);
  ps4DbgPmMode = BTM_SetPowerMode(0x80 /* BTM_PM_SET_ONLY_ID */, ps4PeerAddr, &md);
}
static void ps4_l2cap_connect_ind_cback(BD_ADDR bd_addr, uint16_t l2cap_cid, uint16_t psm, uint8_t l2cap_id);
static void ps4_l2cap_connect_cfm_cback(uint16_t l2cap_cid, uint16_t result);
static void ps4_l2cap_config_ind_cback(uint16_t l2cap_cid, tL2CAP_CFG_INFO *p_cfg);
static void ps4_l2cap_config_cfm_cback(uint16_t l2cap_cid, tL2CAP_CFG_INFO *p_cfg);
static void ps4_l2cap_disconnect_ind_cback(uint16_t l2cap_cid, bool ack_needed);
static void ps4_l2cap_disconnect_cfm_cback(uint16_t l2cap_cid, uint16_t result);
static void ps4_l2cap_data_ind_cback(uint16_t l2cap_cid, BT_HDR *p_msg);
static void ps4_l2cap_congest_cback(uint16_t cid, bool congested);

/********************************************************************************/
/*                         L O C A L    V A R I A B L E S                       */
/********************************************************************************/

static const tL2CAP_APPL_INFO dyn_info = {
  ps4_l2cap_connect_ind_cback,
  ps4_l2cap_connect_cfm_cback,
  NULL,
  ps4_l2cap_config_ind_cback,
  ps4_l2cap_config_cfm_cback,
  ps4_l2cap_disconnect_ind_cback,
  ps4_l2cap_disconnect_cfm_cback,
  NULL,
  ps4_l2cap_data_ind_cback,
  ps4_l2cap_congest_cback,
  NULL
};

static tL2CAP_CFG_INFO ps4_cfg_info;

bool is_connected = false;
uint16_t l2cap_control_channel = 0;
uint16_t l2cap_interrupt_channel = 0;

/********************************************************************************/
/*                      P U B L I C    F U N C T I O N S                        */
/********************************************************************************/

/*******************************************************************************
**
** Function         ps4_l2cap_init_services
**
** Description      This function initialises the required L2CAP services.
**
** Returns          void
**
*******************************************************************************/
void ps4_l2cap_init_services() {
  ps4_l2cap_init_service("PS4-HIDC", BT_PSM_HID_CONTROL, BTM_SEC_SERVICE_FIRST_EMPTY);
  ps4_l2cap_init_service("PS4-HIDI", BT_PSM_HID_INTERRUPT, BTM_SEC_SERVICE_FIRST_EMPTY + 1);
}

/*******************************************************************************
**
** Function         ps4_l2cap_deinit_services
**
** Description      This function deinitialises the required L2CAP services.
**
** Returns          void
**
*******************************************************************************/
void ps4_l2cap_deinit_services() {
  ps4_l2cap_deinit_service("PS4-HIDC", BT_PSM_HID_CONTROL);
  ps4_l2cap_deinit_service("PS4-HIDI", BT_PSM_HID_INTERRUPT);
}

/*******************************************************************************
**
** Function         ps4_l2cap_send_hid
**
** Description      This function sends the HID command using the L2CAP service.
**
** Returns          void
**
*******************************************************************************/
void ps4_l2cap_send_hid(hid_cmd_t *hid_cmd, uint8_t len) {
  uint8_t result;
  BT_HDR *p_buf;

  p_buf = (BT_HDR *)osi_malloc(BT_DEFAULT_BUFFER_SIZE);

  if (!p_buf) {
    ESP_LOGE(PS4_TAG, "[%s] allocating buffer for sending the command failed", __func__);
  }

  p_buf->length = len + (sizeof(*hid_cmd) - sizeof(hid_cmd->data));
  p_buf->offset = L2CAP_MIN_OFFSET;

  memcpy((uint8_t *)(p_buf + 1) + p_buf->offset, (uint8_t *)hid_cmd, p_buf->length);

  if (l2cap_control_channel == 0) {
    ESP_LOGE(PS4_TAG, "[%s] l2cap_control_channel not initialized.", __func__);
  }
  result = L2CA_DataWrite(l2cap_control_channel, p_buf);

  if (result == L2CAP_DW_SUCCESS)
    ESP_LOGI(PS4_TAG, "[%s] sending command: success", __func__);

  if (result == L2CAP_DW_CONGESTED)
    ESP_LOGW(PS4_TAG, "[%s] sending command: congested", __func__);

  if (result == L2CAP_DW_FAILED)
    ESP_LOGE(PS4_TAG, "[%s] sending command: failed", __func__);
}

/********************************************************************************/
/*                      L O C A L    F U N C T I O N S                          */
/********************************************************************************/

/*******************************************************************************
**
** Function         ps4_l2cap_init_service
**
** Description      This registers the specified bluetooth service in order
**                  to listen for incoming connections.
**
** Returns          void
**
*******************************************************************************/
static void ps4_l2cap_init_service(const char *name, uint16_t psm, uint8_t security_id) {
  // log_i("init services");
  /* Register the PSM for incoming connections */
  if (!L2CA_Register(psm, (tL2CAP_APPL_INFO *)&dyn_info)) {
    ESP_LOGE(PS4_TAG, "%s Registering service %s failed", __func__, name);
    return;
  }

  /* Register with the Security Manager for our specific security level (none) */
  if (!BTM_SetSecurityLevel(false, name, security_id, 0, psm, 0, 0)) {
    ESP_LOGE(PS4_TAG, "%s Registering security service %s failed", __func__, name);
    return;
  }

  ESP_LOGI(PS4_TAG, "[%s] Service %s Initialized", __func__, name);
}

/*******************************************************************************
**
** Function         ps4_l2cap_deinit_service
**
** Description      This deregisters the specified bluetooth service.
**
** Returns          void
**
*******************************************************************************/
static void ps4_l2cap_deinit_service(const char *name, uint16_t psm) {
  /* Deregister the PSM from incoming connections */
  L2CA_Deregister(psm);
  ESP_LOGI(PS4_TAG, "[%s] Service %s Deinitialized", __func__, name);
}

/*******************************************************************************
**
** Function         ps4_l2cap_connect_ind_cback
**
** Description      This the L2CAP inbound connection indication callback function.
**
** Returns          void
**
*******************************************************************************/
static void ps4_l2cap_connect_ind_cback(BD_ADDR bd_addr, uint16_t l2cap_cid, uint16_t psm, uint8_t l2cap_id) {
  memcpy(ps4PeerAddr, bd_addr, 6);  // Game hub : adresse de la manette (lien force en mode actif ensuite)
  ps4PmDone = 0;
  ESP_LOGI(PS4_TAG, "[%s] bd_addr: %s\n  l2cap_cid: 0x%02x\n  psm: %d\n  id: %d", __func__, bd_addr, l2cap_cid, psm, l2cap_id);

  /* Send connection pending response to the L2CAP layer. */
  L2CA_CONNECT_RSP(bd_addr, l2cap_id, l2cap_cid, L2CAP_CONN_PENDING, L2CAP_CONN_PENDING, NULL, NULL);

  /* Send response to the L2CAP layer. */
  L2CA_CONNECT_RSP(bd_addr, l2cap_id, l2cap_cid, L2CAP_CONN_OK, L2CAP_CONN_OK, NULL, NULL);

  /* Send a Configuration Request. */
  L2CA_CONFIG_REQ(l2cap_cid, &ps4_cfg_info);

  if (psm == BT_PSM_HID_CONTROL) {
    l2cap_control_channel = l2cap_cid;
  } else if (psm == BT_PSM_HID_INTERRUPT) {
    l2cap_interrupt_channel = l2cap_cid;
  }
}

/*******************************************************************************
**
** Function         ps4_l2cap_connect_cfm_cback
**
** Description      This is the L2CAP connect confirmation callback function.
**
**
** Returns          void
**
*******************************************************************************/
static void ps4_l2cap_connect_cfm_cback(uint16_t l2cap_cid, uint16_t result) {
  ESP_LOGI(PS4_TAG, "[%s] l2cap_cid: 0x%02x\n  result: %d", __func__, l2cap_cid, result);
}

/*******************************************************************************
**
** Function         ps4_l2cap_config_cfm_cback
**
** Description      This is the L2CAP config confirmation callback function.
**
**
** Returns          void
**
*******************************************************************************/
void ps4_l2cap_config_cfm_cback(uint16_t l2cap_cid, tL2CAP_CFG_INFO *p_cfg) {
  ESP_LOGI(PS4_TAG, "[%s] l2cap_cid: 0x%02x\n  p_cfg->result: %d", __func__, l2cap_cid, p_cfg->result);

  /* The PS4 controller is connected after    */
  /* receiving the second config confirmation */
  bool prev_is_connected = is_connected;
  is_connected = l2cap_cid == l2cap_interrupt_channel;
  if (prev_is_connected != is_connected) {
    ps4ConnectEvent(is_connected);
  }
}

/*******************************************************************************
**
** Function         ps4_l2cap_config_ind_cback
**
** Description      This is the L2CAP config indication callback function.
**
**
** Returns          void
**
*******************************************************************************/
void ps4_l2cap_config_ind_cback(uint16_t l2cap_cid, tL2CAP_CFG_INFO *p_cfg) {
  ESP_LOGI(PS4_TAG, "[%s] l2cap_cid: 0x%02x\n  p_cfg->result: %d\n  p_cfg->mtu_present: %d\n  p_cfg->mtu: %d", __func__, l2cap_cid, p_cfg->result, p_cfg->mtu_present, p_cfg->mtu);

  p_cfg->result = L2CAP_CFG_OK;

  L2CA_ConfigRsp(l2cap_cid, p_cfg);
}

/*******************************************************************************
**
** Function         ps4_l2cap_disconnect_ind_cback
**
** Description      This is the L2CAP disconnect indication callback function.
**
**
** Returns          void
**
*******************************************************************************/
void ps4_l2cap_disconnect_ind_cback(uint16_t l2cap_cid, bool ack_needed) {
  ESP_LOGI(PS4_TAG, "[%s] l2cap_cid: 0x%02x\n  ack_needed: %d", __func__, l2cap_cid, ack_needed);
  is_connected = false;
  if (ack_needed) {
    L2CA_DisconnectRsp(l2cap_cid);
  }
  ps4ConnectEvent(is_connected);
}

/*******************************************************************************
**
** Function         ps4_l2cap_disconnect_cfm_cback
**
** Description      This is the L2CAP disconnect confirm callback function.
**
**
** Returns          void
**
*******************************************************************************/
static void ps4_l2cap_disconnect_cfm_cback(uint16_t l2cap_cid, uint16_t result) {
  ESP_LOGI(PS4_TAG, "[%s] l2cap_cid: 0x%02x\n  result: %d", __func__, l2cap_cid, result);
}

/*******************************************************************************
**
** Function         ps4_l2cap_data_ind_cback
**
** Description      This is the L2CAP data indication callback function.
**
**
** Returns          void
**
*******************************************************************************/
/* Game hub (modification, GPL-3) : le paquet n'est plus lu a une position fixe. On cherche l'entete
   HID 0xA1 suivie du type de rapport (0x11 complet ou 0x01 court), puis on recopie les donnees pour que
   le stick gauche tombe a l'index 13 attendu par ps4_parser.c. Avec le core ESP32 2.0.17, la position
   fixe d'origine etait decalee : seul "haut" etait lu et la batterie etait fausse. */
uint8_t ps4DbgHdr[24];     // debut du 1er paquet recu (affiche dans le moniteur serie par le jeu)
volatile int ps4DbgPos = -2;  // position de l'entete 0xA1 trouvee (-1 : introuvable, -2 : rien recu)
volatile uint8_t ps4DbgType = 0;
volatile uint8_t ps4DbgRaw[12];  // octets 13 a 24 du paquet recale
volatile int ps4DbgLen = 0;
volatile uint32_t ps4DbgCount = 0;  // paquets recus (le jeu affiche le nombre par seconde)
volatile uint32_t ps4DbgSent = 0;   // rapports envoyes par la manette, d'apres son compteur (pour les pertes)
static void ps4_l2cap_data_ind_cback(uint16_t l2cap_cid, BT_HDR *p_buf) {
  uint8_t *d = p_buf->data;
  int n = p_buf->offset + p_buf->length;
  int pos = -1;
  ps4DbgCount++;
  if (ps4PmDone == 0 || (ps4PmDone < 4 && (ps4DbgCount & 255) == 0)) { ps4PmDone++; ps4ForceActive(); }  // 1er paquet puis 3 rappels
  if (p_buf->offset < n - 1 && d[p_buf->offset] == 0xA1 && (d[p_buf->offset + 1] == 0x11 || d[p_buf->offset + 1] == 0x01))
    pos = p_buf->offset;
  for (int i = 0; pos < 0 && i + 1 < n && i < 32; i++)
    if (d[i] == 0xA1 && (d[i + 1] == 0x11 || d[i + 1] == 0x01)) pos = i;
  if (ps4DbgPos == -2) {
    for (int i = 0; i < 24; i++) ps4DbgHdr[i] = i < n ? d[i] : 0;
    ps4DbgPos = pos;
    ps4DbgType = pos >= 0 ? d[pos + 1] : 0;
  }
  if (pos >= 0 && p_buf->length > 2) {
    static uint8_t tmp[96];
    int shift = d[pos + 1] == 0x11 ? 9 : 11;  // 0x11 : stick gauche a pos+4 ; 0x01 : a pos+2 -> index 13
    for (int i = 0; i < (int)sizeof(tmp); i++) {
      int j = pos - shift + i;
      tmp[i] = (j >= 0 && j < n) ? d[j] : 0;
    }
    for (int i = 0; i < 12; i++) ps4DbgRaw[i] = tmp[13 + i];  // sticks, boutons, gachettes (diagnostic)
    ps4DbgLen = n - pos;
    {  // le 7e octet apres les sticks porte un compteur 6 bits : l'ecart donne les rapports envoyes
      static int prevCnt = -1;
      int c = tmp[19] >> 2;
      ps4DbgSent += prevCnt < 0 ? 1 : ((c - prevCnt) & 63 ? (c - prevCnt) & 63 : 64);
      prevCnt = c;
    }
    parsePacket(tmp);
  }

  osi_free(p_buf);
}

/*******************************************************************************
**
** Function         ps4_l2cap_congest_cback
**
** Description      This is the L2CAP congestion callback function.
**
** Returns          void
**
*******************************************************************************/
static void ps4_l2cap_congest_cback(uint16_t l2cap_cid, bool congested) {
  ESP_LOGI(PS4_TAG, "[%s] l2cap_cid: 0x%02x\n  congested: %d", __func__, l2cap_cid, congested);
}
