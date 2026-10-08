<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# Contributing

Contributions are welcome when they improve the technical quality of the project and preserve its licensing and security model.

## Mandatory rules

1. Do not commit passwords, tokens, private keys, Wi-Fi credentials, certificates, local secret files or generated firmware binaries.
2. Do not remove or weaken copyright, PolyForm Noncommercial or Required Notice text.
3. Do not modify `LICENSE` or `NOTICE.md` without explicit authorization from the copyright holder.
4. Hardware changes must document GPIOs, board revision, receiver model and observed behavior.
5. Changes to DCF77 timing, PLL, erasure recovery, OLED RF-quiet behavior or radio duty cycling must include a reproducible validation description.
6. `pio run` and repository-integrity checks must pass before merge.
7. Use pull requests for proposed changes and avoid direct changes to `main`.

By submitting a contribution, you represent that you have the right to submit it and agree that it may be distributed under the repository's current license.

## Security

Never post real credentials in issues, logs, screenshots or pull requests. Redact environment-specific secrets when they are not required to reproduce a problem.
