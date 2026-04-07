#define _CRT_SECURE_NO_WARNINGS

#include "can_slcan.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAN_DEFAULT_LOG_FILE_PATH "bmt_can.log"

/**
 * @brief Remove espacos em branco do inicio e do fim de uma string.
 *
 * @param text String a ser ajustada em memoria.
 * @return char* Ponteiro para o inicio util da string.
 */
static char *can_trim_whitespace(char *text)
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
 * @brief Compara duas strings sem diferenciar letras maiusculas e minusculas.
 *
 * @param left Primeira string.
 * @param right Segunda string.
 * @return int Retorna 1 quando forem equivalentes, caso contrario 0.
 */
static int can_equals_ignore_case(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (toupper((unsigned char)*left) != toupper((unsigned char)*right)) {
            return 0;
        }

        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

/**
 * @brief Envia um comando textual ao adaptador SLCAN.
 *
 * @param state Estado global do backend CAN.
 * @param command Texto ASCII do comando.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int can_slcan_write_command(CanState *state, const char *command)
{
    DWORD bytes_written = 0;
    size_t length = strlen(command);

    if (state->handle == NULL) {
        return 0;
    }

    if (!WriteFile(state->handle, command, (DWORD)length, &bytes_written, NULL) || bytes_written != length) {
        return 0;
    }

    return 1;
}

/**
 * @brief Mapeia um bitrate CAN para o codigo SLCAN correspondente.
 *
 * @param bitrate Bitrate CAN em bits por segundo.
 * @return char Codigo SLCAN associado ou '\0' quando nao suportado.
 */
static char can_get_slcan_bitrate_code(unsigned int bitrate)
{
    switch (bitrate) {
    case 10000:
        return '0';
    case 20000:
        return '1';
    case 50000:
        return '2';
    case 100000:
        return '3';
    case 125000:
        return '4';
    case 250000:
        return '5';
    case 500000:
        return '6';
    case 800000:
        return '7';
    case 1000000:
        return '8';
    default:
        return '\0';
    }
}

/**
 * @brief Converte um caractere hexadecimal para o seu valor numerico.
 *
 * @param value Caractere hexadecimal ASCII.
 * @return int Valor convertido ou -1 em caso de caractere invalido.
 */
static int can_hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }

    if (value >= 'A' && value <= 'F') {
        return 10 + (value - 'A');
    }

    if (value >= 'a' && value <= 'f') {
        return 10 + (value - 'a');
    }

    return -1;
}

/**
 * @brief Interpreta um bloco hexadecimal ASCII como inteiro sem sinal.
 *
 * @param text Texto de origem.
 * @param length Quantidade de caracteres hexadecimais a processar.
 * @param value Ponteiro para receber o valor convertido.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int can_parse_hex_slice(const char *text, size_t length, unsigned int *value)
{
    size_t index;
    unsigned int result = 0;

    if (text == NULL || value == NULL || length == 0U) {
        return 0;
    }

    for (index = 0; index < length; index++) {
        int nibble = can_hex_value(text[index]);

        if (nibble < 0) {
            return 0;
        }

        result = (result << 4) | (unsigned int)nibble;
    }

    *value = result;
    return 1;
}

/**
 * @brief Configura a porta serial do adaptador CAN em 115200 8N1.
 *
 * @param handle Handle da porta COM aberta.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int can_configure_slcan_port(HANDLE handle)
{
    DCB dcb;
    COMMTIMEOUTS timeouts;

    ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        return 0;
    }

    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (!SetCommState(handle, &dcb)) {
        return 0;
    }

    ZeroMemory(&timeouts, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutConstant = 20;
    timeouts.ReadTotalTimeoutMultiplier = 5;
    timeouts.WriteTotalTimeoutConstant = 20;
    timeouts.WriteTotalTimeoutMultiplier = 5;

    if (!SetCommTimeouts(handle, &timeouts)) {
        return 0;
    }

    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return 1;
}

/**
 * @brief Define os valores padrao da configuracao CAN/SLCAN.
 *
 * @param config Estrutura de configuracao a ser inicializada.
 */
void can_set_default_config(CanConfig *config)
{
    ZeroMemory(config, sizeof(*config));
    config->com_port[0] = '\0';
    config->bitrate = 500000;
    config->view_mode = CAN_VIEW_TABLE;
    config->timestamp_enabled = 1;
    config->colors_enabled = 1;
    config->filter_id_enabled = 0;
    config->filter_id = 0;
    snprintf(config->logfile_path, sizeof(config->logfile_path), "%s", CAN_DEFAULT_LOG_FILE_PATH);
    config->dbc_vcu_path[0] = '\0';
    config->dbc_inverter_path[0] = '\0';
    config->inverter_left_base = 0U;
    config->inverter_right_base = 0U;
    config->inverter_left_base_set = 0;
    config->inverter_right_base_set = 0;
    config->ui_can_monitor_enabled = 0;
}

/**
 * @brief Reinicializa todos os contadores estatisticos do subsistema CAN.
 *
 * @param stats Estrutura de estatisticas a ser limpa.
 */
void can_reset_stats(CanStats *stats)
{
    ZeroMemory(stats, sizeof(*stats));
}

/**
 * @brief Converte um texto para o modo de exibicao CAN correspondente.
 *
 * @param value Texto informado pelo usuario.
 * @param view_mode Ponteiro para receber o modo convertido.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int can_parse_view_mode(const char *value, CanViewMode *view_mode)
{
    if (can_equals_ignore_case(value, "raw")) {
        *view_mode = CAN_VIEW_RAW;
        return 1;
    }

    if (can_equals_ignore_case(value, "table")) {
        *view_mode = CAN_VIEW_TABLE;
        return 1;
    }

    return 0;
}

/**
 * @brief Converte o enum de view CAN para texto.
 *
 * @param view_mode Modo de exibicao configurado.
 * @return const char* Texto correspondente ao modo informado.
 */
const char *can_view_mode_to_string(CanViewMode view_mode)
{
    switch (view_mode) {
    case CAN_VIEW_RAW:
        return "raw";
    case CAN_VIEW_TABLE:
        return "table";
    default:
        return "unknown";
    }
}

/**
 * @brief Aplica uma entrada de configuracao CAN lida do arquivo persistente.
 *
 * @param config Configuracao alvo a ser atualizada.
 * @param key Nome da chave lida.
 * @param value Valor textual associado a chave.
 * @return int Retorna 1 quando a chave foi aplicada com sucesso, caso contrario 0.
 */
int can_apply_config_entry(CanConfig *config, const char *key, const char *value)
{
    CanViewMode view_mode;
    char *end = NULL;
    unsigned long numeric_value;
    char value_buffer[CAN_LOG_PATH_SIZE];
    char *trimmed_value;

    if (can_equals_ignore_case(key, "can_com")) {
        snprintf(config->com_port, sizeof(config->com_port), "%s", value);
        return 1;
    }

    if (can_equals_ignore_case(key, "can_bitrate")) {
        numeric_value = strtoul(value, &end, 10);
        if (end == value || *end != '\0' || can_get_slcan_bitrate_code((unsigned int)numeric_value) == '\0') {
            return 0;
        }
        config->bitrate = (unsigned int)numeric_value;
        return 1;
    }

    if (can_equals_ignore_case(key, "can_view")) {
        if (!can_parse_view_mode(value, &view_mode)) {
            return 0;
        }
        config->view_mode = view_mode;
        return 1;
    }

    if (can_equals_ignore_case(key, "can_timestamp")) {
        if (can_equals_ignore_case(value, "on")) {
            config->timestamp_enabled = 1;
            return 1;
        }
        if (can_equals_ignore_case(value, "off")) {
            config->timestamp_enabled = 0;
            return 1;
        }
        return 0;
    }

    if (can_equals_ignore_case(key, "can_color")) {
        if (can_equals_ignore_case(value, "on")) {
            config->colors_enabled = 1;
            return 1;
        }
        if (can_equals_ignore_case(value, "off")) {
            config->colors_enabled = 0;
            return 1;
        }
        return 0;
    }

    if (can_equals_ignore_case(key, "can_filter_id_enabled")) {
        if (can_equals_ignore_case(value, "on")) {
            config->filter_id_enabled = 1;
            return 1;
        }
        if (can_equals_ignore_case(value, "off")) {
            config->filter_id_enabled = 0;
            return 1;
        }
        return 0;
    }

    if (can_equals_ignore_case(key, "can_filter_id")) {
        numeric_value = strtoul(value, &end, 0);
        if (end == value || *end != '\0' || numeric_value > 0x7FFUL) {
            return 0;
        }
        config->filter_id = (unsigned int)numeric_value;
        return 1;
    }

    if (can_equals_ignore_case(key, "can_logfile_path")) {
        snprintf(value_buffer, sizeof(value_buffer), "%s", value);
        trimmed_value = can_trim_whitespace(value_buffer);
        snprintf(config->logfile_path, sizeof(config->logfile_path), "%s", trimmed_value);
        return 1;
    }

    if (can_equals_ignore_case(key, "can_dbc_path") || can_equals_ignore_case(key, "can_dbc_vcu_path")) {
        snprintf(value_buffer, sizeof(value_buffer), "%s", value);
        trimmed_value = can_trim_whitespace(value_buffer);
        snprintf(config->dbc_vcu_path, sizeof(config->dbc_vcu_path), "%s", trimmed_value);
        return 1;
    }

    if (can_equals_ignore_case(key, "can_dbc_inverter_path")) {
        snprintf(value_buffer, sizeof(value_buffer), "%s", value);
        trimmed_value = can_trim_whitespace(value_buffer);
        snprintf(config->dbc_inverter_path, sizeof(config->dbc_inverter_path), "%s", trimmed_value);
        return 1;
    }

    if (can_equals_ignore_case(key, "can_inverter_left_base")) {
        if (value[0] == '\0') {
            config->inverter_left_base = 0U;
            config->inverter_left_base_set = 0;
            return 1;
        }
        numeric_value = strtoul(value, &end, 0);
        if (end == value || *end != '\0') {
            return 0;
        }
        config->inverter_left_base = (unsigned int)numeric_value;
        config->inverter_left_base_set = 1;
        return 1;
    }

    if (can_equals_ignore_case(key, "can_inverter_right_base")) {
        if (value[0] == '\0') {
            config->inverter_right_base = 0U;
            config->inverter_right_base_set = 0;
            return 1;
        }
        numeric_value = strtoul(value, &end, 0);
        if (end == value || *end != '\0') {
            return 0;
        }
        config->inverter_right_base = (unsigned int)numeric_value;
        config->inverter_right_base_set = 1;
        return 1;
    }

    if (can_equals_ignore_case(key, "can_ui_monitor")) {
        if (can_equals_ignore_case(value, "on")) {
            config->ui_can_monitor_enabled = 1;
            return 1;
        }
        if (can_equals_ignore_case(value, "off")) {
            config->ui_can_monitor_enabled = 0;
            return 1;
        }
        return 0;
    }

    return 0;
}

/**
 * @brief Persiste no arquivo as chaves de configuracao do subsistema CAN.
 *
 * @param file Arquivo de configuracao aberto para escrita.
 * @param config Configuracao CAN a ser gravada.
 */
void can_save_config(FILE *file, const CanConfig *config)
{
    fprintf(file, "can_com=%s\n", config->com_port);
    fprintf(file, "can_bitrate=%u\n", config->bitrate);
    fprintf(file, "can_view=%s\n", can_view_mode_to_string(config->view_mode));
    fprintf(file, "can_timestamp=%s\n", config->timestamp_enabled ? "on" : "off");
    fprintf(file, "can_color=%s\n", config->colors_enabled ? "on" : "off");
    fprintf(file, "can_filter_id_enabled=%s\n", config->filter_id_enabled ? "on" : "off");
    fprintf(file, "can_filter_id=0x%03X\n", config->filter_id);
    fprintf(file, "can_logfile_path=%s\n", config->logfile_path);
    fprintf(file, "can_dbc_vcu_path=%s\n", config->dbc_vcu_path);
    fprintf(file, "can_dbc_inverter_path=%s\n", config->dbc_inverter_path);
    if (config->inverter_left_base_set) {
        fprintf(file, "can_inverter_left_base=0x%08X\n", config->inverter_left_base);
    } else {
        fprintf(file, "can_inverter_left_base=\n");
    }
    if (config->inverter_right_base_set) {
        fprintf(file, "can_inverter_right_base=0x%08X\n", config->inverter_right_base);
    } else {
        fprintf(file, "can_inverter_right_base=\n");
    }
    fprintf(file, "can_ui_monitor=%s\n", config->ui_can_monitor_enabled ? "on" : "off");
}

/**
 * @brief Abre a porta COM do adaptador e aplica a configuracao inicial do protocolo SLCAN.
 *
 * @param state Estado global do backend CAN.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int can_slcan_open(CanState *state)
{
    char full_port_name[64];
    char command[8];
    char bitrate_code;

    if (state->config.com_port[0] == '\0') {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (InterlockedCompareExchange(&state->opened, 0, 0)) {
        return 1;
    }

    bitrate_code = can_get_slcan_bitrate_code(state->config.bitrate);
    if (bitrate_code == '\0') {
        SetLastError(ERROR_INVALID_DATA);
        return 0;
    }

    snprintf(full_port_name, sizeof(full_port_name), "\\\\.\\%s", state->config.com_port);
    state->handle = CreateFileA(
        full_port_name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (state->handle == INVALID_HANDLE_VALUE) {
        state->handle = NULL;
        return 0;
    }

    if (!can_configure_slcan_port(state->handle)) {
        CloseHandle(state->handle);
        state->handle = NULL;
        return 0;
    }

    if (!can_slcan_write_command(state, "C\r")) {
        CloseHandle(state->handle);
        state->handle = NULL;
        return 0;
    }

    snprintf(command, sizeof(command), "S%c\r", bitrate_code);
    if (!can_slcan_write_command(state, command)) {
        CloseHandle(state->handle);
        state->handle = NULL;
        return 0;
    }

    InterlockedExchange(&state->opened, 1);
    InterlockedExchange(&state->running, 0);
    return 1;
}

/**
 * @brief Fecha a porta COM associada ao backend CAN/SLCAN.
 *
 * @param state Estado global do backend CAN.
 */
void can_slcan_close(CanState *state)
{
    if (state->handle != NULL) {
        can_slcan_write_command(state, "C\r");
        CloseHandle(state->handle);
        state->handle = NULL;
    }

    InterlockedExchange(&state->running, 0);
    InterlockedExchange(&state->opened, 0);
}

/**
 * @brief Envia o comando SLCAN que coloca o barramento em operacao.
 *
 * @param state Estado global do backend CAN.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int can_slcan_start(CanState *state)
{
    if (!InterlockedCompareExchange(&state->opened, 0, 0) || state->handle == NULL) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    if (InterlockedCompareExchange(&state->running, 0, 0)) {
        return 1;
    }

    if (!can_slcan_write_command(state, "O\r")) {
        return 0;
    }

    InterlockedExchange(&state->running, 1);
    return 1;
}

/**
 * @brief Envia o comando SLCAN para parar o barramento ativo.
 *
 * @param state Estado global do backend CAN.
 */
void can_slcan_stop(CanState *state)
{
    if (state->handle != NULL) {
        can_slcan_write_command(state, "C\r");
    }

    InterlockedExchange(&state->running, 0);
}

/**
 * @brief Envia um frame CAN padrao de 11 bits usando o formato ASCII do SLCAN.
 *
 * @param state Estado global do backend CAN.
 * @param can_id Identificador CAN padrao.
 * @param data Payload do frame.
 * @param dlc Quantidade de bytes validos no payload.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int can_slcan_send_standard(CanState *state, unsigned int can_id, const unsigned char *data, unsigned char dlc)
{
    char command[64];
    size_t offset = 0;
    unsigned char index;

    if (can_id > 0x7FFU || dlc > 8) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (!InterlockedCompareExchange(&state->running, 0, 0) || state->handle == NULL) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    offset += (size_t)snprintf(command + offset, sizeof(command) - offset, "t%03X%1X", can_id, dlc);
    for (index = 0; index < dlc && offset + 2 < sizeof(command); index++) {
        offset += (size_t)snprintf(command + offset, sizeof(command) - offset, "%02X", data[index]);
    }
    snprintf(command + offset, sizeof(command) - offset, "\r");

    if (!can_slcan_write_command(state, command)) {
        return 0;
    }

    state->stats.tx_count++;
    state->stats.last_id = can_id;
    return 1;
}

/**
 * @brief Interpreta uma linha SLCAN textual e a converte para um frame CAN classico.
 *
 * @param line Linha ASCII recebida do adaptador.
 * @param frame Estrutura que recebera o frame interpretado.
 * @return int Retorna 1 quando a linha representa um frame valido, caso contrario 0.
 */
int can_slcan_parse_frame(const char *line, CanFrame *frame)
{
    size_t id_length;
    size_t payload_offset;
    unsigned int numeric_value;
    unsigned int index;

    if (line == NULL || frame == NULL || line[0] == '\0') {
        return 0;
    }

    ZeroMemory(frame, sizeof(*frame));
    frame->direction = CAN_DIR_RX;
    GetLocalTime(&frame->timestamp);

    switch (line[0]) {
    case 't':
        frame->is_extended = 0;
        frame->is_remote = 0;
        id_length = 3;
        break;
    case 'T':
        frame->is_extended = 1;
        frame->is_remote = 0;
        id_length = 8;
        break;
    case 'r':
        frame->is_extended = 0;
        frame->is_remote = 1;
        id_length = 3;
        break;
    case 'R':
        frame->is_extended = 1;
        frame->is_remote = 1;
        id_length = 8;
        break;
    default:
        return 0;
    }

    if (!can_parse_hex_slice(line + 1, id_length, &frame->id)) {
        return 0;
    }

    if (can_hex_value(line[1 + id_length]) < 0) {
        return 0;
    }

    frame->dlc = (unsigned char)can_hex_value(line[1 + id_length]);
    frame->data_length = frame->dlc;
    if (frame->dlc > 8U) {
        return 0;
    }

    payload_offset = 2 + id_length;
    if (frame->is_remote) {
        return line[payload_offset] == '\0';
    }

    for (index = 0; index < frame->dlc; index++) {
        if (!can_parse_hex_slice(line + payload_offset + (index * 2U), 2U, &numeric_value)) {
            return 0;
        }
        frame->data[index] = (unsigned char)numeric_value;
    }

    return line[payload_offset + ((size_t)frame->dlc * 2U)] == '\0';
}
