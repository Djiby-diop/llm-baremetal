#include <efi.h>
#include <efilib.h>
#include "../ssm/soma_uart.h"

/**
 * llmk_telepathy_query_djibion — Envoie une requête "télépathique" à Djibion via UART.
 * 
 * Cette fonction envoie le prompt marqué par [DJIBION_REQ] sur le port série,
 * puis attend et affiche la réponse du pont Python.
 */
void llmk_telepathy_query_djibion(const char *prompt) {
    if (!prompt) return;

    Print(L"[Telepathy] Sending request to Djibion...\r\n");

    // 1. Initialiser l'UART au cas où
    soma_uart_init();

    // 2. Envoyer le marqueur de requête et le prompt
    // On n'utilise pas soma_uart_puts car elle ajoute \r\n à la fin,
    // ce qui pourrait perturber le parsing du pont Python.
    const char *req_pfx = "[DJIBION_REQ] ";
    while (*req_pfx) soma_uart_putc(*req_pfx++);
    
    const char *p = prompt;
    while (*p) soma_uart_putc(*p++);
    
    soma_uart_putc('\n'); // Fin du message pour le pont

    Print(L"Djibion: ");

    // 3. Attendre et filtrer le marqueur de début [DJIBION_RES]
    const char *start_marker = "[DJIBION_RES]";
    int start_ptr = 0;

    while (start_marker[start_ptr]) {
        char c = soma_uart_getc();
        if (c == start_marker[start_ptr]) {
            start_ptr++;
        } else {
            // On réinitialise si la séquence est rompue
            start_ptr = (c == start_marker[0]) ? 1 : 0;
        }
    }

    // 4. Recevoir et afficher le contenu jusqu'au marqueur de fin [DJIBION_END]
    const char *end_marker = "[DJIBION_END]";
    int end_ptr = 0;
    
    while (1) {
        char c = soma_uart_getc();
        
        // Est-ce le début du marqueur de fin ?
        if (c == end_marker[end_ptr]) {
            end_ptr++;
            if (end_marker[end_ptr] == '\0') break; // Fin détectée !
        } else {
            // Si on avait un début de marqueur mais que ça a échoué,
            // on affiche les caractères qu'on avait mis de côté.
            if (end_ptr > 0) {
                for (int i = 0; i < end_ptr; i++) {
                    Print(L"%c", (CHAR16)end_marker[i]);
                }
                end_ptr = (c == end_marker[0]) ? 1 : 0;
                if (end_ptr == 0) {
                    if (c == '\n') Print(L"\r\n");
                    else Print(L"%c", (CHAR16)c);
                }
            } else {
                if (c == '\n') Print(L"\r\n");
                else Print(L"%c", (CHAR16)c);
            }
        }
    }

    Print(L"\r\n[Telepathy] Link closed.\r\n");
}
