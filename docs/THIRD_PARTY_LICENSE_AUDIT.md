# Third-Party License Audit (EN + FR)

Use this file before any public release.

---

## EN - How to use

1. List every third-party dependency actually shipped in source, binary, image, or release artifact.
2. Verify license compatibility with the repository license.
3. Record whether redistribution is allowed.
4. Add required notices/attributions in `NOTICE` and release notes.

Status labels:
- `OK`: reviewed and safe.
- `REVIEW`: unclear, legal check required.
- `BLOCKED`: cannot redistribute publicly in current form.

## FR - Mode d'emploi

1. Lister toutes les dependances tierces distribuees (source, binaire, image, release).
2. Verifier la compatibilite de licence avec la licence du depot.
3. Noter clairement le droit de redistribution.
4. Reporter les mentions obligatoires dans `NOTICE` et les release notes.

Etiquettes:
- `OK`: verifie et publiable.
- `REVIEW`: ambigu, verification legale requise.
- `BLOCKED`: non redistribuable en l'etat.

---

## Audit table

| Component | Source/URL | Version | License | Redistribution | Attribution required | Status | Notes |
|---|---|---|---|---|---|---|---|
| GNU-EFI |  |  |  |  |  | REVIEW |  |
| mbedTLS (if bundled) |  |  |  |  |  | REVIEW |  |
| Model weights (GGUF/BIN) |  |  |  |  |  | REVIEW | Verify model card terms |
| Tokenizer files |  |  |  |  |  | REVIEW |  |
| Any external scripts/tools |  |  |  |  |  | REVIEW |  |

---

## Release decision

- Legal owner sign-off:
- Date:
- Final decision: `GO` / `NO-GO`
