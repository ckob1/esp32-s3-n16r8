#include "ble_provision.h"
#include "wifi_utils.h"
#include "logger.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static BLEServer* bleServer = NULL;
static BLECharacteristic* bleTx = NULL;
static bool bleConnected = false;
static bool bleWasConnected = false;

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
        logger_println("[BLE] Received: " + data);

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

void ble_provision_start() {
    BLEDevice::init("ESP32-AI-Setup");
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
    bleServer->getAdvertising()->start();
    logger_println("[BLE] Provisioning service ready");
}

void ble_provision_loop() {
    if (!bleConnected && bleWasConnected) {
        delay(500);
        bleServer->startAdvertising();
        logger_println("[BLE] Advertising again");
    }
    bleWasConnected = bleConnected;
}
