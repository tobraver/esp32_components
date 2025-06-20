/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPDX-FileContributor: 2024 Sergey Kharenko
 * SPDX-FileContributor: 2024 Espressif Systems (Shanghai) CO LTD
 */

#include <string.h>
#include <stdlib.h>
#include <sys/cdefs.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_eth_phy_802_3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_eth_phy_ch390.h"
#include "driver/gpio.h"

#define CH390_INFO_OUI                       0x1CDC64

#define CH390_INFO_MODEL                     0x01

typedef struct {
    phy_802_3_t phy_802_3;
} phy_ch390_t;

static const char *TAG = "ch390.phy";

static esp_err_t ch390_update_link_duplex_speed(phy_ch390_t *ch390)
{
    esp_err_t ret = ESP_OK;
    esp_eth_mediator_t *eth = ch390->phy_802_3.eth;
    uint32_t addr = ch390->phy_802_3.addr;
    eth_speed_t speed = ETH_SPEED_10M;
    eth_duplex_t duplex = ETH_DUPLEX_HALF;
    bmcr_reg_t bmcr;
    bmsr_reg_t bmsr;
    uint32_t peer_pause_ability = false;
    anlpar_reg_t anlpar;
    ESP_GOTO_ON_ERROR(eth->phy_reg_read(eth, addr, ETH_PHY_BMSR_REG_ADDR, &(bmsr.val)), err, TAG, "read BMSR failed");
    ESP_GOTO_ON_ERROR(eth->phy_reg_read(eth, addr, ETH_PHY_BMSR_REG_ADDR, &(bmsr.val)), err, TAG, "read BMSR failed");
    ESP_GOTO_ON_ERROR(eth->phy_reg_read(eth, addr, ETH_PHY_ANLPAR_REG_ADDR, &(anlpar.val)), err, TAG, "read ANLPAR failed");
    eth_link_t link = bmsr.link_status ? ETH_LINK_UP : ETH_LINK_DOWN;
    /* check if link status changed */
    if (ch390->phy_802_3.link_status != link) {
        /* when link up, read negotiation result */
        if (link == ETH_LINK_UP) {
            ESP_GOTO_ON_ERROR(eth->phy_reg_read(eth, addr, ETH_PHY_BMCR_REG_ADDR, &(bmcr.val)), err, TAG, "read BMCR failed");
            if (bmcr.speed_select) {
                speed = ETH_SPEED_100M;
            } else {
                speed = ETH_SPEED_10M;
            }
            if (bmcr.duplex_mode) {
                duplex = ETH_DUPLEX_FULL;
            } else {
                duplex = ETH_DUPLEX_HALF;
            }
            ESP_GOTO_ON_ERROR(eth->on_state_changed(eth, ETH_STATE_SPEED, (void *)speed), err, TAG, "change speed failed");
            ESP_GOTO_ON_ERROR(eth->on_state_changed(eth, ETH_STATE_DUPLEX, (void *)duplex), err, TAG, "change duplex failed");
            /* if we're in duplex mode, and peer has the flow control ability */
            if (duplex == ETH_DUPLEX_FULL && anlpar.symmetric_pause) {
                peer_pause_ability = 1;
            } else {
                peer_pause_ability = 0;
            }
            ESP_GOTO_ON_ERROR(eth->on_state_changed(eth, ETH_STATE_PAUSE, (void *)peer_pause_ability), err, TAG, "change pause ability failed");
        }
        ESP_GOTO_ON_ERROR(eth->on_state_changed(eth, ETH_STATE_LINK, (void *)link), err, TAG, "change link failed");
        ch390->phy_802_3.link_status = link;
    }
    return ESP_OK;
err:
    return ret;
}

static esp_err_t ch390_get_link(esp_eth_phy_t *phy)
{
    esp_err_t ret = ESP_OK;
    phy_ch390_t *ch390 = __containerof(esp_eth_phy_into_phy_802_3(phy), phy_ch390_t, phy_802_3);
    /* Update information about link, speed, duplex */
    ESP_GOTO_ON_ERROR(ch390_update_link_duplex_speed(ch390), err, TAG, "update link duplex speed failed");
    return ESP_OK;
err:
    return ret;
}

static esp_err_t ch390_autonego_ctrl(esp_eth_phy_t *phy, eth_phy_autoneg_cmd_t cmd, bool *autonego_en_stat)
{
    esp_err_t ret = ESP_OK;
    phy_802_3_t *phy_802_3 = esp_eth_phy_into_phy_802_3(phy);
    esp_eth_mediator_t *eth = phy_802_3->eth;
    if (cmd == ESP_ETH_PHY_AUTONEGO_EN) {
        bmcr_reg_t bmcr;
        ESP_GOTO_ON_ERROR(eth->phy_reg_read(eth, phy_802_3->addr, ETH_PHY_BMCR_REG_ADDR, &(bmcr.val)), err, TAG, "read BMCR failed");
        ESP_GOTO_ON_FALSE(bmcr.en_loopback == 0, ESP_ERR_INVALID_STATE, err, TAG, "Auto-negotiation can't be enabled while in loopback operation");
    }
    return esp_eth_phy_802_3_autonego_ctrl(phy_802_3, cmd, autonego_en_stat);
err:
    return ret;
}

static esp_err_t ch390_loopback(esp_eth_phy_t *phy, bool enable)
{
    esp_err_t ret = ESP_OK;
    phy_802_3_t *phy_802_3 = esp_eth_phy_into_phy_802_3(phy);
    bool auto_nego_en = true;
    ESP_GOTO_ON_ERROR(ch390_autonego_ctrl(phy, ESP_ETH_PHY_AUTONEGO_G_STAT, &auto_nego_en), err, TAG, "get status of autonegotiation failed");
    ESP_GOTO_ON_FALSE(!(auto_nego_en && enable), ESP_ERR_INVALID_STATE, err, TAG,
                      "Unable to set loopback while auto-negotiation is enabled. Disable it to use loopback");
    return esp_eth_phy_802_3_loopback(phy_802_3, enable);
err:
    return ret;
}
static esp_err_t ch390_init(esp_eth_phy_t *phy)
{
    esp_err_t ret = ESP_OK;
    phy_802_3_t *phy_802_3 = esp_eth_phy_into_phy_802_3(phy);

    /* Basic PHY init */
    ESP_GOTO_ON_ERROR(esp_eth_phy_802_3_basic_phy_init(phy_802_3), err, TAG, "failed to init PHY");

    /* Check PHY ID */
    uint32_t oui;
    uint8_t model;
    ESP_GOTO_ON_ERROR(esp_eth_phy_802_3_read_oui(phy_802_3, &oui), err, TAG, "read OUI failed");
    ESP_GOTO_ON_ERROR(esp_eth_phy_802_3_read_manufac_info(phy_802_3, &model, NULL), err, TAG, "read manufacturer's info failed");
    ESP_GOTO_ON_FALSE(oui == CH390_INFO_OUI && model == CH390_INFO_MODEL, ESP_FAIL, err, TAG, "wrong chip ID");

    return ESP_OK;
err:
    return ret;
}

static esp_err_t ch390_reset_hw(esp_eth_phy_t *phy)
{
    phy_802_3_t *phy_802_3 = esp_eth_phy_into_phy_802_3(phy);
    if (phy_802_3->reset_gpio_num >= 0) {
        esp_rom_gpio_pad_select_gpio(phy_802_3->reset_gpio_num);
        gpio_set_direction(phy_802_3->reset_gpio_num, GPIO_MODE_OUTPUT);
        gpio_set_level(phy_802_3->reset_gpio_num, 0);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level(phy_802_3->reset_gpio_num, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return ESP_OK;
}

esp_eth_phy_t *esp_eth_phy_new_ch390(const eth_phy_config_t *config)
{
    esp_eth_phy_t *ret = NULL;
    phy_ch390_t *ch390 = calloc(1, sizeof(phy_ch390_t));
    ESP_GOTO_ON_FALSE(ch390, NULL, err, TAG, "calloc ch390 failed");
    ESP_GOTO_ON_FALSE(esp_eth_phy_802_3_obj_config_init(&ch390->phy_802_3, config) == ESP_OK,
                      NULL, err, TAG, "configuration initialization of PHY 802.3 failed");

    // override functions which need to be customized for sake of ch390
    ch390->phy_802_3.parent.init = ch390_init;
    ch390->phy_802_3.parent.reset_hw = ch390_reset_hw;
    ch390->phy_802_3.parent.get_link = ch390_get_link;
    ch390->phy_802_3.parent.autonego_ctrl = ch390_autonego_ctrl;
    ch390->phy_802_3.parent.loopback = ch390_loopback;

    return &ch390->phy_802_3.parent;
err:
    if (ch390 != NULL) {
        free(ch390);
    }
    return ret;
}
