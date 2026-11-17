#include "setup_portal.h"

#include <stdio.h>
#include <string.h>

#include "../core/text.h"

namespace stella {
namespace {

// Appends to a fixed buffer, remembering overflow (the page then ends early
// instead of corrupting memory).
struct Writer {
  char *buf;
  size_t cap, len = 0;
  bool overflow = false;
  Writer(char *b, size_t c) : buf(b), cap(c) {
    if (cap) buf[0] = '\0';
  }
  void put(const char *s) {
    const size_t n = strlen(s);
    if (len + n + 1 > cap) {
      overflow = true;
      return;
    }
    memcpy(buf + len, s, n + 1);
    len += n;
  }
  void putEscaped(const char *s) {
    char tmp[200];
    if (!SetupPortal::htmlEscape(tmp, sizeof(tmp), s)) overflow = true;
    put(tmp);
  }
};

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void wipe(char *p, size_t n) {
  volatile char *v = p;
  for (size_t i = 0; i < n; ++i) v[i] = 0;
}

// The page's style: small, dark and soft like the device.
const char kHead[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\"><title>Stella Wi-Fi</title><style>"
    "body{font-family:system-ui,sans-serif;background:#1c1330;color:#eee8f5;margin:0;padding:22px;"
    "max-width:430px;margin:auto}h1{font-size:24px;margin:8px 0 4px}p{color:#bcb2c9;line-height:1.4}"
    "input,button{font:inherit;border-radius:14px;border:0;padding:13px 14px;margin:5px 0}"
    "input{width:100%;box-sizing:border-box;background:#2e283f;color:#eee8f5}"
    "button{background:#bdbde7;color:#211e30;font-weight:600}.n button{background:#2e283f;color:#eee8f5;"
    "margin:3px 4px 3px 0;font-weight:400}.go{width:100%;margin-top:10px}"
    ".bad{color:#f0a9b5}.ok{color:#b3d6cd}</style></head><body><h1>Stella Wi-Fi</h1>";

}  // namespace

int SetupPortal::urlDecode(char *dst, size_t cap, const char *src, size_t len) {
  if (!cap) return -1;
  size_t out = 0;
  for (size_t i = 0; i < len; ++i) {
    char c = src[i];
    if (c == '+') {
      c = ' ';
    } else if (c == '%') {
      const int hi = i + 1 < len ? hexValue(src[i + 1]) : -1;
      const int lo = i + 2 < len ? hexValue(src[i + 2]) : -1;
      if (hi < 0 || lo < 0) {  // a stray '%' stays as it is
        c = '%';
      } else {
        c = (char)(hi * 16 + lo);
        i += 2;
      }
    }
    if (out + 1 >= cap) {
      dst[out] = '\0';
      return -1;
    }
    dst[out++] = c;
  }
  dst[out] = '\0';
  return (int)out;
}

bool SetupPortal::formField(const char *body, size_t len, const char *name, char *out, size_t cap) {
  const size_t nameLen = strlen(name);
  size_t i = 0;
  while (i < len) {
    size_t end = i;
    while (end < len && body[end] != '&') ++end;
    if (end - i > nameLen && memcmp(body + i, name, nameLen) == 0 && body[i + nameLen] == '=') {
      return urlDecode(out, cap, body + i + nameLen + 1, end - i - nameLen - 1) >= 0;
    }
    i = end + 1;
  }
  if (cap) out[0] = '\0';
  return false;
}

bool SetupPortal::htmlEscape(char *dst, size_t cap, const char *src) {
  size_t out = 0;
  bool fits = true;
  for (const char *p = src; *p; ++p) {
    const char *rep = nullptr;
    switch (*p) {
      case '&': rep = "&amp;"; break;
      case '<': rep = "&lt;"; break;
      case '>': rep = "&gt;"; break;
      case '"': rep = "&quot;"; break;
      case '\'': rep = "&#39;"; break;
      default: break;
    }
    const size_t n = rep ? strlen(rep) : 1;
    if (out + n + 1 > cap) {
      fits = false;
      break;
    }
    if (rep) memcpy(dst + out, rep, n);
    else dst[out] = *p;
    out += n;
  }
  if (cap) dst[out] = '\0';
  return fits;
}

void SetupPortal::page(SetupResponse &response, const char *notice, bool problem) {
  Writer w(response.body, response.capacity);
  w.put(kHead);
  if (notice && notice[0]) {
    w.put(problem ? "<p class=bad>" : "<p class=ok>");
    w.putEscaped(notice);
    w.put("</p>");
  } else {
    w.put("<p>Choose your home network and enter its password. The Stella screen shows how it goes.</p>");
  }
  const WifiScan scan = control_.setupScan();
  if (scan.count) {
    w.put("<div class=n>");
    for (int i = 0; i < scan.count; ++i) {
      w.put("<button type=button onclick=\"n.value=this.textContent\">");
      w.putEscaped(scan.networks[i].ssid);
      w.put("</button>");
    }
    w.put("</div>");
  }
  w.put("<form method=post action=/join><input id=n name=ssid placeholder=\"Network name\" maxlength=32 required "
        "autocapitalize=none autocorrect=off><input name=pw type=password placeholder=Password maxlength=63 "
        "autocapitalize=none autocorrect=off><button class=go>Connect</button></form></body></html>");
  response.status = 200;
  response.length = w.len;
}

void SetupPortal::handleSetup(const SetupRequest &request, SetupResponse &response) {
  response.contentType = "text/html; charset=utf-8";
  if (!response.body || response.capacity < 64) return;
  if (!request.post) {
    // Any path serves the page, so a phone's connectivity probe lands on it.
    const NetworkStatus status = control_.setupStatus();
    if (status.state == WifiState::Connecting) {
      char note[64];
      snprintf(note, sizeof(note), "Connecting to %.32s\xe2\x80\xa6", status.ssid);
      page(response, note, false);
    } else if (status.error[0] && status.state != WifiState::Connected) {
      char note[96];
      snprintf(note, sizeof(note), "%.40s. Check the password and try again.", status.error);
      page(response, note, true);
    } else {
      page(response, "", false);
    }
    return;
  }
  if (strcmp(request.path, "/join") != 0) {
    response.status = 404;
    response.length = 0;
    if (response.capacity) response.body[0] = '\0';
    return;
  }
  char ssid[kMaxSsid + 2], password[kMaxPassword + 2];
  const bool haveSsid = formField(request.body, request.bodyLen, "ssid", ssid, sizeof(ssid)) && ssid[0];
  const bool havePassword = formField(request.body, request.bodyLen, "pw", password, sizeof(password)) ||
                            !strstr(request.body, "pw=");  // an open network sends no password
  if (!haveSsid || !havePassword || strlen(ssid) > kMaxSsid || strlen(password) > kMaxPassword) {
    wipe(password, sizeof(password));
    response.status = 400;
    page(response, "That didn't look right. Check the name and password and try again.", true);
    response.status = 400;
    return;
  }
  control_.setupJoin(ssid, password);
  wipe(password, sizeof(password));
  char note[128];
  snprintf(note, sizeof(note), "Trying %.32s. Watch the Stella screen; setup closes by itself when it connects.", ssid);
  page(response, note, false);
}

}  // namespace stella
