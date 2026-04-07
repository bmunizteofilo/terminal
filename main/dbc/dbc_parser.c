#define _CRT_SECURE_NO_WARNINGS

#include "dbc_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Remove quebras de linha do final do texto informado.
 *
 * @param text Texto a ser ajustado em memoria.
 */
static void dbc_trim_newline(char *text)
{
    size_t length = strlen(text);

    while (length > 0U && (text[length - 1U] == '\n' || text[length - 1U] == '\r')) {
        text[length - 1U] = '\0';
        length--;
    }
}

/**
 * @brief Remove espacos em branco do inicio e do fim do texto.
 *
 * @param text Texto a ser ajustado.
 * @return char* Ponteiro para o inicio util do texto.
 */
static char *dbc_trim_whitespace(char *text)
{
    char *start = text;
    char *end;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (*start == '\0') {
        return start;
    }

    end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return start;
}

/**
 * @brief Localiza uma mensagem DBC pelo identificador CAN.
 *
 * @param database Banco DBC carregado.
 * @param id Identificador CAN da mensagem.
 * @return DbcMessage* Ponteiro para a mensagem encontrada ou NULL.
 */
static DbcMessage *dbc_find_message(DbcDatabase *database, unsigned int id)
{
    int index;

    for (index = 0; index < database->message_count; index++) {
        if (database->messages[index].id == id) {
            return &database->messages[index];
        }
    }

    return NULL;
}

/**
 * @brief Localiza um sinal DBC pelo nome dentro de uma mensagem.
 *
 * @param message Mensagem alvo.
 * @param name Nome do sinal.
 * @return DbcSignal* Ponteiro para o sinal encontrado ou NULL.
 */
static DbcSignal *dbc_find_signal(DbcMessage *message, const char *name)
{
    int index;

    for (index = 0; index < message->signal_count; index++) {
        if (strcmp(message->signals[index].name, name) == 0) {
            return &message->signals[index];
        }
    }

    return NULL;
}

/**
 * @brief Realiza a extensao de sinal para valores assinados de ate 63 bits.
 *
 * @param value Valor bruto extraido do payload.
 * @param bit_length Quantidade de bits validos do sinal.
 * @return long long Valor convertido com extensao de sinal quando necessario.
 */
static long long dbc_sign_extend(unsigned long long value, unsigned int bit_length)
{
    unsigned long long sign_mask;
    unsigned long long extend_mask;

    if (bit_length == 0U || bit_length >= 64U) {
        return (long long)value;
    }

    sign_mask = 1ULL << (bit_length - 1U);
    if ((value & sign_mask) == 0ULL) {
        return (long long)value;
    }

    extend_mask = ~((1ULL << bit_length) - 1ULL);
    return (long long)(value | extend_mask);
}

/**
 * @brief Faz o parse da linha VERSION do arquivo DBC.
 *
 * @param line Linha a ser interpretada.
 * @param database Banco DBC destino.
 */
static void dbc_parse_version(char *line, DbcDatabase *database)
{
    char *first_quote = strchr(line, '"');
    char *last_quote = strrchr(line, '"');

    if (first_quote == NULL || last_quote == NULL || first_quote == last_quote) {
        return;
    }

    *last_quote = '\0';
    snprintf(database->version, sizeof(database->version), "%s", first_quote + 1);
}

/**
 * @brief Faz o parse de uma linha BO_ e adiciona a mensagem ao banco.
 *
 * @param line Linha BO_ do arquivo DBC.
 * @param database Banco DBC destino.
 */
static void dbc_parse_message(char *line, DbcDatabase *database)
{
    DbcMessage *message;
    unsigned int id;
    unsigned int dlc;
    char name[DBC_NAME_SIZE];
    char transmitter[DBC_NODE_SIZE];

    if (database->message_count >= DBC_MAX_MESSAGES) {
        return;
    }

    if (sscanf(line, "BO_ %u %63[^:]: %u %31s", &id, name, &dlc, transmitter) != 4) {
        return;
    }

    message = &database->messages[database->message_count++];
    ZeroMemory(message, sizeof(*message));
    message->id = id;
    message->dlc = dlc;
    snprintf(message->name, sizeof(message->name), "%s", name);
    snprintf(message->transmitter, sizeof(message->transmitter), "%s", transmitter);
}

/**
 * @brief Faz o parse de uma linha SG_ pertencente a mensagem corrente.
 *
 * @param line Linha SG_ do arquivo DBC.
 * @param message Mensagem atualmente em contexto.
 * @param database Banco DBC destino para contadores agregados.
 */
static void dbc_parse_signal(char *line, DbcMessage *message, DbcDatabase *database)
{
    DbcSignal *signal;
    char receiver[DBC_NODE_SIZE];
    char *unit_start;
    char *unit_end;
    char sign_char;
    unsigned int start_bit;
    unsigned int bit_length;
    unsigned int endian_mode;

    if (message == NULL || message->signal_count >= DBC_MAX_SIGNALS_PER_MESSAGE) {
        return;
    }

    signal = &message->signals[message->signal_count];
    ZeroMemory(signal, sizeof(*signal));

    if (sscanf(line,
               "SG_ %63s : %u|%u@%u%c (%lf,%lf) [%lf|%lf]",
               signal->name,
               &start_bit,
               &bit_length,
               &endian_mode,
               &sign_char,
               &signal->factor,
               &signal->offset,
               &signal->minimum,
               &signal->maximum) != 9) {
        return;
    }

    unit_start = strchr(line, '"');
    if (unit_start == NULL) {
        return;
    }

    unit_end = strchr(unit_start + 1, '"');
    if (unit_end == NULL) {
        return;
    }

    if (unit_end > (unit_start + 1)) {
        size_t unit_length = (size_t)(unit_end - (unit_start + 1));
        if (unit_length >= sizeof(signal->unit)) {
            unit_length = sizeof(signal->unit) - 1U;
        }

        memcpy(signal->unit, unit_start + 1, unit_length);
        signal->unit[unit_length] = '\0';
    } else {
        signal->unit[0] = '\0';
    }

    if (sscanf(unit_end + 1, " %31s", receiver) != 1) {
        return;
    }

    signal->start_bit = start_bit;
    signal->bit_length = bit_length;
    signal->is_little_endian = endian_mode == 1U;
    signal->is_signed = sign_char == '-';
    snprintf(signal->receiver, sizeof(signal->receiver), "%s", receiver);

    message->signal_count++;
    database->total_signal_count++;
}

/**
 * @brief Faz o parse de uma linha VAL_ e associa enums ao sinal correspondente.
 *
 * @param line Linha VAL_ do arquivo DBC.
 * @param database Banco DBC carregado.
 */
static void dbc_parse_value_description(char *line, DbcDatabase *database)
{
    unsigned int message_id;
    char signal_name[DBC_NAME_SIZE];
    char *cursor;
    DbcMessage *message;
    DbcSignal *signal;

    if (sscanf(line, "VAL_ %u %63s", &message_id, signal_name) != 2) {
        return;
    }

    message = dbc_find_message(database, message_id);
    if (message == NULL) {
        return;
    }

    signal = dbc_find_signal(message, signal_name);
    if (signal == NULL) {
        return;
    }

    cursor = strchr(line, ' ');
    if (cursor == NULL) {
        return;
    }
    cursor = strchr(cursor + 1, ' ');
    if (cursor == NULL) {
        return;
    }
    cursor = strchr(cursor + 1, ' ');
    if (cursor == NULL) {
        return;
    }
    cursor = dbc_trim_whitespace(cursor);

    while (*cursor != '\0' && *cursor != ';' && signal->value_count < DBC_MAX_ENUM_VALUES) {
        int value;
        char text[DBC_NAME_SIZE];
        int consumed = 0;

        if (sscanf(cursor, "%d \"%63[^\"]\"%n", &value, text, &consumed) != 2) {
            break;
        }

        signal->values[signal->value_count].value = value;
        snprintf(signal->values[signal->value_count].text,
                 sizeof(signal->values[signal->value_count].text),
                 "%s",
                 text);
        signal->value_count++;
        cursor = dbc_trim_whitespace(cursor + consumed);
    }
}

void dbc_reset_database(DbcDatabase *database)
{
    ZeroMemory(database, sizeof(*database));
}

int dbc_load_file(const char *path, DbcDatabase *database)
{
    FILE *file;
    char line[512];
    DbcMessage *current_message = NULL;

    if (path == NULL || database == NULL) {
        return 0;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }

    dbc_reset_database(database);
    snprintf(database->path, sizeof(database->path), "%s", path);

    while (fgets(line, sizeof(line), file) != NULL) {
        char *trimmed;

        dbc_trim_newline(line);
        trimmed = dbc_trim_whitespace(line);

        if (trimmed[0] == '\0') {
            continue;
        }

        if (strncmp(trimmed, "VERSION", 7) == 0) {
            dbc_parse_version(trimmed, database);
            continue;
        }

        if (strncmp(trimmed, "BO_", 3) == 0) {
            dbc_parse_message(trimmed, database);
            current_message = database->message_count > 0 ? &database->messages[database->message_count - 1] : NULL;
            continue;
        }

        if (strncmp(trimmed, "SG_", 3) == 0) {
            dbc_parse_signal(trimmed, current_message, database);
            continue;
        }

        if (strncmp(trimmed, "VAL_", 4) == 0) {
            dbc_parse_value_description(trimmed, database);
        }
    }

    fclose(file);
    database->loaded = database->message_count > 0;
    return database->loaded;
}

const DbcMessage *dbc_find_message_by_id(const DbcDatabase *database, unsigned int id)
{
    int index;

    if (database == NULL) {
        return NULL;
    }

    for (index = 0; index < database->message_count; index++) {
        if (database->messages[index].id == id) {
            return &database->messages[index];
        }
    }

    return NULL;
}

const DbcMessage *dbc_find_message_by_name(const DbcDatabase *database, const char *name)
{
    int index;

    if (database == NULL || name == NULL) {
        return NULL;
    }

    for (index = 0; index < database->message_count; index++) {
        if (strcmp(database->messages[index].name, name) == 0) {
            return &database->messages[index];
        }
    }

    return NULL;
}

const DbcSignal *dbc_find_signal_by_name(const DbcMessage *message, const char *name)
{
    int index;

    if (message == NULL || name == NULL) {
        return NULL;
    }

    for (index = 0; index < message->signal_count; index++) {
        if (strcmp(message->signals[index].name, name) == 0) {
            return &message->signals[index];
        }
    }

    return NULL;
}

const char *dbc_find_value_text(const DbcSignal *signal, int value)
{
    int index;

    if (signal == NULL) {
        return NULL;
    }

    for (index = 0; index < signal->value_count; index++) {
        if (signal->values[index].value == value) {
            return signal->values[index].text;
        }
    }

    return NULL;
}

int dbc_decode_signal(const DbcSignal *signal,
                      const unsigned char *data,
                      unsigned int data_length,
                      long long *raw_value,
                      double *physical_value)
{
    unsigned long long packed_data = 0ULL;
    unsigned long long extracted_value;
    unsigned int index;
    long long signed_value;

    if (signal == NULL || data == NULL) {
        return 0;
    }

    if (!signal->is_little_endian || signal->bit_length == 0U || signal->bit_length > 63U) {
        return 0;
    }

    if ((signal->start_bit + signal->bit_length) > (data_length * 8U)) {
        return 0;
    }

    for (index = 0; index < data_length && index < 8U; index++) {
        packed_data |= ((unsigned long long)data[index]) << (index * 8U);
    }

    extracted_value = (packed_data >> signal->start_bit) & ((1ULL << signal->bit_length) - 1ULL);
    signed_value = signal->is_signed ? dbc_sign_extend(extracted_value, signal->bit_length) : (long long)extracted_value;

    if (raw_value != NULL) {
        *raw_value = signed_value;
    }

    if (physical_value != NULL) {
        *physical_value = ((double)signed_value * signal->factor) + signal->offset;
    }

    return 1;
}
