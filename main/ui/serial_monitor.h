#ifndef SERIAL_MONITOR_H
#define SERIAL_MONITOR_H

#include <stddef.h>
#include <windows.h>

#define SERIAL_MONITOR_MAX_ENTRIES 2000
#define SERIAL_MONITOR_MAX_TEXT_LENGTH 512

/**
 * @brief Classifica visualmente uma linha recebida pela serial.
 */
typedef enum SerialMonitorHighlight {
    SERIAL_MONITOR_HIGHLIGHT_NONE = 0,
    SERIAL_MONITOR_HIGHLIGHT_INFO,
    SERIAL_MONITOR_HIGHLIGHT_WARNING,
    SERIAL_MONITOR_HIGHLIGHT_ERROR
} SerialMonitorHighlight;

/**
 * @brief Representa uma linha capturada pelo monitor serial web.
 */
typedef struct SerialMonitorEntry {
    unsigned long id;
    char timestamp[16];
    SerialMonitorHighlight highlight;
    char text[SERIAL_MONITOR_MAX_TEXT_LENGTH];
} SerialMonitorEntry;

/**
 * @brief Resume o estado atual do buffer circular do monitor serial.
 */
typedef struct SerialMonitorSnapshot {
    unsigned long earliest_id;
    unsigned long latest_id;
    size_t count;
} SerialMonitorSnapshot;

/**
 * @brief Mantem o buffer circular das ultimas linhas recebidas pela serial.
 */
typedef struct SerialMonitorState {
    CRITICAL_SECTION lock;
    SerialMonitorEntry entries[SERIAL_MONITOR_MAX_ENTRIES];
    unsigned long next_id;
    size_t start;
    size_t count;
} SerialMonitorState;

/**
 * @brief Inicializa o buffer circular do monitor serial.
 *
 * @param state Estado do monitor a ser preparado.
 */
void serial_monitor_initialize(SerialMonitorState *state);

/**
 * @brief Libera os recursos internos do monitor serial.
 *
 * @param state Estado do monitor a ser destruido.
 */
void serial_monitor_destroy(SerialMonitorState *state);

/**
 * @brief Limpa todas as linhas armazenadas no monitor serial.
 *
 * @param state Estado do monitor a ser limpo.
 */
void serial_monitor_clear(SerialMonitorState *state);

/**
 * @brief Armazena uma nova linha recebida no buffer circular.
 *
 * @param state Estado do monitor.
 * @param line Texto bruto recebido pela serial.
 */
void serial_monitor_push_line(SerialMonitorState *state, const char *line);

/**
 * @brief Retorna um resumo do intervalo de linhas disponiveis no buffer.
 *
 * @param state Estado do monitor.
 * @param snapshot Estrutura de saida preenchida com o resumo atual.
 */
void serial_monitor_get_snapshot(SerialMonitorState *state, SerialMonitorSnapshot *snapshot);

/**
 * @brief Copia linhas mais novas que um identificador informado.
 *
 * @param state Estado do monitor.
 * @param after_id Ultimo identificador ja consumido pelo cliente.
 * @param destination Vetor de saida para as linhas copiadas.
 * @param max_entries Quantidade maxima de entradas que cabem em destination.
 * @param next_after Recebe o ultimo identificador realmente copiado.
 * @param latest_id Recebe o identificador mais novo disponivel no buffer.
 * @return size_t Quantidade de linhas copiadas.
 */
size_t serial_monitor_copy_since(SerialMonitorState *state,
                                 unsigned long after_id,
                                 SerialMonitorEntry *destination,
                                 size_t max_entries,
                                 unsigned long *next_after,
                                 unsigned long *latest_id);

#endif
