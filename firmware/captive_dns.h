#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// ── Minimal captive-portal DNS ────────────────────────────────
// Answers every A-record query with the SoftAP IP (192.168.4.1).
// That is what makes Android/iOS/Windows show the "Sign in to network"
// sheet after joining the recovery AP.

static WiFiUDP  captiveDnsUdp;
static bool     captiveDnsActive = false;

static void captiveDnsStart() {
  if (captiveDnsActive) return;
  if (captiveDnsUdp.begin(53)) {
    captiveDnsActive = true;
    Serial.println(F("Captive DNS: listening on 53"));
  } else {
    Serial.println(F("Captive DNS: could not bind port 53"));
  }
}

static void captiveDnsStop() {
  if (!captiveDnsActive) return;
  captiveDnsUdp.stop();
  captiveDnsActive = false;
}

static void captiveDnsService() {
  if (!captiveDnsActive) return;

  int packetSize = captiveDnsUdp.parsePacket();
  if (packetSize <= 0) return;

  uint8_t buffer[256];
  int len = captiveDnsUdp.read(buffer, sizeof(buffer));
  if (len < 12) return;                       // too short to be a DNS header

  // Only answer standard queries. QR bit (0x80) must be 0.
  if (buffer[2] & 0x80) return;

  const uint16_t qdcount = (buffer[4] << 8) | buffer[5];
  if (qdcount != 1) return;                   // we only handle single-question queries

  // Walk the QNAME to find where the question ends.
  int pos = 12;
  while (pos < len && buffer[pos] != 0) {
    if ((buffer[pos] & 0xC0) == 0xC0) { pos += 2; break; }  // compression pointer
    pos += buffer[pos] + 1;
  }
  if (pos >= len) return;
  if (buffer[pos] == 0) pos++;                // skip the null terminator
  pos += 4;                                   // QTYPE + QCLASS
  if (pos > len) return;

  // Build the response: flip QR, set RA, keep the question, add one A answer.
  buffer[2] |= 0x80;                          // QR = 1
  buffer[3] |= 0x80;                          // RA = 1
  buffer[6] = 0; buffer[7] = 1;               // ANCOUNT = 1
  buffer[8] = buffer[9] = buffer[10] = buffer[11] = 0;  // NS/AR = 0

  static const uint8_t answer[] = {
    0xC0, 0x0C,             // name pointer → offset 12 (the question)
    0x00, 0x01,             // type A
    0x00, 0x01,             // class IN
    0x00, 0x00, 0x00, 0x3C, // TTL 60 s
    0x00, 0x04,             // rdlength = 4
    192, 168, 4, 1          // 192.168.4.1
  };

  captiveDnsUdp.beginPacket(captiveDnsUdp.remoteIP(), captiveDnsUdp.remotePort());
  captiveDnsUdp.write(buffer, pos);           // header + question
  captiveDnsUdp.write(answer, sizeof(answer));
  captiveDnsUdp.endPacket();
}