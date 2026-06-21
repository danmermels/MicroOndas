# Project Rules

## WiFi and OTA Update Constraints
- **Maintain Static IP Configurations**: If modifying WiFi connection routines, always ensure that static IP configurations (using `WiFi.config(...)`) are correctly initialized and re-applied upon reconnection. Do not let the device revert to DHCP unless explicitly instructed.
