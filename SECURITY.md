<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# Security

- Do not commit `include/secrets.h`; it is intentionally ignored.
- Prefer the captive portal / LittleFS provisioning over hard-coded production Wi-Fi credentials.
- The device exposes an unauthenticated diagnostic Web UI on the local network and is intended for a trusted LAN / lab environment.
- The setup AP password is a provisioning convenience, not a hardened security boundary.
- Do not expose port 80 of the device directly to the Internet.
