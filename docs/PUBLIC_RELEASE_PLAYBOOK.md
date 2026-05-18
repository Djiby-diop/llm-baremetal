# Public Release Playbook (EN + FR)

This document is the operational checklist to publish this repository in a clean, professional, and legally safe way.

---

## EN - Goal

Publish a repository that is:

1. Clean (no temporary/debug/build garbage tracked).
2. Understandable (clear structure, docs, onboarding).
3. Legally safe (licenses and third-party attributions verified).
4. Security-safe (no secrets in history or current tree).

## EN - Required release gates

All gates must be green before switching to public.

1. Git hygiene
- `git status --short` must be empty.
- No accidental worktree changes in sibling repositories.
- No generated binaries tracked unless explicitly intentional.

2. Documentation quality
- `README.md` has: project purpose, architecture, quick start, test path, known limits.
- Security information available in `docs/SECURITY.md`.
- Public release process documented (this file).
- French/English explanation available for key onboarding concepts.

3. License and attribution
- Root `LICENSE` present and correct.
- Root `NOTICE` updated.
- Third-party components listed and checked in `docs/THIRD_PARTY_LICENSE_AUDIT.md`.
- Model files, tokenizer files, and datasets have explicit redistribution status.

4. Secrets and credentials
- No hardcoded API key, token, password, private key.
- Runtime integrations use environment variables only.
- Run preflight scan before tagging.

5. Reproducibility and quality
- Main smoke test path is documented and reproducible.
- Commands in docs are copy/paste-safe.
- Build artifacts are excluded from source control.

6. Public scope discipline
- Repository remains focused on baremetal runtime/prototype concerns.
- Out-of-scope private ecosystem material is not exposed in public docs.
- Run scope checker: `./scripts/public-scope-check.ps1`.

## EN - Recommended publish flow

1. Run local preflight:
- `./scripts/public-preflight.ps1`

2. Fix all red checks.

3. Re-run target smoke tests.

4. Prepare release notes:
- Scope, known limitations, migration notes.

5. Tag and publish only after legal review is complete.

6. Run scope check before final push:
- `./scripts/public-scope-check.ps1 -Strict`

---

## FR - Objectif

Publier un depot qui est:

1. Propre (pas de fichiers temporaires/debug/build suivis).
2. Clair (structure et documentation lisibles).
3. Legalement propre (licences et attributions verifiees).
4. Securise (pas de secrets dans le code ou l'historique recent).

## FR - Portes de validation obligatoires

Toutes les portes doivent etre vertes avant passage en public.

1. Hygiene Git
- `git status --short` doit etre vide.
- Pas de modifications accidentelles dans les worktrees voisins.
- Pas de binaires generes suivis, sauf cas explicitement voulu.

2. Qualite documentaire
- `README.md` couvre objectif, architecture, demarrage rapide, tests, limites.
- Guide securite disponible dans `docs/SECURITY.md`.
- Processus de publication documente (ce fichier).
- Explications FR/EN disponibles pour l'onboarding.

3. Licences et attribution
- `LICENSE` racine present et valide.
- `NOTICE` racine a jour.
- Composants tiers listes dans `docs/THIRD_PARTY_LICENSE_AUDIT.md`.
- Statut de redistribution explicite pour modeles/tokenizer/datasets.

4. Secrets et credentials
- Aucun secret hardcode (API key, token, password, private key).
- Integrations externes via variables d'environnement uniquement.
- Lancer le scan preflight avant publication.

5. Reproductibilite et qualite
- Chemin de smoke test principal documente et reproductible.
- Commandes docs copiables directement.
- Artefacts de build exclus du controle de version.

6. Discipline de perimetre public
- Le depot reste concentre sur le runtime/prototype baremetal.
- Le contenu prive hors perimetre n'est pas expose dans la documentation publique.
- Lancer le controle de perimetre: `./scripts/public-scope-check.ps1`.

## FR - Flux de publication recommande

1. Lancer le preflight local:
- `./scripts/public-preflight.ps1`

2. Corriger tous les checks rouges.

3. Relancer les smoke tests cibles.

4. Preparer les release notes:
- Portee, limites connues, notes de migration.

5. Tag + publication uniquement apres validation legale.

6. Lancer le controle de perimetre en mode bloquant avant push final:
- `./scripts/public-scope-check.ps1 -Strict`
