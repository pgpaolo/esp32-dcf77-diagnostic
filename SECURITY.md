<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# Security

## Repository security

Automated checks protect licensing notices, reject local secret files and generated firmware artifacts, verify immutable GitHub Action references and detect obvious hard-coded Wi-Fi credentials or private-key material.

`include/secrets.h` is intentionally excluded from Git and must remain local.

## Device security model

The firmware is intended for a trusted LAN / laboratory environment.

- Prefer captive-portal / LittleFS provisioning over hard-coded Wi-Fi credentials.
- Do not expose the diagnostic Web UI directly to the Internet.
- Do not forward TCP/80 from the public Internet to the device.
- The setup AP password is a provisioning convenience, not a hardened security boundary.
- Treat serial logs and screenshots as potentially sensitive environment data.

## Reporting

Do not publish passwords, tokens, private keys or other sensitive data in public issues or pull requests. If a security report requires sensitive evidence, contact the repository owner before publishing those details.

## Licensing integrity

Security changes must preserve `LICENSE`, `NOTICE.md` and the copyright/license notices embedded in project-authored files.
