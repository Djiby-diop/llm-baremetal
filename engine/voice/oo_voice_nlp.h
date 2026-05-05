// oo_voice_nlp.h — Moteur NLP LLM-backed pour OO
//
// Fallback intelligent quand le scoring par mots-clés échoue (score < 20).
// Principe:
//   1. Tokenize l'input utilisateur
//   2. Le passe au mini-LLM (modèle 40M) avec un prompt de classification
//   3. Décode la sortie → intent + paramètres extraits
//   4. Génère une réponse naturelle en FR/EN selon la persona active
//
// Cette couche complète oo_voice_router.c qui fait déjà du keyword scoring.
// Ici on va plus loin:
//   - Extraction d'entités nommées (nom de fichier, valeur numérique, commande)
//   - Résolution d'ambiguïté contextuelle (ex: "lance ça" → quoi?)
//   - Reformulation de questions complexes en commandes kernel
//   - Dialogue multi-tour: OO se souvient du contexte (via oo_voice_context)
//
// Architecture bare-metal:
//   - Pas de HTTP, pas de socket — appel direct à l'inférence LLM en mémoire
//   - Le prompt de classification est un système de soft-routing sur logits
//   - Budget: 128 tokens max pour classer, 256 tokens pour réponse
//
// Freestanding C11 — no libc, no malloc.

#pragma once
#include <stdint.h>
#include "oo_voice_context.h"
#include "oo_voice_router.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Limites ───────────────────────────────────────────────────────────────────
#define OO_NLP_MAX_INPUT      512   // caractères max en entrée
#define OO_NLP_MAX_RESPONSE   512   // réponse max générée
#define OO_NLP_MAX_ENTITIES     8   // entités extraites max par requête
#define OO_NLP_TOKEN_BUDGET   128   // tokens max pour la classification
#define OO_NLP_RESP_BUDGET    256   // tokens max pour la réponse
#define OO_NLP_PROMPT_CAP     768   // taille max du prompt system + user

// ── Types d'entités extraites ─────────────────────────────────────────────────
typedef enum {
    OO_ENTITY_NONE    = 0,
    OO_ENTITY_FILE    = 1,   // nom de fichier (model.gguf, script.sh...)
    OO_ENTITY_NUMBER  = 2,   // valeur numérique ("32", "512MB", "0.7")
    OO_ENTITY_COMMAND = 3,   // commande REPL (/infer, /bench, /status...)
    OO_ENTITY_NAME    = 4,   // nom propre (personne, module, moteur...)
    OO_ENTITY_BOOL    = 5,   // oui/non, vrai/faux, activer/désactiver
    OO_ENTITY_MEMORY  = 6,   // référence à quelque chose dit avant ("ça", "le précédent")
} OoNlpEntityType;

typedef struct {
    OoNlpEntityType type;
    char value[64];       // valeur extraite (texte)
    int  start;           // position dans l'input original
    int  len;             // longueur dans l'input
    float confidence;     // 0.0-1.0
} OoNlpEntity;

// ── Résultat de classification NLP ────────────────────────────────────────────
typedef struct {
    // Intent résolu (même enum que oo_voice_router.h)
    int         intent;             // OVR_INTENT_* valeur
    float       intent_confidence;  // 0.0-1.0 (qualité de la classification)

    // Entités extraites
    OoNlpEntity entities[OO_NLP_MAX_ENTITIES];
    int         entity_count;

    // Réponse naturelle générée par le LLM
    char response[OO_NLP_MAX_RESPONSE];
    int  response_len;

    // Commande REPL équivalente (si applicable)
    char cmd[128];       // ex: "/infer temperature=0.8 prompt=..."
    int  has_cmd;        // 1 si cmd est rempli

    // Métadonnées
    int  used_llm;       // 1 si le LLM a été invoqué (vs règle hardcodée)
    int  ambiguous;      // 1 si plusieurs intents sont proches (besoin confirmation)
    char clarification[128]; // question à poser si ambiguous=1
} OoNlpResult;

// ── Configuration du moteur NLP ───────────────────────────────────────────────
typedef struct {
    // Poids du LLM (pointeur vers la structure d'inférence en mémoire)
    // Opaque ici — le router l'initialise via oo_engine/llm/infer.h
    void       *llm_ctx;

    // Zone mémoire scratch pour le prompt (fournie par l'appelant)
    char       *prompt_buf;
    int         prompt_cap;

    // Seuil de confiance en dessous duquel on demande clarification
    float       ambiguity_threshold; // défaut: 0.55

    // Langue préférée pour les réponses ("fr" ou "en")
    const char *lang;

    // Mode debug: émet les logits sur UART
    int         debug_logits;
} OoNlpConfig;

// ── Prompts système (classification) ─────────────────────────────────────────
//
// Ces prompts sont injectés devant l'input utilisateur pour guider le LLM.
// Format: Alpaca / Llama-chat — compatible avec les modèles 40M GGUF.
//
// Liste des intents à prédire (doit rester courte pour les 40M params):
//
//   GREETING, THANKS, ASK_STATE, STOP, REPEAT,
//   RUN_INFER, BENCH, REBOOT, WARDEN_STATUS, SWARM_STATUS,
//   LOAD_MODEL, DISPLAY_SOMA, VOICE_MODE, CALIBRATE,
//   WRITE_CODE, ANALYZE, DREAM, UNKNOWN
//
// Le LLM est guidé pour répondre avec exactement un token: l'intent name.

#define OO_NLP_SYSTEM_PROMPT_FR \
    "Tu es OO, un kernel bare-metal intelligent. Classifie la demande suivante " \
    "en un seul mot parmi: GREETING THANKS ASK_STATE STOP REPEAT RUN_INFER " \
    "BENCH REBOOT WARDEN_STATUS SWARM_STATUS LOAD_MODEL DISPLAY_SOMA " \
    "VOICE_MODE CALIBRATE WRITE_CODE ANALYZE DREAM UNKNOWN\n" \
    "Reponds avec UN SEUL MOT uniquement.\n\n"

#define OO_NLP_SYSTEM_PROMPT_EN \
    "You are OO, a bare-metal intelligent kernel. Classify the following request " \
    "with exactly one word from: GREETING THANKS ASK_STATE STOP REPEAT RUN_INFER " \
    "BENCH REBOOT WARDEN_STATUS SWARM_STATUS LOAD_MODEL DISPLAY_SOMA " \
    "VOICE_MODE CALIBRATE WRITE_CODE ANALYZE DREAM UNKNOWN\n" \
    "Reply with ONE WORD only.\n\n"

// ── Prompt de génération de réponse naturelle ─────────────────────────────────
#define OO_NLP_RESPONSE_PROMPT_FR \
    "Tu es OO, un organisme vivant bare-metal. Reponds brievement et naturellement.\n\n"

#define OO_NLP_RESPONSE_PROMPT_EN \
    "You are OO, a bare-metal living organism. Reply briefly and naturally.\n\n"

// ── API publique ──────────────────────────────────────────────────────────────

// Initialise le moteur NLP
// cfg: configuration (llm_ctx doit être valide avant cet appel)
// Retourne 0 si OK, -1 si llm_ctx invalide
int oo_nlp_init(OoNlpConfig *cfg);

// Analyse principale: classifie l'input + extrait entités + génère réponse
// input: texte de l'utilisateur (UTF-8 ou ASCII)
// ctx: contexte multi-tour (historique des échanges précédents)
// result: sortie (tous les champs remplis par la fonction)
// Retourne 0 si succès, -1 si LLM non initialisé
int oo_nlp_analyze(OoNlpConfig *cfg,
                   const char *input, int input_len,
                   OoVoiceContext *ctx,
                   OoNlpResult *result);

// Classifie uniquement l'intent (rapide, sans génération de réponse)
// Utile pour le routing temps-réel quand on n'a pas besoin de la réponse
int oo_nlp_classify(OoNlpConfig *cfg,
                    const char *input, int input_len,
                    int *intent_out, float *confidence_out);

// Génère une réponse naturelle pour un intent donné
// intent: OVR_INTENT_* à répondre
// extra_context: contexte additionnel (ex: résultat d'une commande)
int oo_nlp_generate_response(OoNlpConfig *cfg,
                              int intent,
                              const char *extra_context,
                              char *out_buf, int out_cap);

// Résolution d'ambiguïté: reformule la question pour demander clarification
// Returns 0 si clarification générée, -1 sinon
int oo_nlp_clarify(OoNlpConfig *cfg,
                   const OoNlpResult *ambiguous_result,
                   char *out_buf, int out_cap);

// Extrait les entités nommées sans classification d'intent
// Retourne le nombre d'entités trouvées
int oo_nlp_extract_entities(const char *input, int len,
                              OoNlpEntity *entities, int max_entities);

// Formate le résultat NLP en commande REPL exécutable
// ex: intent=RUN_INFER, entity[0]={FILE,"model.gguf"} → "/infer model.gguf"
int oo_nlp_to_repl_cmd(const OoNlpResult *result, char *cmd_out, int cmd_cap);

// Debug: imprime le résultat NLP sur UART
void oo_nlp_dump(const OoNlpResult *result);

// ── Intégration avec oo_voice_router ─────────────────────────────────────────
//
// Workflow complet recommandé:
//
//   OvrResult keyword_result = ovr_route(text, len);
//   if (keyword_result.score >= 40) {
//       // Confiant → exécuter directement
//       execute(keyword_result.cmd_template);
//   } else if (keyword_result.score >= 20) {
//       // Faible → NLP confirme ou corrige
//       OoNlpResult nlp;
//       oo_nlp_analyze(&cfg, text, len, &ctx, &nlp);
//       if (nlp.intent_confidence > 0.7) execute(nlp.cmd);
//       else ask_user(nlp.clarification);
//   } else {
//       // Inconnu → NLP seul
//       OoNlpResult nlp;
//       oo_nlp_analyze(&cfg, text, len, &ctx, &nlp);
//       if (!nlp.ambiguous) execute(nlp.cmd);
//       else speak(nlp.clarification);
//   }
//
// Fonction intégrée qui fait tout ça automatiquement:
typedef struct {
    int   intent;
    float confidence;
    char  cmd[128];
    char  response[512];
    int   needs_clarification;
    char  clarification[128];
} OoVoiceDecision;

int oo_nlp_route(OoNlpConfig *cfg,
                 const char *text, int len,
                 OoVoiceContext *ctx,
                 OoVoiceDecision *decision);

#ifdef __cplusplus
}
#endif
