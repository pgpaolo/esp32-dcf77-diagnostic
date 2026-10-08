# Security

- Do not commit `include/secrets.h`; it is intentionally ignored.
- Prefer the captive portal / LittleFS provisioning over hard-coded production Wi-Fi credentials.
- The device exposes an unauthenticated diagnostic Web UI on the local network and is intended for a trusted LAN / lab environment.
- The setup AP password is a provisioning convenience, not a hardened security boundary.
- Do not expose port 80 of the device directly to the Internet.
