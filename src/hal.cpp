#include "hal.h"
#include <LittleFS.h>

void task_hal_update(void *)
{
    while (1)
    {
        if (hal._hookButton)
        {
            while (hal.btnr.isPressing() || hal.btnl.isPressing() || hal.btnc.isPressing())
            {
                hal.btnr.tick();
                hal.btnl.tick();
                hal.btnc.tick();
                delay(20);
            }
            hal.btnr.tick();
            hal.btnl.tick();
            hal.btnc.tick();
            while (hal._hookButton)
            {
                while (hal.SleepUpdateMutex)
                    delay(10);
                hal.update();
                delay(20);
            }
            while (hal.btnr.isPressing() || hal.btnl.isPressing() || hal.btnc.isPressing())
            {
                delay(20);
            }
        }
        while (hal.SleepUpdateMutex)
            delay(10);
        hal.SleepUpdateMutex = true;
        hal.btnr.tick();
        hal.btnl.tick();
        hal.btnc.tick();
        hal.SleepUpdateMutex = false;
        delay(20);
        while (hal.SleepUpdateMutex)
            delay(10);
        hal.SleepUpdateMutex = true;
        hal.btnr.tick();
        hal.btnl.tick();
        hal.btnc.tick();
        hal.update();
        hal.SleepUpdateMutex = false;
        delay(20);
    }
}
void HAL::saveConfig()
{
    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile)
    {
        Serial.println("Failed to open config file for writing");
        return;
    }
    serializeJson(config, configFile);
    configFile.close();
}
void HAL::loadConfig()
{
    File configFile = LittleFS.open("/config.json", "r");
    if (!configFile)
    {
        Serial.println("Failed to open config file");
        return;
    }
    deserializeJson(config, configFile);
    configFile.close();
}

void HAL::getTime()
{
    int64_t tmp;
    if (peripherals.peripherals_current & PERIPHERALS_DS3231_BIT)
    {
        xSemaphoreTake(peripherals.i2cMutex, portMAX_DELAY);
        timeinfo.tm_year = peripherals.rtc.getYear() + 100;
        timeinfo.tm_mon = peripherals.rtc.getMonth() - 1;
        timeinfo.tm_mday = peripherals.rtc.getDate();
        timeinfo.tm_hour = peripherals.rtc.getHour();
        timeinfo.tm_min = peripherals.rtc.getMinute();
        timeinfo.tm_sec = peripherals.rtc.getSecond();
        timeinfo.tm_wday = peripherals.rtc.getDoW() - 1;
        now = mktime(&timeinfo);
        xSemaphoreGive(peripherals.i2cMutex);
    }
    else
    {
        time(&now);
        if (delta != 0 && lastsync < now)
        {
            // 下面修正时钟频率偏移
            tmp = now - lastsync;
            tmp *= delta;
            tmp /= every;
            now -= tmp;
        }
        localtime_r(&now, &timeinfo);
    }
}

void HAL::WiFiConfigSmartConfig()
{
#include "img_esptouch.h"
    display.fillScreen(GxEPD_WHITE);
    display.drawXBitmap(0, 0, esptouch_bits, 296, 128, GxEPD_BLACK);
    display.display();
    WiFi.beginSmartConfig();
    int count = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
        ++count;
        if (count >= 240) // 120秒超时
        {
            Serial.println("SmartConfig超时");
            display.fillScreen(GxEPD_WHITE);
            u8g2Fonts.setCursor(70, 80);
            u8g2Fonts.print("SmartConfig超时");
            display.display();
            delay(100);
            hal.powerOff(false);
            ESP.restart();
        }
    }
    /*
    void esp_dpp_start();
    esp_dpp_start();
    */
    if (WiFi.waitForConnectResult() == WL_CONNECTED)
    {
        Serial.println("WiFi connected");
        config[PARAM_SSID] = WiFi.SSID();
        config[PARAM_PASS] = WiFi.psk();
        hal.saveConfig();
    }
}

#include <DNSServer.h>
void HAL::WiFiConfigManual()
{
    DNSServer dnsServer;
#include "img_manual.h"
    String passwd = String((esp_random() % 1000000000L) + 10000000L); // 生成随机密码
    String str = "WIFI:T:WPA2;S:WeatherClock;P:" + passwd + ";;";
    WiFi.softAP("WeatherClock", passwd.c_str());
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    beginWebServer();
    display.fillScreen(GxEPD_WHITE);
    display.drawXBitmap(0, 0, wifi_manual_bits, 296, 128, GxEPD_BLACK);
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(7)];
    qrcode_initText(&qrcode, qrcodeData, 6, 0, str.c_str());
    Serial.println(qrcode.size);
    for (uint8_t y = 0; y < qrcode.size; y++)
    {
        // Each horizontal module
        for (uint8_t x = 0; x < qrcode.size; x++)
        {
            display.fillRect(2 * x + 20, 2 * y + 20, 2, 2, qrcode_getModule(&qrcode, x, y) ? GxEPD_BLACK : GxEPD_WHITE);
        }
    }
    display.setFont(&FreeSans9pt7b);
    display.setCursor(192, 124);
    display.print(passwd);
    display.display();
    uint32_t last_millis = millis();
    while (1)
    {
        dnsServer.processNextRequest();
        updateWebServer();
        delay(5);
        if (WiFi.softAPgetStationNum() == 0)
        {
            last_millis = millis();
        }
        if (millis() - last_millis > 600000) // 10分钟超时
        {
            Serial.println("手动配置超时");
            display.fillScreen(GxEPD_WHITE);
            u8g2Fonts.setCursor(70, 80);
            u8g2Fonts.print("手动配置超时");
            display.display();
            delay(100);
            hal.powerOff(false);
            ESP.restart();
        }
        if (LuaRunning)
            continue;
        if (hal.btnl.isPressing())
        {
            while (hal.btnl.isPressing())
                delay(20);
            ESP.restart();
            break;
        }
    }
}
void HAL::ReqWiFiConfig()
{
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setCursor(0, 20);
    u8g2Fonts.print("无法连接到WiFi");
    u8g2Fonts.setCursor(0, 40);
    u8g2Fonts.print("向左:网页配置");
    u8g2Fonts.setCursor(0, 60);
    u8g2Fonts.print("向右:SmartConfig");
    u8g2Fonts.setCursor(0, 80);
    u8g2Fonts.print("中间:离线模式");
    display.display();
    uint32_t last_millis = millis();
    while (1)
    {
        if (hal.btnl.isPressing())
        {
            WiFiConfigManual();
        }
        if (hal.btnr.isPressing())
        {
            WiFiConfigSmartConfig();
        }
        if (hal.btnc.isPressing())
        {
            WiFi.disconnect(true);
            config[PARAM_CLOCKONLY] = "1";
            hal.saveConfig();
            ESP.restart();
            break;
        }
        delay(5);
        if (millis() - last_millis > 60000) // 1分钟超时
        {
            Serial.println("WiFi配置方式选择超时");
            break;
        }
    }
    if (WiFi.isConnected() == false)
    {
        config[PARAM_CLOCKONLY] = "1";
        hal.saveConfig();
        ESP.restart();
    }
}
#include "esp_spi_flash.h"
#include "esp_rom_md5.h"
#include "esp_partition.h"
#define PARTITION_TOTAL 4
#define PARTITIONS_OFFSET 0x8000
#define PARTITION_SPIFFS (4 - 1)

void test_littlefs_size(bool format = true)
{
    uint32_t size_request; // 存储目的分区大小
    size_t size_physical = 0;
    esp_flash_get_physical_size(esp_flash_default_chip, &size_physical);
    size_request = size_physical - 0x300000 - 0x1000;
    if (hal.pref.getUInt("size", 0) != size_request)
    {
        Serial.println("检测到分区大小不一致，正在格式化");
        hal.pref.putUInt("size", size_request);
        LittleFS.format();
    }
}
void refresh_partition_table()
{
    md5_context_t ctx;
    static uint8_t table[16 * 20];
    static uint8_t table1[16 * 20];
    esp_rom_md5_init(&ctx);
    union
    {
        uint32_t size;
        uint8_t size_byte[4];
    } partition_size;
    uint32_t size_request; // 存储目的分区大小
    size_t size_physical = 0;
    esp_flash_get_physical_size(esp_flash_default_chip, &size_physical);
    if(size_physical <= 0)
    {
        Serial.println("获取Flash大小失败");
        return;
    }
    size_request = size_physical - 0x300000 - 0x1000;
    esp_flash_read(esp_flash_default_chip, table, 0x8000, sizeof(table));
    memcpy(partition_size.size_byte, &table[16 * 2 * PARTITION_SPIFFS + 0x8], 4);
    Serial.printf("当前LittleFS分区大小%d\n期望LittleFS分区大小%d\n", partition_size.size, size_request);
    if (partition_size.size != size_request)
    {
        Serial.printf("正在修改分区表\n");
        partition_size.size = size_request;
        memcpy(&table[16 * 2 * PARTITION_SPIFFS + 0x8], partition_size.size_byte, 4);
        Serial.println("正在计算MD5\n");
        esp_rom_md5_update(&ctx, table, 16 * 2 * PARTITION_TOTAL);
        esp_rom_md5_final(&table[16 * (2 * PARTITION_TOTAL + 1)], &ctx);
        esp_flash_set_chip_write_protect(esp_flash_default_chip, false);
        Serial.println("\n正在写入");
        if (esp_flash_erase_region(esp_flash_default_chip, 0x8000, 0x1000) != ESP_OK)
        {
            Serial.println("擦除失败");
            while (1)
                vTaskDelay(1000);
        }
        if (esp_flash_write(esp_flash_default_chip, table, 0x8000, sizeof(table)) != ESP_OK)
        {
            Serial.println("写入失败");
            while (1)
                vTaskDelay(1000);
        }
        Serial.println("完成，正在校验结果");
        esp_flash_read(esp_flash_default_chip, table1, 0x8000, sizeof(table1));
        if (memcmp(table, table1, sizeof(table)) != 0)
        {
            Serial.println("校验失败");
            while (1)
                vTaskDelay(1000);
        }
        else
        {
            for (size_t i = 0; i < 16 * 12; i++)
            {
                Serial.printf("0x%02X ", table[i]);
                if ((i + 1) % 16 == 0)
                {
                    Serial.println();
                }
            }
        }
        ESP.restart();
    }
}
bool HAL::init()
{
    int16_t total_gnd = 0;
    bool timeerr = false;
    bool initial = true;
    Serial.begin(115200);
    // 读取时钟偏移
    pref.begin("clock");
    pinMode(PIN_BUTTONR, INPUT);
    pinMode(PIN_BUTTONL, INPUT);
    pinMode(PIN_BUTTONC, INPUT);
    total_gnd += digitalRead(PIN_BUTTONR);
    total_gnd += digitalRead(PIN_BUTTONL);
    total_gnd += digitalRead(PIN_BUTTONC);
    if (total_gnd <= 1)
    {
        btnl._buttonPressed = 1;
        btnr._buttonPressed = 1;
        btnc._buttonPressed = 1;
        btn_activelow = false;
    }
    else
    {
        ESP_LOGW("HAL", "此设备为旧版硬件，建议尽快升级以获得最佳体验。");
        btnl._buttonPressed = 0;
        btnr._buttonPressed = 0;
        btnc._buttonPressed = 0;
        btn_activelow = true;
    }

    esp_task_wdt_init(portMAX_DELAY, false);
    pinMode(PIN_CHARGING, INPUT);
    pinMode(PIN_SD_CARDDETECT, INPUT_PULLUP);
    pinMode(PIN_SDVDD_CTRL, OUTPUT);
    digitalWrite(PIN_SDVDD_CTRL, 1);
    digitalWrite(PIN_BUZZER, 0);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, 0);
    display.epd2.startQueue();
    display.init(0, initial);
    display.setRotation(pref.getUChar(SETTINGS_PARAM_SCREEN_ORIENTATION, 3));
    display.setTextColor(GxEPD_BLACK);
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.begin(display);
    if (hal.btnl.isPressing() && (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED))
    {
        // 复位时检查左键是否按下，可以用于无限重启时临时关机
        powerOff(true);
        ESP.restart();
    }

    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (pref.getUInt("size", 0) != p->size)
    {
        pref.putUInt("size", p->size);
    }
    refresh_partition_table();
    if (pref.getUInt("lastsync") == 0)
    {
        pref.putUInt("lastsync", 1);  // 上次同步时间的准确时间
        pref.putInt("every", 100);    // 两次校准间隔多久
        pref.putInt("delta", 0);      // 这两次校准之间时钟偏差秒数，时钟时间-准确时间
        pref.putInt("upint", 2 * 60); // NTP同步间隔
    }
    lastsync = pref.getUInt("lastsync", 1); // 上次同步时间的准确时间
    every = pref.getInt("every", 100);      // 两次校准间隔多久
    delta = pref.getInt("delta", 0);        // 这两次校准之间时钟偏差秒数，时钟时间-准确时间
    upint = pref.getInt("upint", 2 * 60);   // NTP同步间隔
    // 系统“自检”
    WiFi.mode(WIFI_OFF);
    peripherals.init();
    getTime();
    if ((timeinfo.tm_year < (2016 - 1900)))
    {
        timeerr = true;              // 需要同步时间
        pref.putUInt("lastsync", 1); // 清除上次同步时间，但不清除时钟偏移信息
        lastsync = 1;
    }
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED)
        initial = false;
    // 下面进行初始化

    if (LittleFS.begin(false) == false)
    {
        display.fillScreen(GxEPD_WHITE);
        u8g2Fonts.setCursor(70, 80);
        u8g2Fonts.print("等待LittleFS格式化");
        display.display();
        LittleFS.format();
        if (LittleFS.begin(false) == false)
        {
            Serial.println("LittleFS格式化失败");
            display.fillScreen(GxEPD_WHITE);
            u8g2Fonts.setCursor(70, 80);
            u8g2Fonts.print("LittleFS格式化失败");
            display.display(true);
            delay(100);
            powerOff(false);
            ESP.restart();
        }
        test_littlefs_size(false);
    }
    test_littlefs_size(true);
    if (LittleFS.exists("/config.json") == false)
    {
        Serial.println("正在写入默认配置");
        File f = LittleFS.open("/config.json", "w");
        f.print(DEFAULT_CONFIG);
        f.close();
    }
    loadConfig();
    setenv("TZ", config[PARAM_TIMEZONE], 1);
    tzset();
    weather.begin();
    buzzer.init();
    xTaskCreate(task_hal_update, "hal_update", 2048, NULL, 10, NULL);
    if (initial == false && timeerr == false)
    {
        return false;
    }
    return true;
}

void HAL::autoConnectWiFi()
{
    if (WiFi.isConnected())
    {
        return;
    }
    // 下面连接WiFi
    if (config[PARAM_SSID] == "")
    {
        ReqWiFiConfig();
    }
    else
    {
        WiFi.setHostname("weatherclock");
        WiFi.mode(WIFI_STA);
        WiFi.begin(config[PARAM_SSID].as<const char *>(), config[PARAM_PASS].as<const char *>());
    }
    if (!WiFi.isConnected())
    {
        if (WiFi.waitForConnectResult(20000) != WL_CONNECTED)
        {
            hal.ReqWiFiConfig();
        }
    }
    sntp_stop();
}
static void set_sleep_set_gpio_interrupt()
{
    if (hal.btn_activelow)
    {
        esp_sleep_enable_ext0_wakeup((gpio_num_t)hal._wakeupIO[0], 0);
        esp_sleep_enable_ext1_wakeup((1LL << hal._wakeupIO[1]), ESP_EXT1_WAKEUP_ALL_LOW);
    }
    else
    {
        esp_sleep_enable_ext1_wakeup((1ULL << PIN_BUTTONC) | (1ULL << PIN_BUTTONL) | (1ULL << PIN_BUTTONR), ESP_EXT1_WAKEUP_ANY_HIGH);
    }
}

#include "driver/ledc.h"
static void pre_sleep()
{
    peripherals.sleep();
    set_sleep_set_gpio_interrupt();
    display.hibernate();
    buzzer.waitForSleep();
    delay(10);
    ledcDetachPin(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, 0);
}
static void wait_display()
{
    while(uxQueueMessagesWaiting(display.epd2.getQueue()) > 0)
    {
        delay(10);
    }
    while(display.epd2.isBusy())
    {
        delay(10);
    }
}
void HAL::goSleep(uint32_t sec)
{
    hal.getTime();
    long nextSleep = 0;
    if (sec != 0)
        nextSleep = sec;
    else
    {
        nextSleep = 1;
    }
    Serial.printf("下次唤醒:%ld s\n", nextSleep);
    nextSleep = nextSleep * 1000000UL;
    pre_sleep();
    esp_sleep_enable_timer_wakeup(nextSleep);
    wait_display();
    delay(10);
    if (noDeepSleep)
    {
        esp_light_sleep_start();
        display.init(0, false);
        peripherals.wakeup();
        ledcAttachPin(PIN_BUZZER, 0);
    }
    else
    {
        esp_deep_sleep_start();
    }
}

void HAL::powerOff(bool displayMessage)
{
    if (displayMessage)
    {
        display.setFullWindow();
        display.fillScreen(GxEPD_WHITE);
        u8g2Fonts.setCursor(120, 70);
        u8g2Fonts.print("已关机");
        display.display();
    }
    force_full_update = true;
    pre_sleep();
    WiFi.disconnect(true);
    set_sleep_set_gpio_interrupt();
    delay(10);
    if (noDeepSleep)
    {
        esp_light_sleep_start();
        display.init(0, false);
        peripherals.wakeup();
    }
    else
    {
        esp_deep_sleep_start();
    }
}
void HAL::update(void)
{
    static int count = 0;
    if (count++ % 30 == 0)
    {
        count = 0;
        getTime();
    }

    long adc;
    adc = analogRead(PIN_ADC);
    adc = adc * 7230 / 4096;
    VCC = adc;
    if (adc > 4400)
    {
        USBPluggedIn = true;
    }
    else
    {
        USBPluggedIn = false;
    }
    if (digitalRead(PIN_CHARGING) == 0)
    {
        isCharging = true;
    }
    else
    {
        isCharging = false;
    }
}
int HAL::getNTPMinute()
{
    int res[] = {
        0,
        2 * 60,
        4 * 60,
        6 * 60,
        12 * 60,
        24 * 60,
        36 * 60,
        48 * 60,
    };
    int val = pref.getUChar(SETTINGS_PARAM_NTP_INTERVAL, 1);
    return res[val];
}
#include "img_goodnightmorning.h"
uint8_t RTC_DATA_ATTR night_sleep_today = -1; // 用于判断今天是否退出过夜间模式
uint8_t RTC_DATA_ATTR night_sleep = 0;
void HAL::checkNightSleep()
{
    if (hal.timeinfo.tm_year < (2016 - 1900))
    {
        Serial.println("[夜间模式] 时间错误，直接返回");
        return;
    }
    if (config[PARAM_SLEEPATNIGHT].as<String>() == "0")
    {
        Serial.println("[夜间模式] 夜间模式已禁用");
        return;
    }
    if (night_sleep_today == hal.timeinfo.tm_mday)
    {
        Serial.println("[夜间模式] 当天暂时退出夜间模式");
        return;
    }
    if (hal.timeinfo.tm_year < (2016 - 1900))
    {
        Serial.println("[夜间模式] 时间错误");
        night_sleep = 0;
        night_sleep_today = -1;
        return;
    }
    String tmp = config[PARAM_SLEEPATNIGHT_START].as<String>();
    // 转换时间数据到分钟
    int sleepStart = tmp.substring(0, 2).toInt() * 60 + tmp.substring(3, 5).toInt();
    tmp = config[PARAM_SLEEPATNIGHT_END].as<String>();
    int sleepEnd = tmp.substring(0, 2).toInt() * 60 + tmp.substring(3, 5).toInt();
    bool end_at_nextday = sleepStart > sleepEnd; // 是否在第二天结束
    int now = hal.timeinfo.tm_hour * 60 + hal.timeinfo.tm_min;
    uint8_t night_sleep_pend = 0; // 当前夜间模式状态
    if (end_at_nextday)
    {
        if (now >= sleepStart)
        {
            // 晚安
            night_sleep_pend = 1;
        }
        else if (now < sleepEnd)
        {
            // 早上好
            night_sleep_pend = 2;
        }
        else
        {
            night_sleep_pend = 0;
        }
    }
    else
    {
        int mid = sleepStart + sleepEnd;
        mid = mid / 2;
        if (now >= sleepStart && now <= sleepEnd)
        {
            if (now < mid)
            {
                night_sleep_pend = 1;
            }
            else
            {
                night_sleep_pend = 2;
            }
        }
        else
        {
            night_sleep_pend = 0;
        }
    }
    // 判断当前屏幕显示
    if (night_sleep != night_sleep_pend)
    {
        Serial.println("[DEBUG] 夜间模式重绘");
        night_sleep = night_sleep_pend;
        display.clearScreen();
        if (night_sleep == 1)
        {
            // 晚安
            display.drawXBitmap(0, 0, goodnight_bits, 296, 128, 0);
        }
        else if (night_sleep == 2)
        {
            // 早上好
            display.drawXBitmap(0, 0, goodmorning_bits, 296, 128, 0);
        }
        display.display(false);
    }
    // 判断是否进入睡眠
    if (night_sleep != 0)
    {
        hal.goSleep(1800); // 休眠半小时再看
    }
}
void HAL::setWakeupIO(int io1, int io2)
{
    _wakeupIO[0] = io1;
    _wakeupIO[1] = io2;
}
void HAL::copy(File &newFile, File &file)
{
    char *buf = (char *)malloc(512);
    size_t currentsize = 0;
    if (!buf)
    {
        Serial.println("内存已满");
        ESP.restart();
    }
    while (1)
    {
        currentsize = file.readBytes(buf, 512);
        if (currentsize == 0)
            break;
        newFile.write((uint8_t *)buf, currentsize);
    }
    free(buf);
}
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

void HAL::rm_rf(const char *path)
{
    DIR *dp;
    struct dirent *entry;
    struct stat statbuf;

    // 打开目录
    if ((dp = opendir(path)) == NULL)
    {
        perror("opendir");
        return;
    }

    // 迭代读取目录中的文件
    while ((entry = readdir(dp)) != NULL)
    {
        // 获取文件的完整路径
        char filePath[256];
        sprintf(filePath, "%s/%s", path, entry->d_name);

        // 获取文件信息
        if (stat(filePath, &statbuf) == -1)
        {
            perror("lstat");
            continue;
        }

        // 判断是否是目录
        if (S_ISDIR(statbuf.st_mode))
        {
            // 忽略.和..目录
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            // 递归删除子目录
            rm_rf(filePath);
        }
        else
        {
            // 删除文件
            if (remove(filePath) != 0)
            {
                perror("remove");
            }
        }
    }

    // 关闭目录
    closedir(dp);

    // 删除空目录
    if (rmdir(path) != 0)
    {
        perror("rmdir");
    }
}

HAL hal;