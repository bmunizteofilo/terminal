#define _CRT_SECURE_NO_WARNINGS

#include "serial_monitor.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Formata o horario local atual para exibicao na UI web.
 *
 * @param buffer Buffer de saida.
 * @param buffer_size Tamanho do buffer de saida.
 */
static void serial_monitor_format_timestamp(char *buffer, size_t buffer_size)
{
    SYSTEMTIME local_time;

    GetLocalTime(&local_time);
    snprintf(buffer,
             buffer_size,
             "%02u:%02u:%02u.%03u",
             (unsigned int)local_time.wHour,
             (unsigned int)local_time.wMinute,
             (unsigned int)local_time.wSecond,
             (unsigned int)local_time.wMilliseconds);
}

/**
 * @brief Detecta a categoria visual mais apropriada para uma linha serial.
 *
 * @param line Texto bruto recebido.
 * @return SerialMonitorHighlight Categoria visual detectada.
 */
static SerialMonitorHighlight serial_monitor_detect_highlight(const char *line)
{
    const char *cursor = line;

    if (line == NULL || line[0] == '\0') {
        return SERIAL_MONITOR_HIGHLIGHT_NONE;
    }

    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if ((cursor[0] == 'E' || cursor[0] == 'e') && cursor[1] == ' ' && cursor[2] == '(') {
        return SERIAL_MONITOR_HIGHLIGHT_ERROR;
    }

    if ((cursor[0] == 'W' || cursor[0] == 'w') && cursor[1] == ' ' && cursor[2] == '(') {
        return SERIAL_MONITOR_HIGHLIGHT_WARNING;
    }

    if ((cursor[0] == 'I' || cursor[0] == 'i') && cursor[1] == ' ' && cursor[2] == '(') {
        return SERIAL_MONITOR_HIGHLIGHT_INFO;
    }

    if (strstr(cursor, "[erro]") != NULL || strstr(cursor, "ERROR") != NULL || strstr(cursor, "error") != NULL) {
        return SERIAL_MONITOR_HIGHLIGHT_ERROR;
    }

    if (strstr(cursor, "warning") != NULL || strstr(cursor, "Warning") != NULL || strstr(cursor, "WARN") != NULL) {
        return SERIAL_MONITOR_HIGHLIGHT_WARNING;
    }

    if (strstr(cursor, "info") != NULL || strstr(cursor, "Info") != NULL) {
        return SERIAL_MONITOR_HIGHLIGHT_INFO;
    }

    return SERIAL_MONITOR_HIGHLIGHT_NONE;
}

/**
 * @brief Inicializa o estado do monitor serial em memoria.
 *
 * @param state Estrutura de estado do monitor a ser inicializada.
 */
void serial_monitor_initialize(SerialMonitorState *state)
{
    ZeroMemory(state, sizeof(*state));
    InitializeCriticalSection(&state->lock);
    state->next_id = 1UL;
}

/**
 * @brief Libera os recursos internos usados pelo monitor serial.
 *
 * @param state Estrutura de estado do monitor a ser destruida.
 */
void serial_monitor_destroy(SerialMonitorState *state)
{
    DeleteCriticalSection(&state->lock);
}

/**
 * @brief Limpa todas as entradas armazenadas no monitor serial.
 *
 * @param state Estrutura de estado do monitor.
 */
void serial_monitor_clear(SerialMonitorState *state)
{
    EnterCriticalSection(&state->lock);
    state->start = 0U;
    state->count = 0U;
    state->next_id = 1UL;
    LeaveCriticalSection(&state->lock);
}

/**
 * @brief Adiciona uma nova linha ao buffer circular do monitor serial.
 *
 * @param state Estrutura de estado do monitor.
 * @param line Linha de texto recebida a ser armazenada.
 */
void serial_monitor_push_line(SerialMonitorState *state, const char *line)
{
    size_t slot_index;
    SerialMonitorEntry *entry;

    if (line == NULL || line[0] == '\0') {
        return;
    }

    EnterCriticalSection(&state->lock);

    if (state->count < SERIAL_MONITOR_MAX_ENTRIES) {
        slot_index = (state->start + state->count) % SERIAL_MONITOR_MAX_ENTRIES;
        state->count++;
    } else {
        slot_index = state->start;
        state->start = (state->start + 1U) % SERIAL_MONITOR_MAX_ENTRIES;
    }

    entry = &state->entries[slot_index];
    entry->id = state->next_id++;
    serial_monitor_format_timestamp(entry->timestamp, sizeof(entry->timestamp));
    entry->highlight = serial_monitor_detect_highlight(line);
    snprintf(entry->text, sizeof(entry->text), "%s", line);

    LeaveCriticalSection(&state->lock);
}

/**
 * @brief Retorna um snapshot resumido do estado atual do monitor serial.
 *
 * @param state Estrutura de estado do monitor.
 * @param snapshot Estrutura de saida que recebera os dados resumidos.
 */
void serial_monitor_get_snapshot(SerialMonitorState *state, SerialMonitorSnapshot *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    EnterCriticalSection(&state->lock);

    snapshot->count = state->count;
    if (state->count == 0U) {
        snapshot->earliest_id = 0UL;
        snapshot->latest_id = 0UL;
    } else {
        snapshot->earliest_id = state->entries[state->start].id;
        snapshot->latest_id = state->entries[(state->start + state->count - 1U) % SERIAL_MONITOR_MAX_ENTRIES].id;
    }

    LeaveCriticalSection(&state->lock);
}

/**
 * @brief Copia entradas do monitor a partir de um identificador informado.
 *
 * @param state Estrutura de estado do monitor.
 * @param after_id Ultimo identificador ja consumido pelo chamador.
 * @param destination Vetor de destino para as entradas copiadas.
 * @param max_entries Quantidade maxima de entradas que podem ser copiadas.
 * @param next_after Ponteiro opcional para retornar o ultimo id copiado.
 * @param latest_id Ponteiro opcional para retornar o id mais recente disponivel.
 * @return size_t Quantidade de entradas copiadas para o destino.
 */
size_t serial_monitor_copy_since(SerialMonitorState *state,
                                 unsigned long after_id,
                                 SerialMonitorEntry *destination,
                                 size_t max_entries,
                                 unsigned long *next_after,
                                 unsigned long *latest_id)
{
    size_t index;
    size_t copied = 0U;
    unsigned long last_copied_id = after_id;
    unsigned long newest_available_id = 0UL;

    if (next_after != NULL) {
        *next_after = after_id;
    }
    if (latest_id != NULL) {
        *latest_id = 0UL;
    }
    if (destination == NULL || max_entries == 0U) {
        return 0U;
    }

    EnterCriticalSection(&state->lock);

    if (state->count > 0U) {
        newest_available_id = state->entries[(state->start + state->count - 1U) % SERIAL_MONITOR_MAX_ENTRIES].id;
    }

    for (index = 0U; index < state->count && copied < max_entries; index++) {
        const SerialMonitorEntry *entry = &state->entries[(state->start + index) % SERIAL_MONITOR_MAX_ENTRIES];
        if (entry->id <= after_id) {
            continue;
        }

        destination[copied++] = *entry;
        last_copied_id = entry->id;
    }

    LeaveCriticalSection(&state->lock);

    if (next_after != NULL) {
        *next_after = last_copied_id;
    }
    if (latest_id != NULL) {
        *latest_id = newest_available_id;
    }

    return copied;
}
