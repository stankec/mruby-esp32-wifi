#include <mruby.h>
#include <mruby/data.h>
#include <mruby/variable.h>
#include <mruby/string.h>

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

const int CONNECTED_BIT = BIT0;

static void
mrb_eh_ctx_t_free(mrb_state *mrb, void *p) {
  esp_event_loop_set_cb(NULL, NULL);
  mrb_free(mrb, p);
}

//
// Type declarations
//

static const struct mrb_data_type mrb_eh_station_ctx_t = {
  "$i_mrb_eh_station_ctx_t", mrb_eh_ctx_t_free
};

static const struct mrb_data_type mrb_eh_ap_ctx_t = {
  "$i_mrb_eh_ap_ctx_t", mrb_eh_ctx_t_free
};

static EventGroupHandle_t wifi_event_group;

typedef struct eh_station_ctx_t {
  TaskHandle_t task;
  mrb_state *mrb;
  mrb_value on_connected_blk;
  mrb_value on_disconnected_blk;
} eh_station_ctx_t;

typedef struct eh_ap_ctx_t {
  TaskHandle_t task;
  mrb_state *mrb;
  mrb_value station_connected_handler;
  mrb_value station_disconnected_handler;
  mrb_value station_got_ip_handler;
} eh_ap_ctx_t;

//
// Event handling
//

static esp_err_t
event_handler(void *ctx, system_event_t *event)
{
  eh_station_ctx_t *sehc = NULL;
  eh_ap_ctx_t *aehc = NULL;

  switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
      esp_wifi_connect();
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
      sehc = (eh_station_ctx_t *)ctx;
      if (sehc != NULL) {
        vTaskSuspend(sehc->task);
        int arena_index = mrb_gc_arena_save(sehc->mrb);

        mrb_value mrb_ip_str = mrb_str_buf_new(sehc->mrb, 13);
        char ip_str[13];
        sprintf(ip_str, IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));
        mrb_str_cat_cstr(sehc->mrb, mrb_ip_str, ip_str);

        if (!mrb_nil_p(sehc->on_connected_blk)) {
          mrb_assert(mrb_type(sehc->on_connected_blk) == MRB_TT_PROC);
          mrb_yield_argv(sehc->mrb, sehc->on_connected_blk, 1, &mrb_ip_str);
        }

        mrb_gc_arena_restore(sehc->mrb, arena_index);
        vTaskResume(sehc->task);
      }
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      // This is a workaround as ESP32 WiFi libs don't currently auto-reassociate.
      esp_wifi_connect();
      xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
      sehc = (eh_station_ctx_t *)ctx;
      if (sehc != NULL) {
        vTaskSuspend(sehc->task);
        int arena_index = mrb_gc_arena_save(sehc->mrb);

        if (!mrb_nil_p(sehc->on_disconnected_blk)) {
          mrb_assert(mrb_type(sehc->on_disconnected_blk) == MRB_TT_PROC);
          mrb_yield_argv(sehc->mrb, sehc->on_disconnected_blk, 0, NULL);
        }

        mrb_gc_arena_restore(sehc->mrb, arena_index);
        vTaskResume(sehc->task);
      }
      break;
    // Triggered when the AP starts
    /* case SYSTEM_EVENT_AP_START:*/
    /*   esp_wifi_connect();*/
    /*   break;*/
    // Triggered when the AP stops
    /* case SYSTEM_EVENT_AP_STOP:*/
    /*   break;*/
    // Triggered when a client connects to the network
    case SYSTEM_EVENT_AP_STACONNECTED:
      esp_wifi_connect();
      xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
      aehc = (eh_ap_ctx_t *)ctx;
      if (aehc != NULL) {
        vTaskSuspend(aehc->task);
        int arena_index = mrb_gc_arena_save(aehc->mrb);

        mrb_value mrb_mac_str = mrb_str_buf_new(aehc->mrb, 13);
        char mac_str[13];
        sprintf(mac_str, MACSTR, MAC2STR(event->event_info.sta_connected.mac));
        mrb_str_cat_cstr(aehc->mrb, mrb_mac_str, mac_str);

        if (!mrb_nil_p(aehc->station_connected_handler)) {
          mrb_assert(mrb_type(aehc->station_connected_handler) == MRB_TT_PROC);
          mrb_yield_argv(aehc->mrb, aehc->station_connected_handler, 1, &mrb_mac_str);
        }

        mrb_gc_arena_restore(aehc->mrb, arena_index);
        vTaskResume(aehc->task);
      }
      break;
    // Triggered when a client disconnects from the network
    case SYSTEM_EVENT_AP_STADISCONNECTED:
      esp_wifi_connect();
      xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
      aehc = (eh_ap_ctx_t *)ctx;
      if (aehc != NULL) {
        vTaskSuspend(aehc->task);
        int arena_index = mrb_gc_arena_save(aehc->mrb);

        mrb_value mrb_mac_str = mrb_str_buf_new(aehc->mrb, 13);
        char mac_str[13];
        sprintf(mac_str, MACSTR, MAC2STR(event->event_info.sta_disconnected.mac));
        mrb_str_cat_cstr(aehc->mrb, mrb_mac_str, mac_str);

        if (!mrb_nil_p(aehc->station_connected_handler)) {
          mrb_assert(mrb_type(aehc->station_connected_handler) == MRB_TT_PROC);
          mrb_yield_argv(aehc->mrb, aehc->station_disconnected_handler, 1, &mrb_mac_str);
        }

        mrb_gc_arena_restore(aehc->mrb, arena_index);
        vTaskResume(aehc->task);
      }
      break;
    // Triggered when a client gets an IP address on the network
    /* case SYSTEM_EVENT_AP_STAIPASSIGNED:*/
    /*   aehc = (eh_ap_ctx_t *)ctx;*/
    /*   if (aehc != NULL) {*/
    /*     vTaskSuspend(sehc->task);*/
    /*     int arena_index = mrb_gc_arena_save(sehc->mrb);*/

    /*     mrb_value mrb_ip_str = mrb_str_buf_new(sehc->mrb, 13);*/
    /*     char ip_str[13];*/
    /*     sprintf(ip_str, IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));*/
    /*     mrb_str_cat_cstr(sehc->mrb, mrb_ip_str, ip_str);*/

    /*     mrb_value mrb_ip_changed_bool = event->event_info.got_ip.ip_changed;*/

    /*     if (!mrb_nil_p(sehc->on_connected_blk)) {*/
    /*       mrb_assert(mrb_type(sehc->on_connected_blk) == MRB_TT_PROC);*/
    /*       mrb_yield_argv(sehc->mrb, sehc->on_connected_blk, 1, &mrb_ip_str);*/
    /*     }*/

    /*     mrb_gc_arena_restore(sehc->mrb, arena_index);*/
    /*     vTaskResume(sehc->task);*/
    /*   }*/
    /*   break;*/
    // Triggered when a client inquires about the network
    /* case SYSTEM_EVENT_AP_PROBEREQRECVED:*/
    /*   break;*/
    default:
        break;
  }
  return ESP_OK;
}

// Utils

wifi_auth_mode_t
mrb_esp32_wifi_auth_int_to_auth_mode(int value) {
  switch(value) {
    case 1:
      return WIFI_AUTH_WEP;
    case 2:
      return WIFI_AUTH_WPA_PSK;
    case 3:
      return WIFI_AUTH_WPA2_PSK;
    case 4:
      return WIFI_AUTH_WPA_WPA2_PSK;
    case 5:
      return WIFI_AUTH_WPA2_ENTERPRISE;
    default:
      return WIFI_AUTH_OPEN;
  };
}

// Station / Client mode

static mrb_value
mrb_esp32_wifi_init(mrb_state *mrb, mrb_value self) {
  eh_station_ctx_t *ehc = mrb_malloc(mrb, sizeof(eh_station_ctx_t));
  ehc->task = xTaskGetCurrentTaskHandle();
  ehc->mrb = mrb;
  ehc->on_connected_blk = mrb_nil_value();
  ehc->on_disconnected_blk = mrb_nil_value();

  mrb_data_init(self, ehc, &mrb_eh_station_ctx_t);

  wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK( esp_event_loop_init(event_handler, (eh_station_ctx_t *) DATA_PTR(self)) );

  return self;
}

static mrb_value
mrb_esp32_wifi_connect(mrb_state *mrb, mrb_value self) {
  char *ssid = NULL;
  char *password = NULL;

  mrb_get_args(mrb, "zz", &ssid, &password);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
  ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

  wifi_config_t wifi_config;
  memset((void *)&wifi_config, 0, sizeof(wifi_config_t));
  snprintf(wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
  snprintf(wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", password);

  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
  ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
  ESP_ERROR_CHECK( esp_wifi_start() );

  return self;
}

static mrb_value
mrb_esp32_wifi_disconnect(mrb_state *mrb, mrb_value self) {
  ESP_ERROR_CHECK( esp_wifi_stop() );

  return self;
}

static mrb_value
mrb_esp32_wifi_set_on_connected(mrb_state *mrb, mrb_value self) {
  mrb_value block;
  mrb_get_args(mrb, "&", &block);

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@on_connected_blk"), block);

  eh_station_ctx_t *ehc = (eh_station_ctx_t *) DATA_PTR(self);
  ehc->on_connected_blk = block;

  return self;
}

static mrb_value
mrb_esp32_wifi_set_on_disconnected(mrb_state *mrb, mrb_value self) {
  mrb_value block;
  mrb_get_args(mrb, "&", &block);

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@on_disconnected_blk"), block);

  eh_station_ctx_t *ehc = (eh_station_ctx_t *) DATA_PTR(self);
  ehc->on_disconnected_blk = block;

  return self;
}

// AP mode

static mrb_value
mrb_esp32_ap_init(mrb_state *mrb, mrb_value self) {
  eh_ap_ctx_t *ehc = mrb_malloc(mrb, sizeof(eh_ap_ctx_t));
  ehc->task = xTaskGetCurrentTaskHandle();
  ehc->mrb = mrb;
  ehc->station_connected_handler = mrb_nil_value();
  ehc->station_disconnected_handler = mrb_nil_value();
  ehc->station_got_ip_handler = mrb_nil_value();

  mrb_data_init(self, ehc, &mrb_eh_ap_ctx_t);

  wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK( esp_event_loop_init(event_handler, (eh_ap_ctx_t *) DATA_PTR(self)) );

  return self;
}

static mrb_value
mrb_esp32_wifi_ap_start(mrb_state *mrb, mrb_value self) {
  char *ssid = NULL;
  char *psk = NULL;
  int auth_mode_val;
  int max_connections;
  int channel = 0;
  bool hidden = false;

  mrb_get_args(
    mrb,
    "ziziib",
    &ssid,
    &auth_mode_val,
    &psk,
    &max_connections,
    &channel,
    &hidden
  );

  wifi_auth_mode_t auth_mode =
    mrb_esp32_wifi_auth_int_to_auth_mode(auth_mode_val);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
  ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

  wifi_config_t wifi_config;
  memset((void *)&wifi_config, 0, sizeof(wifi_config_t));

  snprintf(wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", ssid);
  snprintf(wifi_config.ap.password, sizeof(wifi_config.ap.password), "%s", psk);
  wifi_config.ap.authmode = auth_mode;
  wifi_config.ap.max_connection = max_connections;
  wifi_config.ap.ssid_hidden = hidden;
  if (channel > 0) {
    wifi_config.ap.channel = channel;
  }

  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
  ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &wifi_config) );
  ESP_ERROR_CHECK( esp_wifi_start() );

  return self;
}

static mrb_value
mrb_esp32_wifi_ap_stop(mrb_state *mrb, mrb_value self) {
  ESP_ERROR_CHECK( esp_wifi_stop() );

  return self;
}

static mrb_value
mrb_esp32_wifi_ap_set_station_connected_handler(mrb_state *mrb, mrb_value self) {
  mrb_value block;
  mrb_get_args(mrb, "&", &block);

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@station_connected_handler"), block);

  eh_ap_ctx_t *ehc = (eh_ap_ctx_t *) DATA_PTR(self);
  ehc->station_connected_handler = block;

  return self;
}

static mrb_value
mrb_esp32_wifi_ap_set_station_disconnected_handler(mrb_state *mrb, mrb_value self) {
  mrb_value block;
  mrb_get_args(mrb, "&", &block);

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@station_disconnected_handler"), block);

  eh_ap_ctx_t *ehc = (eh_ap_ctx_t *) DATA_PTR(self);
  ehc->station_disconnected_handler = block;

  return self;
}

static mrb_value
mrb_esp32_wifi_ap_set_station_got_ip_handler(mrb_state *mrb, mrb_value self) {
  mrb_value block;
  mrb_get_args(mrb, "&", &block);

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@station_got_ip_handler"), block);

  eh_ap_ctx_t *ehc = (eh_ap_ctx_t *) DATA_PTR(self);
  ehc->station_got_ip_handler = block;

  return self;
}

// Gem definition

void
mrb_mruby_esp32_wifi_gem_init(mrb_state* mrb) {
  tcpip_adapter_init();

  // Define all constants - ESP32::WiFi::Station and ESP32::WiFi::AccessPoint
  struct RClass *esp32_module = mrb_define_module(mrb, "ESP32");
  struct RClass *esp32_wifi_module = mrb_define_module_under(mrb, esp32_module, "WiFi");
  struct RClass *esp32_wifi_ap_class = mrb_define_class_under(mrb, esp32_wifi_module, "AccessPoint", mrb->object_class);
  struct RClass *esp32_wifi_station_class = mrb_define_class_under(mrb, esp32_wifi_module, "Station", mrb->object_class);

  // Station class
  mrb_define_method(mrb, esp32_wifi_station_class, "initialize", mrb_esp32_wifi_init, MRB_ARGS_NONE());

  mrb_define_method(mrb, esp32_wifi_station_class, "connect", mrb_esp32_wifi_connect, MRB_ARGS_REQ(2));
  mrb_define_method(mrb, esp32_wifi_station_class, "disconnect", mrb_esp32_wifi_disconnect, MRB_ARGS_NONE());

  mrb_define_method(mrb, esp32_wifi_station_class, "on_connected", mrb_esp32_wifi_set_on_connected, MRB_ARGS_BLOCK());
  mrb_define_method(mrb, esp32_wifi_station_class, "on_disconnected", mrb_esp32_wifi_set_on_disconnected, MRB_ARGS_BLOCK());

  // AccessPoint class
  mrb_define_method(mrb, esp32_wifi_ap_class, "initialize", mrb_esp32_ap_init, MRB_ARGS_NONE());

  mrb_define_method(mrb, esp32_wifi_ap_class, "__start", mrb_esp32_wifi_ap_start, MRB_ARGS_REQ(3));
  mrb_define_method(mrb, esp32_wifi_ap_class, "__stop", mrb_esp32_wifi_ap_stop, MRB_ARGS_NONE());

  mrb_define_method(mrb, esp32_wifi_ap_class, "on_station_connected", mrb_esp32_wifi_ap_set_station_connected_handler, MRB_ARGS_BLOCK());
  mrb_define_method(mrb, esp32_wifi_ap_class, "on_station_disconnected", mrb_esp32_wifi_ap_set_station_disconnected_handler, MRB_ARGS_BLOCK());
  mrb_define_method(mrb, esp32_wifi_ap_class, "on_station_got_ip", mrb_esp32_wifi_ap_set_station_got_ip_handler, MRB_ARGS_BLOCK());
}

void
mrb_mruby_esp32_wifi_gem_final(mrb_state* mrb) {
}
