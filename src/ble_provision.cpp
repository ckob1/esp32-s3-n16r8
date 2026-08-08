#include "ble_provision.h"
#include "wifi_utils.h"
#include "logger.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static BLEServer* bleServer = NULL;
static BLECharacteristic* bleTx = NULL;
static bool bleConnected = false;
static bool bleWasConnected = false;
static bool bleStackReady = false;
static bool bleProvisionEnabled = false;

class ProvisionServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) {
        bleConnected = true;
        logger_println("[BLE] Client connected");
    }

    void onDisconnect(BLEServer* server) {
        bleConnected = false;
    }
};

class ProvisionWriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) {
        std::string value = characteristic->getValue();
        if (value.length() == 0) return;

        String data = String(value.c_str());
        data.trim();
        // 安全: 写入内容含 WiFi 密码, 只记录长度, 不落日志
        logger_println("[BLE] RX " + String(data.length()) + " bytes");

        String ssid;
        String pass;

        int sep = data.indexOf(':');
        if (sep > 0) {
            ssid = data.substring(0, sep);
            pass = data.substring(sep + 1);
        } else {
            int eq1 = data.indexOf("ssid=");
            int amp = data.indexOf('&');
            if (eq1 >= 0) {
                ssid = data.substring(eq1 + 5, amp > eq1 ? amp : data.length());
                int eq2 = data.indexOf("pass=");
                if (eq2 >= 0) {
                    pass = data.substring(eq2 + 5);
                }
            }
        }

        ssid.trim();
        pass.trim();

        if (ssid.length() == 0) {
            if (bleTx && bleConnected) {
                bleTx->setValue("ERR:SSID_REQUIRED");
                bleTx->notify();
            }
            return;
        }
        if (ssid.length() > 64 || pass.length() > 128) {
            if (bleTx && bleConnected) {
                bleTx->setValue("ERR:VALUE_TOO_LONG");
                bleTx->notify();
            }
            return;
        }

        wifi_save_credentials(ssid, pass);
        logger_println("[BLE] WiFi saved, rebooting...");
        if (bleTx && bleConnected) {
            bleTx->setValue("OK:SAVED");
            bleTx->notify();
        }
        delay(300);
        ESP.restart();
    }
};

void ble_stack_init() {
    if (bleStackReady) return;
    BLEDevice::init(WIFI_AP_SSID);
    bleStackReady = true;
    logger_println("[BLE] stack initialized");
}

void ble_provision_start() {
    ble_stack_init();
    if (bleServer) {
        bleProvisionEnabled = true;
        bleServer->getAdvertising()->start();
        return;
    }
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new ProvisionServerCallbacks());

    BLEService* service = bleServer->createService(BLE_SERVICE_UUID);
    bleTx = service->createCharacteristic(
        BLE_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    bleTx->addDescriptor(new BLE2902());

    BLECharacteristic* bleRx = service->createCharacteristic(
        BLE_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    bleRx->setCallbacks(new ProvisionWriteCallbacks());

    service->start();
    bleProvisionEnabled = true;
    bleServer->getAdvertising()->start();
    logger_println("[BLE] Provisioning service ready");
}

void ble_provision_stop() {
    // 生产环境只在 AP 配网窗口广播配网服务; 停止广播即可关闭攻击面。
    if (bleServer) {
        bleProvisionEnabled = false;
        bleServer->getAdvertising()->stop();
        logger_println("[BLE] Provisioning advertising stopped");
    }
}

void ble_provision_loop() {
    if (!bleServer || !bleProvisionEnabled) {
        bleWasConnected = false;
        return;
    }
    if (!bleConnected && bleWasConnected) {
        delay(500);
        bleServer->startAdvertising();
        logger_println("[BLE] Advertising again");
    }
    bleWasConnected = bleConnected;
}

int ble_scan_devices(BleScanEntry* out, int max) {
    if (!out || max <= 0) return 0;

    ble_stack_init();
    BLEScan* scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    BLEScanResults results = scan->start(3, false);

    int n = 0;
    for (int i = 0; i < results.getCount() && n < max; i++) {
        BLEAdvertisedDevice dev = results.getDevice(i);
        out[n].name = dev.haveName() ? String(dev.getName().c_str()) : "";
        out[n].address = dev.getAddress().toString().c_str();
        out[n].rssi = dev.getRSSI();
        n++;
    }

    scan->clearResults();
    return n;
}
