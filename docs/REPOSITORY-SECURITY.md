<!--
DCF77 RC8000 Console v2.5.4
Copyright (c) 2026 Gianpaolo P.
Licensed under the PolyForm Noncommercial License 1.0.0.
Commercial use requires a separate written license from the copyright holder.
See LICENSE and NOTICE.md.
-->

# Repository security / Sicurezza del repository

## Protezioni applicate nel repository

- `CODEOWNERS`: proprietario del codice `@pgpaolo`.
- GitHub Actions con permesso minimo `contents: read`.
- Checkout senza persistenza delle credenziali.
- Azioni esterne fissate a commit SHA immutabili.
- PlatformIO CI fissato in `requirements-ci.txt`.
- Dependabot per GitHub Actions e dipendenze Python della CI.
- Workflow `Repository integrity` per controllare file fondamentali, licensing, file sensibili, artefatti generati e riferimenti Action immutabili.
- Pull request template con checklist di sicurezza e licensing.
- Regole di contribuzione in `CONTRIBUTING.md`.

## Protezioni GitHub da attivare nelle Settings

1. Ruleset su `main`.
2. Vietare force-push e cancellazione di `main`.
3. Richiedere pull request prima del merge.
4. Richiedere i check `build` e `integrity`.
5. Richiedere la risoluzione delle conversazioni.
6. Proteggere i tag `v*` da modifica/cancellazione.
7. Abilitare Secret Scanning e Push Protection, se disponibili.
8. Abilitare Dependabot alerts.
9. Valutare CodeQL C++ con build PlatformIO manuale.

## English summary

The repository now enforces least-privilege GitHub Actions, immutable action references, dependency monitoring, licensing integrity, forbidden-file checks and pull-request contribution rules. Administrative branch/tag rules and platform-level secret scanning still require repository Settings access.
