#pragma once
// Copy to secrets.h (ignored by git) to join Wi-Fi for network time.
// Never commit secrets.h.
#define STELLA_WIFI_SSID ""
#define STELLA_WIFI_PASSWORD ""

// Over-the-air updates need nothing here: the default address is this
// repository's published update. Only to use another host, or to require
// signed updates (optional, extra security; see docs/ARCHITECTURE.md):
// #define STELLA_UPDATE_URL "https://example.com/stella/manifest.json"
// #define STELLA_UPDATE_PUBLIC_KEY \
//     "-----BEGIN PUBLIC KEY-----\n" \
//     "...\n" \
//     "-----END PUBLIC KEY-----\n"
// (make a key pair with: python3 tools/update_tool.py keygen --private update-key.pem --public update-public.pem)
