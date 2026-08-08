#pragma once
#include <Arduino.h>

#define BLE_SCAN_MAX 8

struct BleScanEntry {
    String name;
    String address;
    int rssi;
};

void ble_stack_init();
void ble_provision_start();
void ble_provision_stop();
void ble_provision_loop();
int ble_scan_devices(BleScanEntry* out, int max);
