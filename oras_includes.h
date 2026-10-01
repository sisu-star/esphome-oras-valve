// Headers used by the lambdas in oras-discovery.yaml and oras-valve.yaml.
// ESPHome does not pull these in by default, and a lambda body cannot hold
// its own #include lines, so they live here.
#pragma once

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <esp_mac.h>             // esp_base_mac_addr_set, esp_read_mac
#include <esp_bt_device.h>       // esp_bt_dev_get_address
#include <esp_gap_ble_api.h>     // esp_ble_gap_* advertising calls
