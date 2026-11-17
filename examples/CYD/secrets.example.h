#pragma once
// Copy to secrets.h (ignored by git) to join Wi-Fi for network time.
// Never commit secrets.h.
#define STELLA_WIFI_SSID ""
#define STELLA_WIFI_PASSWORD ""

// Over-the-air updates (optional). Build with the OTA layout:
//   arduino-cli compile --profile cyd-ota examples/CYD
// STELLA_UPDATE_URL: HTTPS address of manifest.json (no redirects).
// STELLA_UPDATE_PUBLIC_KEY: the public half of your signing key; make the
// pair with   python3 tools/update_tool.py keygen --private update-key.pem
//                                                --public update-public.pem
// which prints this definition. Keep the private key offline, out of git.
// #define STELLA_UPDATE_URL "https://example.com/stella/manifest.json"
// #define STELLA_UPDATE_PUBLIC_KEY \
//     "-----BEGIN PUBLIC KEY-----\n" \
//     "...\n" \
//     "-----END PUBLIC KEY-----\n"
