/*--------------------------------------------------------------------
SenseCAP Traffic
Copyright 2020 fukuen
--------------------------------------------------------------------*/

#include <Arduino.h>

#include <LovyanGFX.hpp>

#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <driver/i2c.h>

#include <ArduinoJson.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>

#include <math.h>
#include <Button.h>

#include "SenseCapD1.h"

static LGFX lcd;
static LGFX_Sprite sprite(&lcd);
static LGFX_Sprite sprite_map(&sprite);

#define TAG "traffic"

#define SSID "<ssid>"
#define PASS "<password>"
#define ZOOM 14
// 新宿都庁付近
#define LAT 35.6895014
#define LON 139.6917337
#define TRAFFIC_REFRESH_MSEC 60 * 1000
#define MAP_REFRESH_MSEC 24 * 60 * 60 * 1000

#define ARRAY_SIZE(x) sizeof(x)/sizeof(x[0])
String infos[] = {"R13", "C01", "A03"}; // 東京都、首都高、高速道路（関東）

int i, xt, yt, _x, _y, base_xtile, base_ytile = 0;
int x01, x02, y01, y02;
unsigned long lastRefreshTraffic = 0;
unsigned long lastRefreshMap = 0;

HTTPClient http;
WiFiClient * stream;

String target;
JsonDocument doc;

#define DEBOUNCE_MS 10
#define BUTTON_PIN 38
Button BtnA = Button(BUTTON_PIN, true, DEBOUNCE_MS);

int bl = 1;

time_t nowSec;
struct tm timeinfo;

uint8_t *img_buff;
unsigned long img_len;

void printCurrentTime() {
  nowSec = time(nullptr);
 
  gmtime_r(&nowSec, &timeinfo);
  ESP_LOGI(TAG, "Current time: %s", asctime(&timeinfo));
}
 
void setClock() {
  configTime(9 * 3600, 0, "ntp.nict.jp", "pool.ntp.org", "time.nist.gov");
 
  ESP_LOGI(TAG, "Waiting for NTP time sync: ");
  nowSec = time(nullptr);
  while (nowSec < 8 * 3600 * 2) {
    delay(500);
    ESP_LOGI(TAG, ".");
    yield();
    nowSec = time(nullptr);
  }
 
  printCurrentTime();
}


int doHttpGet(String url, uint8_t *p_buffer, unsigned long *p_len){
  HTTPClient http;

  ESP_LOGI(TAG, "[HTTP] GET begin...\n");
  http.begin(url);

  ESP_LOGI(TAG, "[HTTP] GET...\n");
  int httpCode = http.GET();
  unsigned long index = 0;

  if (httpCode > 0) {
      ESP_LOGI(TAG, "[HTTP] GET... code: %d\n", httpCode);

      if (httpCode == HTTP_CODE_OK) {
        WiFiClient * stream = http.getStreamPtr();

        int len = http.getSize();
        ESP_LOGI(TAG, "[HTTP] Content-Length=%d\n", len);
        if (len != -1 && len > *p_len) {
          ESP_LOGI(TAG, "[HTTP] buffer size over\n");
          http.end();
          return -1;
        }

        // read all data from server
        while (http.connected() && (len > 0 || len == -1)) {
            size_t size = stream->available();

            if (size > 0) {
                if( (index + size ) > *p_len){
                  ESP_LOGI(TAG, "[HTTP] buffer size over\n");
                  http.end();
                  return -1;
                }
                int c = stream->readBytes(&p_buffer[index], size);

                index += c;
                if (len > 0) {
                    len -= c;
                }
            }
            delay(1);
        }
      } else {
        http.end();
        return -1;
      }
  } else {
    http.end();
    ESP_LOGI(TAG, "[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    return -1;
  }

  http.end();
  *p_len = index;

  return 0;
}

int getPng(String url) {
  img_len = sizeof(img_buff);
  img_len = 128 * 1024;
  return doHttpGet(url, img_buff, &img_len);
}

int getJson(String url) {
  img_len = 512 * 1024;
  return doHttpGet(url, img_buff, &img_len);
}

String tile_to_url(int xtile, int ytile, int zoom_level) {
  // タイル座標を地図URLに変換
  return "https://www.jartic.or.jp/d/map/degital/" + String(zoom_level) + "/" + String(xtile) + "/" + String(ytile) + ".png";
}

void latlon_to_pos(double lat, double lon, int zoom_level) {
  // 緯度経度からタイル座標に変換
  double xd = (lon / 180 + 1) * std::pow(2, zoom_level) / 2;
  xt = int(xd);
  _x = int((xd - xt) * 256);
  double yd = ((-log(tan((45 + lat / 2) * M_PI / 180)) + M_PI) * std::pow(2, zoom_level) / (2 * M_PI));
	yt = int(yd);
  _y = int((yd - yt) * 256);
}

void drawGeometry(JsonObject geometry, String cs) {
  // 交通情報描画
  double lat = -1.0;
  double lon = -1.0;
  x01 = -1;
  y01 = -1;
  int color = 0x0000;
  if (cs == "01") {
    color = TFT_BLACK;
  } else if (cs == "401") {
    color = TFT_RED;
  } else if (cs == "402") {
    color = TFT_ORANGE;
  }
  String type = geometry["type"].as<String>();
  JsonArray coordinates = geometry["coordinates"];
  if (type == "LineString") {
    for (JsonArray a : coordinates) {
      lon = a[0].as<double>();
      lat = a[1].as<double>();
      latlon_to_pos(lat, lon, ZOOM);
      if (xt < base_xtile - 1 || xt > base_xtile + 1 || yt < base_ytile - 1 || yt > base_ytile + 1) {
        continue;
      }
      if (x01 < 0) {
        x01 = _x + (xt - base_xtile + 1) * 256;
        y01 = _y + (yt - base_ytile + 1) * 256;
      } else {
        x02 = _x + (xt - base_xtile + 1) * 256;
        y02 = _y + (yt - base_ytile + 1) * 256;
        sprite.drawLine(x01 - 144, y01 - 144, x02 - 144, y02 - 144, color);
        sprite.drawLine(x01 - 144 + 1, y01 - 144 + 1, x02 - 144 + 1, y02 - 144 + 1, color);
        sprite.drawLine(x01 - 144 - 1, y01 - 144 - 1, x02 - 144 - 1, y02 - 144 - 1, color);
        x01 = _x + (xt - base_xtile + 1) * 256;
        y01 = _y + (yt - base_ytile + 1) * 256;
      }
    }
  } else if (type == "MultiLineString") {
    for (JsonArray l : coordinates) {
      for (JsonArray a : l) {
        lon = a[0].as<double>();
        lat = a[1].as<double>();
        latlon_to_pos(lat, lon, ZOOM);
        if (xt < base_xtile - 1 || xt > base_xtile + 1 || yt < base_ytile - 1 || yt > base_ytile + 1) {
          continue;
        }
        if (x01 < 0) {
          x01 = _x + (xt - base_xtile + 1) * 256;
          y01 = _y + (yt - base_ytile + 1) * 256;
        } else {
          x02 = _x + (xt - base_xtile + 1) * 256;
          y02 = _y + (yt - base_ytile + 1) * 256;
          sprite.drawLine(x01 - 144, y01 - 144, x02 - 144, y02 - 144, color);
          sprite.drawLine(x01 - 144 + 1, y01 - 144 + 1, x02 - 144 + 1, y02 - 144 + 1, color);
          sprite.drawLine(x01 - 144 - 1, y01 - 144 - 1, x02 - 144 - 1, y02 - 144 - 1, color);
          x01 = _x + (xt - base_xtile + 1) * 256;
          y01 = _y + (yt - base_ytile + 1) * 256;
        }
      }
    }
  }
}

void getTarget() {
  // 最新情報の日時
  int ret = getJson("https://www.jartic.or.jp/d/traffic_info/r1/target.json");
  if (ret < 0) return;
  deserializeJson(doc, img_buff);
  const char* t = doc["target"];
  ESP_LOGI(TAG, "target: %s", t);
  target = String(t);
}

void getTrafficInfos(String route) {
  // 交通情報描画
  int ret = getJson("https://www.jartic.or.jp/d/traffic_info/r1/" + target + "/d/301/" + route + ".json");
  if (ret < 0) return;
  deserializeJson(doc, img_buff);
  const char* type = doc["type"];
  JsonArray array = doc["features"].as<JsonArray>();
  int len = array.size();
  for (JsonObject v : array) {
    const char* type2 = v["type"];
    String cs = v["properties"]["cs"].as<String>();
    JsonObject geometry = v["geometry"];
    if (cs == "01") {
      // 通行止め(歩行者天国等)
      drawGeometry(geometry, cs);
    } else if (cs == "40") {
      // 工事
    } else if (cs == "401") {
      // 渋滞
      drawGeometry(geometry, cs);
    } else if (cs == "402") {
      // 混雑
      drawGeometry(geometry, cs);
    }
  }
}

void drawMap() {
  // 地図の描画
  latlon_to_pos(LAT, LON, ZOOM);
  base_xtile = xt;
  base_ytile = yt;
  int ix, iy = 0;
  String url = "";
  for (iy = 0; iy < 3; iy++) {
    for (ix = 0; ix < 3; ix++) {
      lcd.fillRect(0, 470, (iy * 3 + ix + 1) * 50, 480, TFT_BLUE);
      url = tile_to_url(base_xtile + ix - 1, base_ytile + iy - 1, ZOOM);
      getPng(url);
      sprite_map.drawPng(img_buff, img_len, 256 * ix - 144, 256 * iy - 144);
    }
  }
}

void refreshTraffic() {
  // 交通情報の更新
  ESP_LOGI(TAG, "refreshTraffic");
  getTarget();
  for (int i = 0; i < ARRAY_SIZE(infos); i++) {
    lcd.fillRect(0, 470, (i + 1) * 160, 480, TFT_GREEN);
    getTrafficInfos(infos[i]);
  }
  sprite.setTextSize(2);
  sprite.setTextColor(TFT_BLACK);
  sprite.setCursor(5, 5);
  sprite.print(target.substring(0, 4) + "/" + target.substring(4, 6) + "/" + target.substring(6, 8) + " " + target.substring(8, 10) + ":" + target.substring(10, 12));
}

void setup() {

  pinMode(38, INPUT);

  delay(5000);

  ESP_LOGI(TAG, "Connecting WiFi");
  WiFi.mode(WIFI_STA);
  int status = WiFi.begin(SSID, PASS);
  while ( WiFi.status() != WL_CONNECTED) {
    delay(1000);
    ESP_LOGI(TAG, ".");
  }
  ESP_LOGI(TAG, "Connected.");

  lcd.init();
  lcd.setBrightness(128);
  bl = 1;

  img_buff = (uint8_t *)heap_caps_malloc(256*1024*(sizeof(uint8_t)), MALLOC_CAP_SPIRAM);

  sprite.setPsram(true);
  sprite_map.setPsram(true);
  sprite.createSprite(480, 480);
  sprite_map.createSprite(480, 480);

  setClock();
}

void loop() {
  // 1日1回地図を更新
  if (lastRefreshMap == 0 || millis() - lastRefreshMap > MAP_REFRESH_MSEC) {
    drawMap();
    lastRefreshMap = millis();
  }

  if (lastRefreshTraffic == 0 || millis() - lastRefreshTraffic > TRAFFIC_REFRESH_MSEC) {
  // 60秒毎に交通情報を更新
    sprite_map.pushSprite(0, 0);
    refreshTraffic();
    sprite.pushSprite(0, 0);
    lastRefreshTraffic = millis();
  }

  if (BtnA.wasPressed()) {
    ESP_LOGI(TAG, "pressed");
    if (bl == 1) {
      ESP_LOGI(TAG, "Backlight OFF");
      lcd.setBrightness(0);
      bl = 0;
    } else {
      ESP_LOGI(TAG, "Backlight ON");
      lcd.setBrightness(128);
      bl = 1;
    }
  }

  BtnA.read();
  delay(10);

}
