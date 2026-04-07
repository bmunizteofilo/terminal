#define _CRT_SECURE_NO_WARNINGS

#include "dbc_parser.h"
#include "can_slcan.h"
#include "terminal.h"
#include "web_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define INPUT_BUFFER_SIZE 512
#define SERIAL_BUFFER_SIZE 256
#define PARTIAL_BUFFER_SIZE 2048
#define PAUSE_QUEUE_SIZE 1048576
#define MAX_COM_PORTS 256
#define MAX_HISTORY_ENTRIES 100
#define LOG_PATH_SIZE 260
#define CONFIG_FILE_PATH "bmt_config.ini"
#define DEFAULT_LOG_FILE_PATH "bmt_serial.log"
#define BMT_VERSION "0.3.0"

/**
 * @brief Representa os modos de paridade suportados pela configuracao serial.
 */
typedef enum ParityMode {
    BMT_PARITY_NONE,
    BMT_PARITY_ODD,
    BMT_PARITY_EVEN,
    BMT_PARITY_MARK,
    BMT_PARITY_SPACE
} ParityMode;

/**
 * @brief Representa as configuracoes de stop bits suportadas pela serial.
 */
typedef enum StopBitsMode {
    BMT_STOP_BITS_1,
    BMT_STOP_BITS_1_5,
    BMT_STOP_BITS_2
} StopBitsMode;

/**
 * @brief Representa os modos de controle de fluxo disponiveis.
 */
typedef enum FlowControlMode {
    BMT_FLOW_NONE,
    BMT_FLOW_XON_XOFF,
    BMT_FLOW_RTS_CTS,
    BMT_FLOW_DSR_DTR
} FlowControlMode;

/**
 * @brief Define como os dados recebidos pela serial serao interpretados e exibidos.
 */
typedef enum MonitorMode {
    BMT_MONITOR_RAW,
    BMT_MONITOR_ESP32
} MonitorMode;

/**
 * @brief Define quais niveis de log do ESP32 podem ser exibidos no monitor.
 */
typedef enum LogFilterMode {
    BMT_FILTER_ALL,
    BMT_FILTER_INFO,
    BMT_FILTER_WARNING,
    BMT_FILTER_ERROR,
    BMT_FILTER_DEBUG,
    BMT_FILTER_VERBOSE
} LogFilterMode;

/**
 * @brief Define o esquema de cores usado para realce visual dos logs.
 */
typedef enum ThemeMode {
    BMT_THEME_DEFAULT,
    BMT_THEME_LIGHT,
    BMT_THEME_HIGHCONTRAST
} ThemeMode;

/**
 * @brief Armazena a configuracao persistente e operacional do terminal serial.
 *
 * Campos principais:
 * - parametros da porta serial
 * - preferencias de monitoramento
 * - opcoes de persistencia, log e tema
 */
typedef struct SerialConfig {
    char com_port[32];
    DWORD baud_rate;
    BYTE data_bits;
    ParityMode parity;
    StopBitsMode stop_bits;
    FlowControlMode flow_control;
    int esp32_log_colors_enabled;
    int timestamp_enabled;
    int logfile_enabled;
    int autosave_enabled;
    int reconnect_enabled;
    MonitorMode monitor_mode;
    LogFilterMode filter_mode;
    ThemeMode theme_mode;
    char logfile_path[LOG_PATH_SIZE];
} SerialConfig;

/**
 * @brief Mantem o estado completo de execucao do Terminal BMT.
 *
 * Campos principais:
 * - handles e threads da serial
 * - flags de execucao, conexao e pausa
 * - buffers de processamento e fila de pausa
 * - historico da sessao e configuracao ativa
 */
typedef struct SerialState {
    HANDLE serial_handle;
    HANDLE reader_thread;
    HANDLE hotkey_thread;
    DWORD reader_thread_id;
    DWORD hotkey_thread_id;
    volatile LONG running;
    volatile LONG stop_requested;
    volatile LONG connected;
    volatile LONG paused;
    CRITICAL_SECTION console_lock;
    SerialConfig config;
    char partial_line[PARTIAL_BUFFER_SIZE];
    size_t partial_length;
    char history[MAX_HISTORY_ENTRIES][INPUT_BUFFER_SIZE];
    int history_count;
    char paused_output[PAUSE_QUEUE_SIZE];
    size_t paused_length;
    unsigned long paused_dropped_messages;
    SerialMonitorState serial_monitor;
    SerialMonitorState can_monitor;
    CanState can_state;
    WebUiState web_ui;
} SerialState;

/**
 * @brief Compara duas strings sem diferenciar letras maiusculas e minusculas.
 */
static int equals_ignore_case(const char *left, const char *right);
/**
 * @brief Inicializa a configuracao serial padrao do terminal.
 */
static void set_default_config(SerialConfig *config);
/**
 * @brief Converte texto para o enum de paridade.
 */
static int parse_parity(const char *value, ParityMode *parity);
/**
 * @brief Converte texto para o enum de stop bits.
 */
static int parse_stop_bits(const char *value, StopBitsMode *stop_bits);
/**
 * @brief Converte texto para o enum de controle de fluxo.
 */
static int parse_flow_control(const char *value, FlowControlMode *flow_control);
/**
 * @brief Converte texto para o enum de modo de monitor serial.
 */
static int parse_monitor_mode(const char *value, MonitorMode *monitor_mode);
/**
 * @brief Converte texto para o enum de filtro de logs ESP32.
 */
static int parse_filter_mode(const char *value, LogFilterMode *filter_mode);
/**
 * @brief Converte texto para o enum de tema visual.
 */
static int parse_theme_mode(const char *value, ThemeMode *theme_mode);
/**
 * @brief Retorna a representacao textual da paridade.
 */
static const char *parity_to_string(ParityMode parity);
/**
 * @brief Retorna a representacao textual dos stop bits.
 */
static const char *stop_bits_to_string(StopBitsMode stop_bits);
/**
 * @brief Retorna a representacao textual do controle de fluxo.
 */
static const char *flow_control_to_string(FlowControlMode flow_control);
/**
 * @brief Retorna a representacao textual do modo de monitor serial.
 */
static const char *monitor_mode_to_string(MonitorMode monitor_mode);
/**
 * @brief Retorna a representacao textual do filtro de logs.
 */
static const char *filter_mode_to_string(LogFilterMode filter_mode);
/**
 * @brief Retorna a representacao textual do tema visual.
 */
static const char *theme_mode_to_string(ThemeMode theme_mode);
/**
 * @brief Imprime o prompt principal do terminal.
 */
static void print_prompt(void);
/**
 * @brief Inicia a comunicacao serial configurada.
 */
static int start_serial(SerialState *state);
/**
 * @brief Interrompe a comunicacao serial ativa.
 */
static void stop_serial(SerialState *state);
/**
 * @brief Persiste a configuracao atual em arquivo.
 */
static int save_config(const SerialState *state);
/**
 * @brief Salva automaticamente a configuracao quando autosave esta ativo.
 */
static void auto_save_if_enabled(const SerialState *state);
/**
 * @brief Exibe a ajuda especifica dos comandos CAN.
 */
static void print_can_help(void);
/**
 * @brief Exibe o estado atual do subsistema CAN.
 */
static void print_can_status(const SerialState *state);
/**
 * @brief Processa um comando do namespace CAN.
 */
static int process_can_command(SerialState *state, char *rest);
/**
 * @brief Processa um comando do namespace UI web.
 */
static int process_ui_command(SerialState *state, char *rest);
/**
 * @brief Lista as portas COM detectadas no sistema.
 */
static int list_available_ports(char ports[][32], int max_ports);
/**
 * @brief Recarrega a configuracao persistida do terminal.
 */
static void reload_config(SerialState *state);
/**
 * @brief Solicita a parada do barramento CAN em execucao.
 */
static void stop_can_interface(SerialState *state);
/**
 * @brief Carrega um DBC para o alvo CAN selecionado.
 */
static int load_can_dbc(SerialState *state, const char *target, const char *path);
/**
 * @brief Descarrega o DBC associado ao alvo CAN informado.
 */
static void unload_can_dbc(SerialState *state, const char *target);
/**
 * @brief Abre a interface CAN configurada.
 */
static int open_can_interface(SerialState *state);
/**
 * @brief Inicia a captura do barramento CAN.
 */
static int start_can_interface(SerialState *state);
/**
 * @brief Fecha a interface CAN aberta.
 */
static void close_can_interface(SerialState *state);
/**
 * @brief Atualiza o snapshot serial consumido pela UI web.
 */
static void update_web_ui_serial_snapshot(SerialState *state);
/**
 * @brief Processa bytes recebidos da interface CAN monitorada.
 */
static void process_can_bytes(SerialState *state, const char *buffer, size_t bytes_read);
/**
 * @brief Finaliza uma linha CAN parcial acumulada em buffer.
 */
static void flush_partial_can_line(SerialState *state);
/**
 * @brief Ajusta o modo de monitor CAN exposto na UI.
 */
static int set_can_ui_monitor_mode(SerialState *state, const char *value);
/**
 * @brief Verifica se a janela atual pode capturar atalhos do terminal.
 */
static int is_terminal_hotkey_context_active(void);
/**
 * @brief Callback web para aplicar configuracoes seriais.
 */
static int web_apply_serial_settings_callback(void *context,
                                              const char *com_port,
                                              unsigned int baud_rate,
                                              unsigned int data_bits,
                                              const char *parity,
                                              const char *stop_bits,
                                              const char *flow_control,
                                              int timestamp_enabled,
                                              int logfile_enabled,
                                              const char *logfile_path,
                                              int reconnect_enabled,
                                              char *message,
                                              size_t message_size);
/**
 * @brief Callback web para listar portas COM detectadas.
 */
static int web_list_ports_callback(void *context, char ports[][32], int max_ports);
/**
 * @brief Callback web para aplicar configuracoes CAN.
 */
static int web_apply_can_settings_callback(void *context, const char *com_port, unsigned int bitrate, char *message, size_t message_size);
/**
 * @brief Callback web para carregar um arquivo DBC.
 */
static int web_load_dbc_callback(void *context, const char *path, char *message, size_t message_size);
/**
 * @brief Callback web para executar acoes rapidas da interface.
 */
static int web_execute_action_callback(void *context, const char *action, char *message, size_t message_size);

/**
 * @brief Retorna o handle do console de saida padrao.
 *
 * @return HANDLE Handle do console ou INVALID_HANDLE_VALUE em caso de falha.
 */
static HANDLE get_console_output_handle(void)
{
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

/**
 * @brief Restaura a cor padrao do console do Windows.
 *
 * @param console_handle Handle do console de saida.
 * @param original_attributes Atributos originais do console.
 */
static void restore_console_color(HANDLE console_handle, WORD original_attributes)
{
    if (console_handle != NULL && console_handle != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(console_handle, original_attributes);
    }
}

/**
 * @brief Remove caracteres de quebra de linha do final de uma string.
 *
 * @param text String que sera ajustada em memoria.
 */
static void trim_newline(char *text)
{
    size_t length = strlen(text);

    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[length - 1] = '\0';
        length--;
    }
}

/**
 * @brief Remove espacos em branco do inicio e do fim de uma string.
 *
 * @param text String que sera ajustada em memoria.
 * @return char* Ponteiro para o inicio util da string.
 */
static char *trim_whitespace(char *text)
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
 * @brief Copia uma string convertendo seus caracteres para maiusculo.
 *
 * @param destination Buffer de destino.
 * @param destination_size Tamanho do buffer de destino.
 * @param source String de origem.
 */
static void copy_uppercase(char *destination, size_t destination_size, const char *source)
{
    size_t index = 0;

    if (destination_size == 0) {
        return;
    }

    while (source[index] != '\0' && index + 1 < destination_size) {
        destination[index] = (char)toupper((unsigned char)source[index]);
        index++;
    }

    destination[index] = '\0';
}

/**
 * @brief Converte um valor booleano para texto no formato on ou off.
 *
 * @param enabled Valor booleano a ser convertido.
 * @return const char* Texto correspondente ao valor informado.
 */
static const char *bool_to_on_off(int enabled)
{
    return enabled ? "on" : "off";
}

/**
 * @brief Extrai o proximo token separado por espacos de uma linha.
 *
 * @param cursor Ponteiro para a posicao atual de leitura da linha.
 * @return char* Proximo token encontrado ou NULL quando nao houver mais tokens.
 */
static char *next_token(char **cursor)
{
    char *start;

    if (cursor == NULL || *cursor == NULL) {
        return NULL;
    }

    while (**cursor != '\0' && isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }

    if (**cursor == '\0') {
        return NULL;
    }

    start = *cursor;
    while (**cursor != '\0' && !isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }

    if (**cursor != '\0') {
        **cursor = '\0';
        (*cursor)++;
    }

    return start;
}

/**
 * @brief Compara duas strings ignorando diferencas entre maiusculas e minusculas.
 *
 * @param left Primeira string.
 * @param right Segunda string.
 * @return int Retorna 1 quando as strings sao equivalentes, caso contrario 0.
 */
static int equals_ignore_case(const char *left, const char *right)
{
    char left_buffer[64];
    char right_buffer[64];

    copy_uppercase(left_buffer, sizeof(left_buffer), left);
    copy_uppercase(right_buffer, sizeof(right_buffer), right);
    return strcmp(left_buffer, right_buffer) == 0;
}

/**
 * @brief Define os valores padrao de configuracao do terminal serial.
 *
 * @param config Estrutura de configuracao a ser inicializada.
 */
static void set_default_config(SerialConfig *config)
{
    ZeroMemory(config, sizeof(*config));
    config->com_port[0] = '\0';
    config->baud_rate = CBR_115200;
    config->data_bits = 8;
    config->parity = BMT_PARITY_NONE;
    config->stop_bits = BMT_STOP_BITS_1;
    config->flow_control = BMT_FLOW_NONE;
    config->esp32_log_colors_enabled = 1;
    config->timestamp_enabled = 0;
    config->logfile_enabled = 0;
    config->autosave_enabled = 0;
    config->reconnect_enabled = 0;
    config->monitor_mode = BMT_MONITOR_ESP32;
    config->filter_mode = BMT_FILTER_ALL;
    config->theme_mode = BMT_THEME_DEFAULT;
    snprintf(config->logfile_path, sizeof(config->logfile_path), "%s", DEFAULT_LOG_FILE_PATH);
}

/**
 * @brief Converte o enum de paridade para texto.
 *
 * @param parity Modo de paridade configurado.
 * @return const char* Texto correspondente ao valor informado.
 */
static const char *parity_to_string(ParityMode parity)
{
    switch (parity) {
    case BMT_PARITY_NONE:
        return "none";
    case BMT_PARITY_ODD:
        return "odd";
    case BMT_PARITY_EVEN:
        return "even";
    case BMT_PARITY_MARK:
        return "mark";
    case BMT_PARITY_SPACE:
        return "space";
    default:
        return "unknown";
    }
}

/**
 * @brief Converte o enum de stop bits para texto.
 *
 * @param stop_bits Configuracao de stop bits.
 * @return const char* Texto correspondente ao valor informado.
 */
static const char *stop_bits_to_string(StopBitsMode stop_bits)
{
    switch (stop_bits) {
    case BMT_STOP_BITS_1:
        return "1";
    case BMT_STOP_BITS_1_5:
        return "1.5";
    case BMT_STOP_BITS_2:
        return "2";
    default:
        return "unknown";
    }
}

/**
 * @brief Converte o enum de controle de fluxo para texto.
 *
 * @param flow_control Modo de controle de fluxo configurado.
 * @return const char* Texto correspondente ao valor informado.
 */
static const char *flow_control_to_string(FlowControlMode flow_control)
{
    switch (flow_control) {
    case BMT_FLOW_NONE:
        return "none";
    case BMT_FLOW_XON_XOFF:
        return "xonxoff";
    case BMT_FLOW_RTS_CTS:
        return "rtscts";
    case BMT_FLOW_DSR_DTR:
        return "dsrdtr";
    default:
        return "unknown";
    }
}

/**
 * @brief Converte o modo de monitor para texto.
 *
 * @param monitor_mode Modo de monitor configurado.
 * @return const char* Texto correspondente ao valor informado.
 */
static const char *monitor_mode_to_string(MonitorMode monitor_mode)
{
    switch (monitor_mode) {
    case BMT_MONITOR_RAW:
        return "raw";
    case BMT_MONITOR_ESP32:
        return "esp32";
    default:
        return "unknown";
    }
}

/**
 * @brief Converte o filtro de logs para texto.
 *
 * @param filter_mode Filtro de logs configurado.
 * @return const char* Texto correspondente ao valor informado.
 */
static const char *filter_mode_to_string(LogFilterMode filter_mode)
{
    switch (filter_mode) {
    case BMT_FILTER_ALL:
        return "all";
    case BMT_FILTER_INFO:
        return "info";
    case BMT_FILTER_WARNING:
        return "warning";
    case BMT_FILTER_ERROR:
        return "error";
    case BMT_FILTER_DEBUG:
        return "debug";
    case BMT_FILTER_VERBOSE:
        return "verbose";
    default:
        return "unknown";
    }
}

/**
 * @brief Converte o tema visual para texto.
 *
 * @param theme_mode Tema configurado.
 * @return const char* Texto correspondente ao valor informado.
 */
static const char *theme_mode_to_string(ThemeMode theme_mode)
{
    switch (theme_mode) {
    case BMT_THEME_DEFAULT:
        return "default";
    case BMT_THEME_LIGHT:
        return "light";
    case BMT_THEME_HIGHCONTRAST:
        return "highcontrast";
    default:
        return "unknown";
    }
}

/**
 * @brief Interpreta um texto e converte para um modo de paridade valido.
 *
 * @param value Texto informado pelo usuario.
 * @param parity Ponteiro para receber o valor convertido.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int parse_parity(const char *value, ParityMode *parity)
{
    if (equals_ignore_case(value, "none")) {
        *parity = BMT_PARITY_NONE;
        return 1;
    }
    if (equals_ignore_case(value, "odd")) {
        *parity = BMT_PARITY_ODD;
        return 1;
    }
    if (equals_ignore_case(value, "even")) {
        *parity = BMT_PARITY_EVEN;
        return 1;
    }
    if (equals_ignore_case(value, "mark")) {
        *parity = BMT_PARITY_MARK;
        return 1;
    }
    if (equals_ignore_case(value, "space")) {
        *parity = BMT_PARITY_SPACE;
        return 1;
    }
    return 0;
}

/**
 * @brief Interpreta um texto e converte para um modo de stop bits valido.
 *
 * @param value Texto informado pelo usuario.
 * @param stop_bits Ponteiro para receber o valor convertido.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int parse_stop_bits(const char *value, StopBitsMode *stop_bits)
{
    if (strcmp(value, "1") == 0) {
        *stop_bits = BMT_STOP_BITS_1;
        return 1;
    }
    if (strcmp(value, "1.5") == 0) {
        *stop_bits = BMT_STOP_BITS_1_5;
        return 1;
    }
    if (strcmp(value, "2") == 0) {
        *stop_bits = BMT_STOP_BITS_2;
        return 1;
    }
    return 0;
}

/**
 * @brief Interpreta um texto e converte para um modo de controle de fluxo valido.
 *
 * @param value Texto informado pelo usuario.
 * @param flow_control Ponteiro para receber o valor convertido.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int parse_flow_control(const char *value, FlowControlMode *flow_control)
{
    if (equals_ignore_case(value, "none")) {
        *flow_control = BMT_FLOW_NONE;
        return 1;
    }
    if (equals_ignore_case(value, "xonxoff")) {
        *flow_control = BMT_FLOW_XON_XOFF;
        return 1;
    }
    if (equals_ignore_case(value, "rtscts")) {
        *flow_control = BMT_FLOW_RTS_CTS;
        return 1;
    }
    if (equals_ignore_case(value, "dsrdtr")) {
        *flow_control = BMT_FLOW_DSR_DTR;
        return 1;
    }
    return 0;
}

/**
 * @brief Interpreta um texto e converte para um modo de monitor valido.
 *
 * @param value Texto informado pelo usuario.
 * @param monitor_mode Ponteiro para receber o valor convertido.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int parse_monitor_mode(const char *value, MonitorMode *monitor_mode)
{
    if (equals_ignore_case(value, "raw")) {
        *monitor_mode = BMT_MONITOR_RAW;
        return 1;
    }
    if (equals_ignore_case(value, "esp32")) {
        *monitor_mode = BMT_MONITOR_ESP32;
        return 1;
    }
    return 0;
}

/**
 * @brief Interpreta um texto e converte para um filtro de log valido.
 *
 * @param value Texto informado pelo usuario.
 * @param filter_mode Ponteiro para receber o valor convertido.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int parse_filter_mode(const char *value, LogFilterMode *filter_mode)
{
    if (equals_ignore_case(value, "all")) {
        *filter_mode = BMT_FILTER_ALL;
        return 1;
    }
    if (equals_ignore_case(value, "info")) {
        *filter_mode = BMT_FILTER_INFO;
        return 1;
    }
    if (equals_ignore_case(value, "warning")) {
        *filter_mode = BMT_FILTER_WARNING;
        return 1;
    }
    if (equals_ignore_case(value, "error")) {
        *filter_mode = BMT_FILTER_ERROR;
        return 1;
    }
    if (equals_ignore_case(value, "debug")) {
        *filter_mode = BMT_FILTER_DEBUG;
        return 1;
    }
    if (equals_ignore_case(value, "verbose")) {
        *filter_mode = BMT_FILTER_VERBOSE;
        return 1;
    }
    return 0;
}

/**
 * @brief Interpreta um texto e converte para um tema valido.
 *
 * @param value Texto informado pelo usuario.
 * @param theme_mode Ponteiro para receber o valor convertido.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int parse_theme_mode(const char *value, ThemeMode *theme_mode)
{
    if (equals_ignore_case(value, "default")) {
        *theme_mode = BMT_THEME_DEFAULT;
        return 1;
    }
    if (equals_ignore_case(value, "light")) {
        *theme_mode = BMT_THEME_LIGHT;
        return 1;
    }
    if (equals_ignore_case(value, "highcontrast")) {
        *theme_mode = BMT_THEME_HIGHCONTRAST;
        return 1;
    }
    return 0;
}

/**
 * @brief Registra um comando no historico da sessao.
 *
 * @param state Estado global do terminal serial.
 * @param command Linha de comando digitada.
 */
static void add_history_entry(SerialState *state, const char *command)
{
    int index;

    if (state->history_count < MAX_HISTORY_ENTRIES) {
        snprintf(state->history[state->history_count], sizeof(state->history[state->history_count]), "%s", command);
        state->history_count++;
        return;
    }

    for (index = 1; index < MAX_HISTORY_ENTRIES; index++) {
        snprintf(state->history[index - 1], sizeof(state->history[index - 1]), "%s", state->history[index]);
    }

    snprintf(state->history[MAX_HISTORY_ENTRIES - 1], sizeof(state->history[MAX_HISTORY_ENTRIES - 1]), "%s", command);
}

/**
 * @brief Exibe o historico de comandos da sessao atual.
 *
 * @param state Estado global do terminal serial.
 */
static void print_history(const SerialState *state)
{
    int index;

    if (state->history_count == 0) {
        printf("Historico vazio.\n");
        return;
    }

    printf("Historico de comandos:\n");
    for (index = 0; index < state->history_count; index++) {
        printf("  %d. %s\n", index + 1, state->history[index]);
    }
}

/**
 * @brief Formata o horario local atual para exibicao.
 *
 * @param buffer Buffer de destino.
 * @param buffer_size Tamanho do buffer de destino.
 */
static void format_local_timestamp(char *buffer, size_t buffer_size)
{
    SYSTEMTIME local_time;

    GetLocalTime(&local_time);
    snprintf(buffer, buffer_size, "%02u:%02u:%02u",
             (unsigned int)local_time.wHour,
             (unsigned int)local_time.wMinute,
             (unsigned int)local_time.wSecond);
}

/**
 * @brief Grava uma linha no arquivo de log quando o recurso estiver habilitado.
 *
 * @param state Estado global do terminal serial.
 * @param text Linha de texto a ser registrada.
 */
static void write_log_line(const SerialState *state, const char *text)
{
    FILE *file;

    if (!state->config.logfile_enabled) {
        return;
    }

    file = fopen(state->config.logfile_path, "a");
    if (file == NULL) {
        return;
    }

    fputs(text, file);
    fclose(file);
}

/**
 * @brief Adiciona texto a fila de saida pausada.
 *
 * @param state Estado global do terminal serial.
 * @param text Texto a ser armazenado na fila.
 */
static void enqueue_paused_output(SerialState *state, const char *text)
{
    size_t text_length = strlen(text);

    if (state->paused_length + text_length >= sizeof(state->paused_output)) {
        state->paused_dropped_messages++;
        return;
    }

    memcpy(state->paused_output + state->paused_length, text, text_length);
    state->paused_length += text_length;
    state->paused_output[state->paused_length] = '\0';
}

/**
 * @brief Despeja a fila acumulada durante a pausa no terminal.
 *
 * @param state Estado global do terminal serial.
 */
static void flush_paused_output(SerialState *state)
{
    if (state->paused_length > 0) {
        printf("%s", state->paused_output);
        state->paused_length = 0;
        state->paused_output[0] = '\0';
    }

    if (state->paused_dropped_messages > 0) {
        printf("[pause] %lu eventos nao puderam ser armazenados por limite de fila.\n",
               state->paused_dropped_messages);
        state->paused_dropped_messages = 0;
    }
}

/**
 * @brief Ativa a pausa visual do monitor serial.
 *
 * @param state Estado global do terminal serial.
 */
static void pause_monitor_display(SerialState *state)
{
    if (InterlockedCompareExchange(&state->paused, 0, 0)) {
        return;
    }

    InterlockedExchange(&state->paused, 1);
    printf("\n[pause] Monitor pausado. Pressione 'r' para retomar.\n");
}

/**
 * @brief Retoma a exibicao do monitor serial e despeja a fila acumulada.
 *
 * @param state Estado global do terminal serial.
 */
static void resume_monitor_display(SerialState *state)
{
    if (!InterlockedCompareExchange(&state->paused, 0, 0)) {
        return;
    }

    InterlockedExchange(&state->paused, 0);
    printf("\n[resume] Monitor retomado. Despejando fila acumulada.\n");
    flush_paused_output(state);
}

/**
 * @brief Verifica se a janela atualmente em foco pertence ao contexto do Terminal BMT.
 *
 * @return int Retorna 1 quando o terminal esta em foco, caso contrario 0.
 */
static int is_terminal_hotkey_context_active(void)
{
    HWND foreground_window = GetForegroundWindow();
    HWND console_window = GetConsoleWindow();
    DWORD foreground_process_id = 0;

    if (foreground_window == NULL) {
        return 0;
    }

    if (console_window != NULL && foreground_window == console_window) {
        return 1;
    }

    GetWindowThreadProcessId(foreground_window, &foreground_process_id);
    return foreground_process_id == GetCurrentProcessId();
}
/**
 * @brief Verifica se uma linha segue o prefixo de logs do ESP-IDF.
 *
 * @param text Linha recebida da serial.
 * @param level Ponteiro para receber o nivel detectado.
 * @return int Retorna 1 quando um nivel valido foi encontrado, caso contrario 0.
 */
static int detect_esp32_log_level(const char *text, char *level)
{
    if (text == NULL || level == NULL || text[0] == '\0') {
        return 0;
    }

    if (strchr("EWIDV", text[0]) == NULL) {
        return 0;
    }

    if (text[1] != ' ' || text[2] != '(') {
        return 0;
    }

    *level = text[0];
    return 1;
}

/**
 * @brief Verifica se um nivel de log deve ser exibido segundo o filtro atual.
 *
 * @param filter_mode Filtro ativo no terminal.
 * @param level Nivel de log detectado.
 * @return int Retorna 1 quando a linha deve ser exibida, caso contrario 0.
 */
static int log_level_matches_filter(LogFilterMode filter_mode, char level)
{
    if (filter_mode == BMT_FILTER_ALL) {
        return 1;
    }

    if (filter_mode == BMT_FILTER_INFO && level == 'I') {
        return 1;
    }
    if (filter_mode == BMT_FILTER_WARNING && level == 'W') {
        return 1;
    }
    if (filter_mode == BMT_FILTER_ERROR && level == 'E') {
        return 1;
    }
    if (filter_mode == BMT_FILTER_DEBUG && level == 'D') {
        return 1;
    }
    if (filter_mode == BMT_FILTER_VERBOSE && level == 'V') {
        return 1;
    }

    return 0;
}

/**
 * @brief Retorna a cor associada a um nivel de log conforme o tema selecionado.
 *
 * @param theme_mode Tema visual ativo.
 * @param level Nivel de log detectado.
 * @return WORD Atributo de cor do console do Windows.
 */
static WORD get_esp32_log_color(ThemeMode theme_mode, char level)
{
    if (theme_mode == BMT_THEME_LIGHT) {
        switch (level) {
        case 'E':
            return FOREGROUND_RED;
        case 'W':
            return FOREGROUND_RED | FOREGROUND_GREEN;
        case 'I':
            return FOREGROUND_GREEN;
        case 'D':
            return FOREGROUND_BLUE;
        case 'V':
            return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        default:
            return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        }
    }

    if (theme_mode == BMT_THEME_HIGHCONTRAST) {
        switch (level) {
        case 'E':
            return BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case 'W':
            return BACKGROUND_RED | BACKGROUND_GREEN | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case 'I':
            return BACKGROUND_GREEN | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case 'D':
            return BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case 'V':
            return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        default:
            return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        }
    }

    switch (level) {
    case 'E':
        return FOREGROUND_RED | FOREGROUND_INTENSITY;
    case 'W':
        return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case 'I':
        return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case 'D':
        return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case 'V':
        return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    default:
        return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
}

/**
 * @brief Imprime uma linha da serial no terminal, aplicando filtros, timestamp e cor.
 *
 * @param state Estado global do terminal serial.
 * @param line Linha de texto a ser exibida.
 */
static void print_serial_line(const SerialState *state, const char *line)
{
    HANDLE console_handle;
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    WORD original_attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    char timestamp[32];
    char level = '\0';
    int is_esp32_log;
    char log_output[INPUT_BUFFER_SIZE * 2];
    int is_paused;

    if (line == NULL || line[0] == '\0') {
        return;
    }

    is_esp32_log = detect_esp32_log_level(line, &level);
    if (state->config.monitor_mode == BMT_MONITOR_ESP32 && is_esp32_log &&
        !log_level_matches_filter(state->config.filter_mode, level)) {
        return;
    }

    console_handle = get_console_output_handle();
    if (console_handle != INVALID_HANDLE_VALUE && console_handle != NULL &&
        GetConsoleScreenBufferInfo(console_handle, &console_info)) {
        original_attributes = console_info.wAttributes;
    }

    log_output[0] = '\0';
    is_paused = InterlockedCompareExchange((LONG *)&state->paused, 0, 0) != 0;

    if (state->config.timestamp_enabled) {
        format_local_timestamp(timestamp, sizeof(timestamp));
        snprintf(log_output + strlen(log_output), sizeof(log_output) - strlen(log_output), "[%s] ", timestamp);
    }

    snprintf(log_output + strlen(log_output), sizeof(log_output) - strlen(log_output), "[serial] ");
    snprintf(log_output + strlen(log_output), sizeof(log_output) - strlen(log_output), "%s\n", line);

    if (is_paused) {
        enqueue_paused_output((SerialState *)state, log_output);
        write_log_line(state, log_output);
        return;
    }

    if (state->config.timestamp_enabled) {
        printf("[%s] ", timestamp);
    }

    printf("[serial] ");

    if (state->config.monitor_mode == BMT_MONITOR_ESP32 &&
        state->config.esp32_log_colors_enabled &&
        is_esp32_log) {
        SetConsoleTextAttribute(console_handle, get_esp32_log_color(state->config.theme_mode, level));
        printf("%s\n", line);
        restore_console_color(console_handle, original_attributes);
    } else {
        printf("%s\n", line);
    }

    write_log_line(state, log_output);
}

/**
 * @brief Processa um bloco recebido da serial e o divide em linhas.
 *
 * @param state Estado global do terminal serial.
 * @param buffer Buffer recebido da serial.
 * @param bytes_read Quantidade de bytes lidos.
 */
static void process_serial_bytes(SerialState *state, const char *buffer, size_t bytes_read)
{
    size_t index;

    for (index = 0; index < bytes_read; index++) {
        char current = buffer[index];

        if (current == '\r') {
            continue;
        }

        if (current == '\n') {
            state->partial_line[state->partial_length] = '\0';
            serial_monitor_push_line(&state->serial_monitor, state->partial_line);
            print_serial_line(state, state->partial_line);
            state->partial_length = 0;
            state->partial_line[0] = '\0';
            continue;
        }

        if (state->partial_length + 1 < sizeof(state->partial_line)) {
            state->partial_line[state->partial_length] = current;
            state->partial_length++;
            state->partial_line[state->partial_length] = '\0';
        }
    }
}

/**
 * @brief Exibe uma linha parcial acumulada quando a comunicacao e encerrada.
 *
 * @param state Estado global do terminal serial.
 */
static void flush_partial_serial_line(SerialState *state)
{
    if (state->partial_length == 0) {
        return;
    }

    state->partial_line[state->partial_length] = '\0';
    serial_monitor_push_line(&state->serial_monitor, state->partial_line);
    print_serial_line(state, state->partial_line);
    state->partial_length = 0;
    state->partial_line[0] = '\0';
}

/**
 * @brief Solicita a parada da comunicacao serial abortando leituras pendentes.
 *
 * @param state Estado global do terminal serial.
 */
static void request_serial_stop(SerialState *state)
{
    InterlockedExchange(&state->stop_requested, 1);
    InterlockedExchange(&state->running, 0);

    if (state->serial_handle != NULL) {
        CancelIoEx(state->serial_handle, NULL);
    }
}

/**
 * @brief Converte o enum de paridade para o valor esperado pela API do Windows.
 *
 * @param parity Modo de paridade configurado.
 * @return BYTE Valor de paridade usado pela estrutura DCB.
 */
static BYTE to_windows_parity(ParityMode parity)
{
    switch (parity) {
    case BMT_PARITY_NONE:
        return NOPARITY;
    case BMT_PARITY_ODD:
        return ODDPARITY;
    case BMT_PARITY_EVEN:
        return EVENPARITY;
    case BMT_PARITY_MARK:
        return MARKPARITY;
    case BMT_PARITY_SPACE:
        return SPACEPARITY;
    default:
        return NOPARITY;
    }
}

/**
 * @brief Converte o enum de stop bits para o valor esperado pela API do Windows.
 *
 * @param stop_bits Configuracao de stop bits.
 * @return BYTE Valor de stop bits usado pela estrutura DCB.
 */
static BYTE to_windows_stop_bits(StopBitsMode stop_bits)
{
    switch (stop_bits) {
    case BMT_STOP_BITS_1:
        return ONESTOPBIT;
    case BMT_STOP_BITS_1_5:
        return ONE5STOPBITS;
    case BMT_STOP_BITS_2:
        return TWOSTOPBITS;
    default:
        return ONESTOPBIT;
    }
}

/**
 * @brief Aplica a configuracao serial a uma porta ja aberta.
 *
 * @param serial_handle Handle da porta serial aberta.
 * @param config Configuracao serial desejada.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int configure_serial_port(HANDLE serial_handle, const SerialConfig *config)
{
    COMMTIMEOUTS timeouts;
    DCB dcb;

    ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(serial_handle, &dcb)) {
        return 0;
    }

    dcb.BaudRate = config->baud_rate;
    dcb.ByteSize = config->data_bits;
    dcb.Parity = to_windows_parity(config->parity);
    dcb.StopBits = to_windows_stop_bits(config->stop_bits);
    dcb.fBinary = TRUE;
    dcb.fParity = (config->parity != BMT_PARITY_NONE);
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (config->flow_control == BMT_FLOW_XON_XOFF) {
        dcb.fOutX = TRUE;
        dcb.fInX = TRUE;
    } else if (config->flow_control == BMT_FLOW_RTS_CTS) {
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    } else if (config->flow_control == BMT_FLOW_DSR_DTR) {
        dcb.fOutxDsrFlow = TRUE;
        dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
    }

    if (!SetCommState(serial_handle, &dcb)) {
        return 0;
    }

    ZeroMemory(&timeouts, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(serial_handle, &timeouts)) {
        return 0;
    }

    PurgeComm(serial_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return 1;
}

/**
 * @brief Fecha a porta serial atualmente aberta.
 *
 * @param state Estado global do terminal serial.
 */
static void close_serial_handle(SerialState *state)
{
    if (state->serial_handle != NULL) {
        CloseHandle(state->serial_handle);
        state->serial_handle = NULL;
    }

    InterlockedExchange(&state->connected, 0);
}

/**
 * @brief Abre a porta serial configurada e aplica os parametros atuais.
 *
 * @param state Estado global do terminal serial.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int open_serial_handle(SerialState *state)
{
    char full_port_name[64];

    if (state->config.com_port[0] == '\0') {
        return 0;
    }

    snprintf(full_port_name, sizeof(full_port_name), "\\\\.\\%s", state->config.com_port);
    state->serial_handle = CreateFileA(
        full_port_name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (state->serial_handle == INVALID_HANDLE_VALUE) {
        state->serial_handle = NULL;
        return 0;
    }

    if (!configure_serial_port(state->serial_handle, &state->config)) {
        close_serial_handle(state);
        return 0;
    }

    InterlockedExchange(&state->connected, 1);
    return 1;
}
/**
 * @brief Tenta reconectar automaticamente a porta serial quando o recurso estiver habilitado.
 *
 * @param state Estado global do terminal serial.
 * @return int Retorna 1 quando a reconexao ocorreu com sucesso, caso contrario 0.
 */
static int try_reconnect_serial(SerialState *state)
{
    if (!state->config.reconnect_enabled) {
        return 0;
    }

    EnterCriticalSection(&state->console_lock);
    printf("\n[reconnect] Tentando reconectar %s...\n", state->config.com_port);
    LeaveCriticalSection(&state->console_lock);

    while (InterlockedCompareExchange(&state->running, 0, 0)) {
        if (open_serial_handle(state)) {
            EnterCriticalSection(&state->console_lock);
            printf("[reconnect] Comunicacao restabelecida em %s.\n", state->config.com_port);
            LeaveCriticalSection(&state->console_lock);
            return 1;
        }

        Sleep(1000);
    }

    return 0;
}

/**
 * @brief Envia bytes brutos pela porta serial atualmente ativa.
 *
 * @param state Estado global do terminal serial.
 * @param data Buffer de dados a transmitir.
 * @param size Quantidade de bytes a transmitir.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int send_serial_bytes(SerialState *state, const unsigned char *data, size_t size)
{
    DWORD bytes_written = 0;

    if (!InterlockedCompareExchange(&state->running, 0, 0) ||
        state->serial_handle == NULL ||
        !InterlockedCompareExchange(&state->connected, 0, 0)) {
        fprintf(stderr, "A comunicacao serial nao esta ativa.\n");
        return 0;
    }

    if (!WriteFile(state->serial_handle, data, (DWORD)size, &bytes_written, NULL) || bytes_written != size) {
        fprintf(stderr, "Falha ao enviar dados pela serial. Codigo: %lu\n", GetLastError());
        return 0;
    }

    return 1;
}

/**
 * @brief Interpreta uma string hexadecimal e envia os bytes correspondentes pela serial.
 *
 * @param state Estado global do terminal serial.
 * @param hex_text Texto hexadecimal informado pelo usuario.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int send_serial_hex(SerialState *state, const char *hex_text)
{
    unsigned char bytes[INPUT_BUFFER_SIZE];
    int nibble_count = 0;
    int byte_count = 0;
    int high_nibble = 0;
    size_t index;

    for (index = 0; hex_text[index] != '\0'; index++) {
        char current = hex_text[index];
        int value;

        if (isspace((unsigned char)current) || current == ',' || current == '-') {
            continue;
        }

        if (current >= '0' && current <= '9') {
            value = current - '0';
        } else if (current >= 'a' && current <= 'f') {
            value = 10 + (current - 'a');
        } else if (current >= 'A' && current <= 'F') {
            value = 10 + (current - 'A');
        } else {
            fprintf(stderr, "Hex invalido. Use pares como 'AA 01 FF'.\n");
            return 0;
        }

        if ((nibble_count % 2) == 0) {
            high_nibble = value;
        } else {
            bytes[byte_count] = (unsigned char)((high_nibble << 4) | value);
            byte_count++;
        }

        nibble_count++;
    }

    if (nibble_count == 0 || (nibble_count % 2) != 0) {
        fprintf(stderr, "Hex invalido. Forneca quantidade par de digitos.\n");
        return 0;
    }

    if (send_serial_bytes(state, bytes, (size_t)byte_count)) {
        printf("Hex enviado com sucesso.\n");
        return 1;
    }

    return 0;
}

/**
 * @brief Thread responsavel por detectar o atalho Ctrl+Shift+T durante a comunicacao serial.
 *
 * @param parameter Ponteiro para a estrutura de estado do terminal.
 * @return DWORD Codigo de encerramento da thread.
 */
static DWORD WINAPI serial_hotkey_thread(LPVOID parameter)
{
    SerialState *state = (SerialState *)parameter;
    int combo_previously_pressed = 0;
    int pause_previously_pressed = 0;
    int resume_previously_pressed = 0;

    while (InterlockedCompareExchange(&state->running, 0, 0)) {
        int context_active = is_terminal_hotkey_context_active();
        int ctrl_pressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        int shift_pressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        int t_pressed = (GetAsyncKeyState('T') & 0x8000) != 0;
        int combo_pressed = context_active && ctrl_pressed && shift_pressed && t_pressed;
        int pause_pressed = context_active && !ctrl_pressed && !shift_pressed && (GetAsyncKeyState('P') & 0x8000) != 0;
        int resume_pressed = context_active && !ctrl_pressed && !shift_pressed && (GetAsyncKeyState('R') & 0x8000) != 0;

        if (!context_active) {
            combo_previously_pressed = 0;
            pause_previously_pressed = 0;
            resume_previously_pressed = 0;
            Sleep(50);
            continue;
        }

        if (combo_pressed && !combo_previously_pressed) {
            EnterCriticalSection(&state->console_lock);
            printf("\n[atalho] Ctrl+Shift+T detectado. Encerrando comunicacao serial.\n");
            LeaveCriticalSection(&state->console_lock);
            stop_serial(state);
            break;
        }

        if (pause_pressed && !pause_previously_pressed) {
            EnterCriticalSection(&state->console_lock);
            pause_monitor_display(state);
            LeaveCriticalSection(&state->console_lock);
        }

        if (resume_pressed && !resume_previously_pressed) {
            EnterCriticalSection(&state->console_lock);
            resume_monitor_display(state);
            LeaveCriticalSection(&state->console_lock);
        }

        combo_previously_pressed = combo_pressed;
        pause_previously_pressed = pause_pressed;
        resume_previously_pressed = resume_pressed;
        Sleep(50);
    }

    return 0;
}

/**
 * @brief Thread responsavel por ler continuamente os dados recebidos pela serial.
 *
 * @param parameter Ponteiro para a estrutura de estado do terminal.
 * @return DWORD Codigo de encerramento da thread.
 */
static DWORD WINAPI serial_reader_thread(LPVOID parameter)
{
    SerialState *state = (SerialState *)parameter;
    char buffer[SERIAL_BUFFER_SIZE];

    while (InterlockedCompareExchange(&state->running, 0, 0)) {
        DWORD bytes_read = 0;

        if (state->serial_handle == NULL) {
            if (!try_reconnect_serial(state)) {
                break;
            }
            continue;
        }

        if (!ReadFile(state->serial_handle, buffer, sizeof(buffer), &bytes_read, NULL)) {
            DWORD error_code = GetLastError();

            if (!InterlockedCompareExchange(&state->running, 0, 0) &&
                error_code == ERROR_OPERATION_ABORTED) {
                break;
            }

            close_serial_handle(state);

            if (state->config.reconnect_enabled && InterlockedCompareExchange(&state->running, 0, 0)) {
                continue;
            }

            EnterCriticalSection(&state->console_lock);
            fprintf(stderr, "\n[erro] Falha ao ler da serial. Codigo: %lu\n", error_code);
            LeaveCriticalSection(&state->console_lock);
            InterlockedExchange(&state->running, 0);
            break;
        }

        if (bytes_read > 0) {
            EnterCriticalSection(&state->console_lock);
            process_serial_bytes(state, buffer, (size_t)bytes_read);
            LeaveCriticalSection(&state->console_lock);
        }
    }

    EnterCriticalSection(&state->console_lock);
    flush_partial_serial_line(state);
    LeaveCriticalSection(&state->console_lock);
    return 0;
}

/**
 * @brief Thread responsavel por detectar atalhos durante o monitor CAN ativo.
 *
 * @param parameter Ponteiro para a estrutura de estado do terminal.
 * @return DWORD Codigo de encerramento da thread.
 */
static DWORD WINAPI can_hotkey_thread(LPVOID parameter)
{
    SerialState *state = (SerialState *)parameter;
    int combo_previously_pressed = 0;
    int pause_previously_pressed = 0;
    int resume_previously_pressed = 0;

    while (InterlockedCompareExchange(&state->can_state.running, 0, 0)) {
        int context_active = is_terminal_hotkey_context_active();
        int ctrl_pressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        int shift_pressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        int t_pressed = (GetAsyncKeyState('T') & 0x8000) != 0;
        int esc_pressed = context_active && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        int q_pressed = context_active && !ctrl_pressed && !shift_pressed && (GetAsyncKeyState('Q') & 0x8000) != 0;
        int combo_pressed = context_active && ctrl_pressed && shift_pressed && t_pressed;
        int pause_pressed = context_active && !ctrl_pressed && !shift_pressed && (GetAsyncKeyState('P') & 0x8000) != 0;
        int resume_pressed = context_active && !ctrl_pressed && !shift_pressed && (GetAsyncKeyState('R') & 0x8000) != 0;

        if (!context_active) {
            combo_previously_pressed = 0;
            pause_previously_pressed = 0;
            resume_previously_pressed = 0;
            Sleep(50);
            continue;
        }

        if ((combo_pressed && !combo_previously_pressed) || esc_pressed || q_pressed) {
            EnterCriticalSection(&state->console_lock);
            printf("\n[atalho] Encerrando monitor CAN.\n");
            LeaveCriticalSection(&state->console_lock);
            stop_can_interface(state);
            break;
        }

        if (pause_pressed && !pause_previously_pressed) {
            EnterCriticalSection(&state->console_lock);
            pause_monitor_display(state);
            LeaveCriticalSection(&state->console_lock);
        }

        if (resume_pressed && !resume_previously_pressed) {
            EnterCriticalSection(&state->console_lock);
            resume_monitor_display(state);
            LeaveCriticalSection(&state->console_lock);
        }

        combo_previously_pressed = combo_pressed;
        pause_previously_pressed = pause_pressed;
        resume_previously_pressed = resume_pressed;
        Sleep(50);
    }

    return 0;
}

/**
 * @brief Thread responsavel por ler continuamente frames ASCII do adaptador SLCAN.
 *
 * @param parameter Ponteiro para a estrutura de estado do terminal.
 * @return DWORD Codigo de encerramento da thread.
 */
static DWORD WINAPI can_reader_thread(LPVOID parameter)
{
    SerialState *state = (SerialState *)parameter;
    char buffer[SERIAL_BUFFER_SIZE];

    while (InterlockedCompareExchange(&state->can_state.running, 0, 0)) {
        DWORD bytes_read = 0;

        if (state->can_state.handle == NULL) {
            break;
        }

        if (!ReadFile(state->can_state.handle, buffer, sizeof(buffer), &bytes_read, NULL)) {
            DWORD error_code = GetLastError();

            if (!InterlockedCompareExchange(&state->can_state.running, 0, 0) &&
                error_code == ERROR_OPERATION_ABORTED) {
                break;
            }

            EnterCriticalSection(&state->console_lock);
            fprintf(stderr, "\n[erro] Falha ao ler do backend CAN/SLCAN. Codigo: %lu\n", error_code);
            LeaveCriticalSection(&state->console_lock);
            InterlockedExchange(&state->can_state.running, 0);
            break;
        }

        if (bytes_read > 0) {
            EnterCriticalSection(&state->console_lock);
            process_can_bytes(state, buffer, (size_t)bytes_read);
            LeaveCriticalSection(&state->console_lock);
        }
    }

    EnterCriticalSection(&state->console_lock);
    flush_partial_can_line(state);
    LeaveCriticalSection(&state->console_lock);
    return 0;
}

/**
 * @brief Lista as portas COM presentes no sistema.
 *
 * @param ports Matriz de buffers para armazenar os nomes das portas.
 * @param max_ports Quantidade maxima de portas a registrar.
 * @return int Quantidade de portas encontradas.
 */
static int list_available_ports(char ports[][32], int max_ports)
{
    int count = 0;
    int index;

    for (index = 1; index <= MAX_COM_PORTS && count < max_ports; index++) {
        char port_name[32];
        char device_target[128];

        snprintf(port_name, sizeof(port_name), "COM%d", index);
        if (QueryDosDeviceA(port_name, device_target, (DWORD)sizeof(device_target)) != 0) {
            snprintf(ports[count], 32, "%s", port_name);
            count++;
        }
    }

    return count;
}

/**
 * @brief Limpa todo o conteudo visivel do console do Windows.
 */
static void clear_console(void)
{
    HANDLE console_handle;
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    DWORD written = 0;
    DWORD cell_count;
    COORD home = {0, 0};

    console_handle = get_console_output_handle();
    if (console_handle == INVALID_HANDLE_VALUE || console_handle == NULL) {
        return;
    }

    if (!GetConsoleScreenBufferInfo(console_handle, &console_info)) {
        return;
    }

    cell_count = (DWORD)(console_info.dwSize.X * console_info.dwSize.Y);
    FillConsoleOutputCharacterA(console_handle, ' ', cell_count, home, &written);
    FillConsoleOutputAttribute(console_handle, console_info.wAttributes, cell_count, home, &written);
    SetConsoleCursorPosition(console_handle, home);
}

/**
 * @brief Exibe o prompt principal do terminal.
 */
static void print_prompt(void)
{
    printf("\nbmt> ");
    fflush(stdout);
}

/**
 * @brief Aplica uma chave de configuracao lida do arquivo persistente.
 *
 * @param config Estrutura de configuracao a ser atualizada.
 * @param key Nome da chave lida.
 * @param value Valor textual associado a chave.
 * @return int Retorna 1 quando a chave foi aplicada com sucesso, caso contrario 0.
 */
static int apply_config_entry(SerialConfig *config, const char *key, const char *value)
{
    ParityMode parity;
    StopBitsMode stop_bits;
    FlowControlMode flow_control;
    MonitorMode monitor_mode;
    LogFilterMode filter_mode;
    ThemeMode theme_mode;
    char *end = NULL;
    unsigned long numeric_value;

    if (equals_ignore_case(key, "com_port")) {
        snprintf(config->com_port, sizeof(config->com_port), "%s", value);
        return 1;
    }
    if (equals_ignore_case(key, "baud_rate")) {
        numeric_value = strtoul(value, &end, 10);
        if (end == value || *end != '\0' || numeric_value == 0) {
            return 0;
        }
        config->baud_rate = (DWORD)numeric_value;
        return 1;
    }
    if (equals_ignore_case(key, "data_bits")) {
        numeric_value = strtoul(value, &end, 10);
        if (end == value || *end != '\0' || numeric_value < 5 || numeric_value > 8) {
            return 0;
        }
        config->data_bits = (BYTE)numeric_value;
        return 1;
    }
    if (equals_ignore_case(key, "parity")) {
        if (!parse_parity(value, &parity)) {
            return 0;
        }
        config->parity = parity;
        return 1;
    }
    if (equals_ignore_case(key, "stop_bits")) {
        if (!parse_stop_bits(value, &stop_bits)) {
            return 0;
        }
        config->stop_bits = stop_bits;
        return 1;
    }
    if (equals_ignore_case(key, "flow_control")) {
        if (!parse_flow_control(value, &flow_control)) {
            return 0;
        }
        config->flow_control = flow_control;
        return 1;
    }
    if (equals_ignore_case(key, "esp32_logs")) {
        if (equals_ignore_case(value, "on")) {
            config->esp32_log_colors_enabled = 1;
            return 1;
        }
        if (equals_ignore_case(value, "off")) {
            config->esp32_log_colors_enabled = 0;
            return 1;
        }
        return 0;
    }
    if (equals_ignore_case(key, "timestamp")) {
        if (equals_ignore_case(value, "on")) {
            config->timestamp_enabled = 1;
            return 1;
        }
        if (equals_ignore_case(value, "off")) {
            config->timestamp_enabled = 0;
            return 1;
        }
        return 0;
    }
    if (equals_ignore_case(key, "logfile")) {
        if (equals_ignore_case(value, "on")) {
            config->logfile_enabled = 1;
            return 1;
        }
        if (equals_ignore_case(value, "off")) {
            config->logfile_enabled = 0;
            return 1;
        }
        return 0;
    }
    if (equals_ignore_case(key, "logfile_path")) {
        snprintf(config->logfile_path, sizeof(config->logfile_path), "%s", value);
        return 1;
    }
    if (equals_ignore_case(key, "autosave")) {
        if (equals_ignore_case(value, "on")) {
            config->autosave_enabled = 1;
            return 1;
        }
        if (equals_ignore_case(value, "off")) {
            config->autosave_enabled = 0;
            return 1;
        }
        return 0;
    }
    if (equals_ignore_case(key, "reconnect")) {
        if (equals_ignore_case(value, "on")) {
            config->reconnect_enabled = 1;
            return 1;
        }
        if (equals_ignore_case(value, "off")) {
            config->reconnect_enabled = 0;
            return 1;
        }
        return 0;
    }
    if (equals_ignore_case(key, "monitor")) {
        if (!parse_monitor_mode(value, &monitor_mode)) {
            return 0;
        }
        config->monitor_mode = monitor_mode;
        return 1;
    }
    if (equals_ignore_case(key, "filter")) {
        if (!parse_filter_mode(value, &filter_mode)) {
            return 0;
        }
        config->filter_mode = filter_mode;
        return 1;
    }
    if (equals_ignore_case(key, "theme")) {
        if (!parse_theme_mode(value, &theme_mode)) {
            return 0;
        }
        config->theme_mode = theme_mode;
        return 1;
    }

    return 0;
}

/**
 * @brief Carrega a configuracao persistida em disco, quando disponivel.
 *
 * @param state Estado global do terminal que recebera os valores carregados.
 * @return int Retorna 1 quando uma configuracao valida foi carregada, caso contrario 0.
 */
static int load_config(SerialState *state)
{
    FILE *file;
    char line[INPUT_BUFFER_SIZE];
    int loaded_any_value = 0;

    file = fopen(CONFIG_FILE_PATH, "r");
    if (file == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *separator;
        char *key;
        char *value;

        trim_newline(line);
        key = trim_whitespace(line);

        if (key[0] == '\0' || key[0] == '#') {
            continue;
        }

        separator = strchr(key, '=');
        if (separator == NULL) {
            continue;
        }

        *separator = '\0';
        value = trim_whitespace(separator + 1);
        key = trim_whitespace(key);

        if (apply_config_entry(&state->config, key, value) ||
            can_apply_config_entry(&state->can_state.config, key, value) ||
            web_ui_apply_config_entry(&state->web_ui.config, key, value)) {
            loaded_any_value = 1;
        }
    }

    fclose(file);
    return loaded_any_value;
}

/**
 * @brief Salva a configuracao atual em disco para reutilizacao futura.
 *
 * @param state Estado global do terminal com as configuracoes a serem persistidas.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int save_config(const SerialState *state)
{
    FILE *file = fopen(CONFIG_FILE_PATH, "w");

    if (file == NULL) {
        return 0;
    }

    fprintf(file, "com_port=%s\n", state->config.com_port);
    fprintf(file, "baud_rate=%lu\n", (unsigned long)state->config.baud_rate);
    fprintf(file, "data_bits=%u\n", (unsigned int)state->config.data_bits);
    fprintf(file, "parity=%s\n", parity_to_string(state->config.parity));
    fprintf(file, "stop_bits=%s\n", stop_bits_to_string(state->config.stop_bits));
    fprintf(file, "flow_control=%s\n", flow_control_to_string(state->config.flow_control));
    fprintf(file, "esp32_logs=%s\n", bool_to_on_off(state->config.esp32_log_colors_enabled));
    fprintf(file, "timestamp=%s\n", bool_to_on_off(state->config.timestamp_enabled));
    fprintf(file, "logfile=%s\n", bool_to_on_off(state->config.logfile_enabled));
    fprintf(file, "logfile_path=%s\n", state->config.logfile_path);
    fprintf(file, "autosave=%s\n", bool_to_on_off(state->config.autosave_enabled));
    fprintf(file, "reconnect=%s\n", bool_to_on_off(state->config.reconnect_enabled));
    fprintf(file, "monitor=%s\n", monitor_mode_to_string(state->config.monitor_mode));
    fprintf(file, "filter=%s\n", filter_mode_to_string(state->config.filter_mode));
    fprintf(file, "theme=%s\n", theme_mode_to_string(state->config.theme_mode));
    can_save_config(file, &state->can_state.config);
    web_ui_save_config(file, &state->web_ui.config);
    fclose(file);
    return 1;
}

/**
 * @brief Salva automaticamente a configuracao quando o recurso estiver habilitado.
 *
 * @param state Estado global do terminal serial.
 */
static void auto_save_if_enabled(const SerialState *state)
{
    if (state->config.autosave_enabled) {
        save_config(state);
    }
}

/**
 * @brief Retorna a cor usada para destacar frames CAN conforme a direcao.
 *
 * @param direction Direcao do frame CAN.
 * @return WORD Atributo de cor do console do Windows.
 */
static WORD get_can_frame_color(CanDirection direction)
{
    switch (direction) {
    case CAN_DIR_TX:
        return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case CAN_DIR_RX:
    default:
        return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    }
}

/**
 * @brief Monta o payload hexadecimal de um frame CAN em uma string.
 *
 * @param frame Frame a ser formatado.
 * @param buffer Buffer de destino.
 * @param buffer_size Tamanho do buffer de destino.
 */
static void format_can_payload(const CanFrame *frame, char *buffer, size_t buffer_size)
{
    unsigned int index;
    size_t offset = 0;

    if (buffer_size == 0U) {
        return;
    }

    buffer[0] = '\0';

    if (frame->is_remote || frame->data_length == 0U) {
        snprintf(buffer, buffer_size, frame->is_remote ? "RTR" : "-");
        return;
    }

    for (index = 0; index < frame->data_length && offset + 3U < buffer_size; index++) {
        offset += (size_t)snprintf(buffer + offset,
                                   buffer_size - offset,
                                   index == 0U ? "%02X" : " %02X",
                                   frame->data[index]);
    }
}

/**
 * @brief Imprime o cabecalho da visualizacao tabular do monitor CAN.
 *
 * @param state Estado global do terminal.
 */
static void print_can_table_header(SerialState *state)
{
    if (state->can_state.table_header_printed) {
        return;
    }

    if (state->can_state.config.timestamp_enabled) {
        printf("TIME           DIR  ID          TYPE  DLC  DATA\n");
    } else {
        printf("DIR  ID          TYPE  DLC  DATA\n");
    }

    state->can_state.table_header_printed = 1;
}

/**
 * @brief Escreve uma linha do monitor CAN no arquivo de log configurado.
 *
 * @param state Estado global do terminal.
 * @param text Linha a ser gravada.
 */
static void write_can_log_line(const SerialState *state, const char *text)
{
    FILE *file;

    if (state->can_state.config.logfile_path[0] == '\0') {
        return;
    }

    file = fopen(state->can_state.config.logfile_path, "a");
    if (file == NULL) {
        return;
    }

    fputs(text, file);
    fclose(file);
}

/**
 * @brief Imprime um frame CAN no terminal em modo raw ou table.
 *
 * @param state Estado global do terminal.
 * @param frame Frame CAN ja interpretado.
 */
static void print_can_frame(SerialState *state, const CanFrame *frame)
{
    HANDLE console_handle;
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    WORD original_attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    char timestamp[32];
    char payload[64];
    char log_output[256];
    char id_text[16];
    const char *direction_text;
    const char *type_text;
    int is_paused;

    if (state->can_state.config.filter_id_enabled &&
        frame->id != state->can_state.config.filter_id) {
        state->can_state.stats.filtered_count++;
        return;
    }

    format_can_payload(frame, payload, sizeof(payload));
    snprintf(timestamp, sizeof(timestamp), "%02u:%02u:%02u.%03u",
             (unsigned int)frame->timestamp.wHour,
             (unsigned int)frame->timestamp.wMinute,
             (unsigned int)frame->timestamp.wSecond,
             (unsigned int)frame->timestamp.wMilliseconds);
    direction_text = frame->direction == CAN_DIR_TX ? "TX" : "RX";
    type_text = frame->is_remote ? (frame->is_extended ? "XTR" : "RTR") :
                                   (frame->is_extended ? "EXT" : "STD");
    snprintf(id_text, sizeof(id_text), frame->is_extended ? "0x%08X" : "0x%03X", frame->id);

    if (state->can_state.config.view_mode == CAN_VIEW_TABLE) {
        if (state->can_state.config.timestamp_enabled) {
            snprintf(log_output, sizeof(log_output), "%-14s %-3s  %-11s %-4s  %u    %s\n",
                     timestamp, direction_text, id_text, type_text, (unsigned int)frame->dlc, payload);
        } else {
            snprintf(log_output, sizeof(log_output), "%-3s  %-11s %-4s  %u    %s\n",
                     direction_text, id_text, type_text, (unsigned int)frame->dlc, payload);
        }
    } else if (state->can_state.config.timestamp_enabled) {
        snprintf(log_output, sizeof(log_output), "[%s] [can][%s] ID:%s DLC:%u TYPE:%s DATA:%s\n",
                 timestamp, direction_text, id_text, (unsigned int)frame->dlc, type_text, payload);
    } else {
        snprintf(log_output, sizeof(log_output), "[can][%s] ID:%s DLC:%u TYPE:%s DATA:%s\n",
                 direction_text, id_text, (unsigned int)frame->dlc, type_text, payload);
    }

    serial_monitor_push_line(&state->can_monitor, log_output);

    is_paused = InterlockedCompareExchange((LONG *)&state->paused, 0, 0) != 0;
    if (is_paused) {
        enqueue_paused_output(state, log_output);
        write_can_log_line(state, log_output);
        return;
    }

    console_handle = get_console_output_handle();
    if (console_handle != INVALID_HANDLE_VALUE && console_handle != NULL &&
        GetConsoleScreenBufferInfo(console_handle, &console_info)) {
        original_attributes = console_info.wAttributes;
    }

    if (state->can_state.config.view_mode == CAN_VIEW_TABLE) {
        print_can_table_header(state);
    }

    if (state->can_state.config.colors_enabled) {
        SetConsoleTextAttribute(console_handle, get_can_frame_color(frame->direction));
        printf("%s", log_output);
        restore_console_color(console_handle, original_attributes);
    } else {
        printf("%s", log_output);
    }

    write_can_log_line(state, log_output);
}

/**
 * @brief Interpreta uma linha SLCAN recebida do adaptador CAN.
 *
 * @param state Estado global do terminal.
 * @param line Linha ASCII completa recebida do backend SLCAN.
 */
static void process_can_line(SerialState *state, const char *line)
{
    CanFrame frame;
    char can_log_line[320];

    if (line == NULL || line[0] == '\0') {
        return;
    }

    if (strcmp(line, "000") == 0) {
        return;
    }

    if (!can_slcan_parse_frame(line, &frame)) {
        state->can_state.stats.parse_error_count++;
        snprintf(can_log_line, sizeof(can_log_line), "[can] Linha SLCAN nao reconhecida: %s", line);
        serial_monitor_push_line(&state->can_monitor, can_log_line);
        if (!InterlockedCompareExchange((LONG *)&state->paused, 0, 0)) {
            printf("%s\n", can_log_line);
        } else {
            enqueue_paused_output(state, "[can] Linha SLCAN nao reconhecida durante pausa.\n");
        }
        return;
    }

    state->can_state.stats.rx_count++;
    state->can_state.stats.last_id = frame.id;
    web_ui_update_from_frame(&state->web_ui, &state->can_state.dbc_vcu_database, &frame);
    print_can_frame(state, &frame);
}

/**
 * @brief Processa um bloco bruto recebido do adaptador SLCAN.
 *
 * @param state Estado global do terminal.
 * @param buffer Buffer recebido.
 * @param bytes_read Quantidade de bytes validos no buffer.
 */
static void process_can_bytes(SerialState *state, const char *buffer, size_t bytes_read)
{
    size_t index;

    for (index = 0; index < bytes_read; index++) {
        char current = buffer[index];

        if (current == '\n') {
            continue;
        }

        if (current == '\r') {
            state->can_state.partial_line[state->can_state.partial_length] = '\0';
            process_can_line(state, state->can_state.partial_line);
            state->can_state.partial_length = 0;
            state->can_state.partial_line[0] = '\0';
            continue;
        }

        if (state->can_state.partial_length + 1U < sizeof(state->can_state.partial_line)) {
            state->can_state.partial_line[state->can_state.partial_length++] = current;
            state->can_state.partial_line[state->can_state.partial_length] = '\0';
        } else {
            state->can_state.stats.dropped_count++;
            state->can_state.partial_length = 0;
            state->can_state.partial_line[0] = '\0';
        }
    }
}

/**
 * @brief Despeja a linha parcial CAN acumulada quando o monitor e encerrado.
 *
 * @param state Estado global do terminal.
 */
static void flush_partial_can_line(SerialState *state)
{
    if (state->can_state.partial_length == 0U) {
        return;
    }

    state->can_state.partial_line[state->can_state.partial_length] = '\0';
    process_can_line(state, state->can_state.partial_line);
    state->can_state.partial_length = 0;
    state->can_state.partial_line[0] = '\0';
}
/**
 * @brief Exibe a mensagem inicial do Terminal BMT.
 *
 * @param config_loaded Indica se uma configuracao persistida foi carregada.
 */
static void print_banner(int config_loaded)
{
    printf("Ola, eu sou o Terminal BMT\n");
    printf("Status: disponivel para configurar e iniciar comunicacao serial.\n");
    if (config_loaded) {
        printf("Configuracao anterior carregada de %s.\n", CONFIG_FILE_PATH);
    }
    printf("Digite 'bmt -list' para ver os comandos.\n\n");
}

/**
 * @brief Exibe ajuda detalhada para um comando especifico.
 *
 * @param command Nome do comando solicitado.
 */
static void print_command_help(const char *command)
{
    if (equals_ignore_case(command, "-can")) {
        print_can_help();
        return;
    }
    if (equals_ignore_case(command, "-ui")) {
        printf("bmt -ui status\n");
        printf("bmt -ui on|off\n");
        printf("bmt -ui open\n");
        printf("bmt -ui port <valor>\n");
        printf("  Controla o dashboard HTML local para o VCU decodificado por DBC.\n");
        return;
    }
    if (equals_ignore_case(command, "-send")) {
        printf("bmt -send <texto>\n");
        printf("  Envia texto puro pela serial ativa.\n");
        return;
    }
    if (equals_ignore_case(command, "-sendhex")) {
        printf("bmt -sendhex <hex>\n");
        printf("  Envia bytes em hexadecimal, ex: bmt -sendhex AA 01 FF.\n");
        return;
    }
    if (equals_ignore_case(command, "-timestamp")) {
        printf("bmt -timestamp on|off\n");
        printf("  Ativa ou desativa horario local antes de cada linha recebida.\n");
        return;
    }
    if (equals_ignore_case(command, "-filter")) {
        printf("bmt -filter info|warning|error|debug|verbose|all\n");
        printf("  Filtra logs ESP32 por nivel quando o monitor estiver em modo esp32.\n");
        return;
    }
    if (equals_ignore_case(command, "-logfile")) {
        printf("bmt -logfile on|off\n");
        printf("bmt -logfile path <arquivo>\n");
        printf("  Controla a gravacao da saida serial em arquivo.\n");
        return;
    }
    if (equals_ignore_case(command, "-autosave")) {
        printf("bmt -autosave on|off\n");
        printf("  Salva automaticamente a configuracao a cada alteracao.\n");
        return;
    }
    if (equals_ignore_case(command, "-reset")) {
        printf("bmt -reset\n");
        printf("  Restaura os parametros padrao do terminal.\n");
        return;
    }
    if (equals_ignore_case(command, "-reload")) {
        printf("bmt -reload\n");
        printf("  Recarrega o arquivo %s.\n", CONFIG_FILE_PATH);
        return;
    }
    if (equals_ignore_case(command, "-monitor")) {
        printf("bmt -monitor raw|esp32\n");
        printf("  Alterna entre modo bruto e modo interpretado para logs ESP32.\n");
        return;
    }
    if (equals_ignore_case(command, "-reconnect")) {
        printf("bmt -reconnect on|off\n");
        printf("  Tenta reabrir a serial quando a conexao cair inesperadamente.\n");
        return;
    }
    if (equals_ignore_case(command, "-theme")) {
        printf("bmt -theme default|light|highcontrast\n");
        printf("  Altera o esquema de cores usado nos logs coloridos.\n");
        return;
    }
    if (equals_ignore_case(command, "-history")) {
        printf("bmt -history\n");
        printf("  Exibe o historico de comandos da sessao atual.\n");
        return;
    }
    if (equals_ignore_case(command, "-about")) {
        printf("bmt -about\n");
        printf("  Mostra versao, compilador e caminhos usados pelo terminal.\n");
        return;
    }

    printf("Sem ajuda detalhada para %s. Use 'bmt -list'.\n", command);
}

/**
 * @brief Exibe a lista de comandos suportados pelo terminal.
 */
static void print_help(void)
{
    printf("Comandos disponiveis:\n");
    printf("  bmt -list                         Lista todos os comandos\n");
    printf("  bmt -help <comando>               Mostra ajuda detalhada por comando\n");
    printf("  bmt -com                          Lista as portas COM disponiveis e a atual\n");
    printf("  bmt -com refresh                  Atualiza a lista de portas COM\n");
    printf("  bmt -com COMx                     Define a porta COM atual\n");
    printf("  bmt -baud                         Mostra o baud rate atual\n");
    printf("  bmt -baud <valor>                 Define o baud rate, ex: 9600, 115200\n");
    printf("  bmt -parity                       Mostra a paridade atual\n");
    printf("  bmt -parity <modo>                none, odd, even, mark, space\n");
    printf("  bmt -databits                     Mostra os bits de dados atuais\n");
    printf("  bmt -databits <valor>             5, 6, 7 ou 8\n");
    printf("  bmt -stopbits                     Mostra os stop bits atuais\n");
    printf("  bmt -stopbits <valor>             1, 1.5 ou 2\n");
    printf("  bmt -flow                         Mostra o controle de fluxo atual\n");
    printf("  bmt -flow <modo>                  none, xonxoff, rtscts, dsrdtr\n");
    printf("  bmt -can                          Mostra o status atual do subsistema CAN\n");
    printf("  bmt -can help                     Mostra a ajuda detalhada do modo CAN\n");
    printf("  bmt -can list                     Lista portas COM candidatas para CAN/SLCAN\n");
    printf("  bmt -can com [COMx]               Mostra ou define a porta COM do CANable\n");
    printf("  bmt -can bitrate [valor]          Mostra ou define o bitrate CAN\n");
    printf("  bmt -can view [raw|table]         Mostra ou define o modo de exibicao CAN\n");
    printf("  bmt -can timestamp on|off         Ativa ou desativa timestamp no monitor CAN\n");
    printf("  bmt -can color on|off             Ativa ou desativa cores no monitor CAN\n");
    printf("  bmt -can serialmonitor on|off     Liga ou desliga a subview CAN Monitor da UI\n");
    printf("  bmt -can filter id <hex>          Filtra um identificador CAN especifico\n");
    printf("  bmt -can filter clear             Remove o filtro de identificador CAN\n");
    printf("  bmt -can dbc info|load|unload     Controla os dicionarios DBC VCU e Inverter\n");
    printf("  bmt -can open|start|stop|close    Controla a sessao CAN/SLCAN\n");
    printf("  bmt -can send <id> <bytes...>     Envia um frame CAN padrao de 11 bits\n");
    printf("  bmt -can stats                    Mostra estatisticas da sessao CAN\n");
    printf("  bmt -can save|reload              Salva ou recarrega a configuracao CAN\n");
    printf("  bmt -ui status                    Mostra o estado atual da UI web\n");
    printf("  bmt -ui on|off                    Inicia ou encerra o dashboard HTML local\n");
    printf("  bmt -ui open                      Abre o dashboard no navegador padrao\n");
    printf("  bmt -ui port <valor>              Define a porta HTTP local da UI\n");
    printf("  bmt -send <texto>                 Envia texto manualmente pela serial\n");
    printf("  bmt -sendhex <hex>                Envia bytes em hexadecimal\n");
    printf("  bmt -logs esp32 on|off            Ativa ou desativa cores para logs ESP32\n");
    printf("  bmt -timestamp on|off             Ativa ou desativa horario nas linhas recebidas\n");
    printf("  bmt -filter <nivel>               info, warning, error, debug, verbose, all\n");
    printf("  bmt -monitor raw|esp32            Alterna entre monitor bruto e interpretado\n");
    printf("  bmt -logfile                      Mostra status do log em arquivo\n");
    printf("  bmt -logfile on|off               Ativa ou desativa gravacao em arquivo\n");
    printf("  bmt -logfile path <arquivo>       Define o arquivo de log\n");
    printf("  bmt -autosave on|off              Ativa ou desativa salvamento automatico\n");
    printf("  bmt -reconnect on|off             Ativa ou desativa reconexao automatica\n");
    printf("  bmt -theme                        Mostra o tema atual\n");
    printf("  bmt -theme <tema>                 default, light, highcontrast\n");
    printf("  bmt -save                         Salva a configuracao atual para a proxima execucao\n");
    printf("  bmt -reload                       Recarrega o arquivo de configuracao\n");
    printf("  bmt -reset                        Restaura os valores padrao da configuracao\n");
    printf("  bmt -clear                        Limpa a tela do terminal\n");
    printf("  bmt -status                       Mostra toda a configuracao atual\n");
    printf("  bmt -history                      Mostra o historico de comandos da sessao\n");
    printf("  bmt -about                        Mostra informacoes do projeto\n");
    printf("  bmt -start                        Abre a porta e inicia a escuta serial\n");
    printf("  bmt -stop                         Para a escuta e fecha a porta serial\n");
    printf("  p                                 Pausa a exibicao do monitor serial ativo\n");
    printf("  r                                 Retoma a exibicao e despeja a fila pausada\n");
    printf("  Ctrl+Shift+T                      Atalho para encerrar a comunicacao serial ativa\n");
    printf("  bmt -exit                         Fecha o Terminal BMT\n");
}

/**
 * @brief Exibe a ajuda detalhada do conjunto de comandos CAN/SLCAN.
 */
static void print_can_help(void)
{
    printf("Comandos CAN disponiveis:\n");
    printf("  bmt -can                          Mostra o status atual do subsistema CAN\n");
    printf("  bmt -can list                     Lista portas COM candidatas para CAN/SLCAN\n");
    printf("  bmt -can com [COMx]               Mostra ou define a porta COM do CANable\n");
    printf("  bmt -can bitrate [valor]          Mostra ou define o bitrate CAN\n");
    printf("  bmt -can view [raw|table]         Mostra ou define o modo de exibicao CAN\n");
    printf("  bmt -can timestamp on|off         Ativa ou desativa timestamp local\n");
    printf("  bmt -can color on|off             Ativa ou desativa colorizacao do monitor CAN\n");
    printf("  bmt -can serialmonitor on|off     Liga ou desliga a subview CAN Monitor da UI\n");
    printf("  bmt -can filter id <hex>          Aplica um filtro por identificador CAN\n");
    printf("  bmt -can filter clear             Remove o filtro por identificador CAN\n");
    printf("  bmt -can dbc info                 Exibe o resumo dos DBCs carregados\n");
    printf("  bmt -can dbc load vcu <arquivo>   Carrega o DBC da VCU\n");
    printf("  bmt -can dbc load inverter <arq>  Carrega o DBC dos inversores\n");
    printf("  bmt -can dbc unload vcu|inverter  Remove um DBC carregado da memoria\n");
    printf("  bmt -can inverter status          Mostra as bases configuradas dos inversores\n");
    printf("  bmt -can inverter base <lado> <id> Define a base do Inverter A/B\n");
    printf("  bmt -can open                     Abre a porta COM e configura o SLCAN\n");
    printf("  bmt -can start                    Abre o barramento CAN no adaptador SLCAN\n");
    printf("  bmt -can stop                     Fecha o barramento sem soltar a COM\n");
    printf("  bmt -can close                    Fecha o barramento e libera a COM\n");
    printf("  bmt -can send <id> <bytes...>     Envia um frame CAN padrao de 11 bits\n");
    printf("  bmt -can stats                    Exibe estatisticas da sessao CAN\n");
    printf("  bmt -can save                     Persiste a configuracao CAN atual\n");
    printf("  bmt -can reload                   Recarrega a configuracao CAN de %s\n", CONFIG_FILE_PATH);
    printf("  bmt -ui on                        Sobe o dashboard HTML local usando o DBC carregado\n");
    printf("  bmt -ui open                      Abre o dashboard no navegador padrao\n");
}

/**
 * @brief Exibe a configuracao serial atual e o estado da comunicacao.
 *
 * @param state Estado global do terminal serial.
 */
static void print_status(const SerialState *state)
{
    printf("Configuracao atual:\n");
    printf("  Porta COM: %s\n", state->config.com_port[0] == '\0' ? "(nao definida)" : state->config.com_port);
    printf("  Baud rate: %lu\n", (unsigned long)state->config.baud_rate);
    printf("  Paridade: %s\n", parity_to_string(state->config.parity));
    printf("  Data bits: %u\n", (unsigned int)state->config.data_bits);
    printf("  Stop bits: %s\n", stop_bits_to_string(state->config.stop_bits));
    printf("  Flow control: %s\n", flow_control_to_string(state->config.flow_control));
    printf("  Monitor: %s\n", monitor_mode_to_string(state->config.monitor_mode));
    printf("  Filtro ESP32: %s\n", filter_mode_to_string(state->config.filter_mode));
    printf("  Logs ESP32 coloridos: %s\n", bool_to_on_off(state->config.esp32_log_colors_enabled));
    printf("  Timestamp: %s\n", bool_to_on_off(state->config.timestamp_enabled));
    printf("  Log em arquivo: %s\n", bool_to_on_off(state->config.logfile_enabled));
    printf("  Caminho do log: %s\n", state->config.logfile_path);
    printf("  Autosave: %s\n", bool_to_on_off(state->config.autosave_enabled));
    printf("  Reconnect: %s\n", bool_to_on_off(state->config.reconnect_enabled));
    printf("  Tema: %s\n", theme_mode_to_string(state->config.theme_mode));
    printf("  Pausa do monitor: %s\n", InterlockedCompareExchange((LONG *)&state->paused, 0, 0) ? "ativa" : "inativa");
    printf("  Comunicacao: %s\n", InterlockedCompareExchange((LONG *)&state->running, 0, 0) ? "ativa" : "parada");
    printf("  CAN COM: %s\n", state->can_state.config.com_port[0] == '\0' ? "(nao definida)" : state->can_state.config.com_port);
    printf("  CAN bitrate: %u\n", state->can_state.config.bitrate);
    printf("  CAN view: %s\n", can_view_mode_to_string(state->can_state.config.view_mode));
    printf("  CAN timestamp: %s\n", bool_to_on_off(state->can_state.config.timestamp_enabled));
    printf("  CAN cores: %s\n", bool_to_on_off(state->can_state.config.colors_enabled));
    printf("  CAN serial monitor web: %s\n", bool_to_on_off(state->can_state.config.ui_can_monitor_enabled));
    if (state->can_state.config.filter_id_enabled) {
        printf("  CAN filtro ID: 0x%03X\n", state->can_state.config.filter_id);
    } else {
        printf("  CAN filtro ID: (desativado)\n");
    }
    printf("  CAN DBC VCU: %s\n", state->can_state.dbc_vcu_database.loaded ? "carregado" : "nao carregado");
    if (state->can_state.dbc_vcu_database.loaded) {
        printf("  CAN DBC VCU path: %s\n", state->can_state.dbc_vcu_database.path);
        printf("  CAN DBC VCU mensagens: %d\n", state->can_state.dbc_vcu_database.message_count);
        printf("  CAN DBC VCU sinais: %d\n", state->can_state.dbc_vcu_database.total_signal_count);
    } else if (state->can_state.config.dbc_vcu_path[0] != '\0') {
        printf("  CAN DBC VCU path: %s\n", state->can_state.config.dbc_vcu_path);
    }
    printf("  CAN DBC Inverter: %s\n", state->can_state.dbc_inverter_database.loaded ? "carregado" : "nao carregado");
    if (state->can_state.dbc_inverter_database.loaded) {
        printf("  CAN DBC Inverter path: %s\n", state->can_state.dbc_inverter_database.path);
        printf("  CAN DBC Inverter mensagens: %d\n", state->can_state.dbc_inverter_database.message_count);
        printf("  CAN DBC Inverter sinais: %d\n", state->can_state.dbc_inverter_database.total_signal_count);
    } else if (state->can_state.config.dbc_inverter_path[0] != '\0') {
        printf("  CAN DBC Inverter path: %s\n", state->can_state.config.dbc_inverter_path);
    }
    printf("  UI habilitada: %s\n", state->web_ui.config.enabled ? "on" : "off");
    printf("  UI web: %s\n", InterlockedCompareExchange((LONG *)&state->web_ui.running, 0, 0) ? "ativa" : "parada");
    printf("  UI porta: %u\n", (unsigned int)state->web_ui.config.port);
    printf("  CAN aberto: %s\n", InterlockedCompareExchange((LONG *)&state->can_state.opened, 0, 0) ? "sim" : "nao");
    printf("  CAN ativo: %s\n", InterlockedCompareExchange((LONG *)&state->can_state.running, 0, 0) ? "sim" : "nao");
    printf("  Inverter A base: %s\n", state->can_state.config.inverter_left_base_set ? "(definida)" : "(nao definida)");
    if (state->can_state.config.inverter_left_base_set) {
        printf("  Inverter A base ID: 0x%08X\n", state->can_state.config.inverter_left_base);
    }
    printf("  Inverter B base: %s\n", state->can_state.config.inverter_right_base_set ? "(definida)" : "(nao definida)");
    if (state->can_state.config.inverter_right_base_set) {
        printf("  Inverter B base ID: 0x%08X\n", state->can_state.config.inverter_right_base);
    }
}

/**
 * @brief Exibe o estado resumido do subsistema CAN/SLCAN.
 *
 * @param state Estado global do terminal.
 */
static void print_can_status(const SerialState *state)
{
    printf("Status CAN:\n");
    printf("  Porta COM: %s\n", state->can_state.config.com_port[0] == '\0' ? "(nao definida)" : state->can_state.config.com_port);
    printf("  Bitrate: %u\n", state->can_state.config.bitrate);
    printf("  View: %s\n", can_view_mode_to_string(state->can_state.config.view_mode));
    printf("  Timestamp: %s\n", bool_to_on_off(state->can_state.config.timestamp_enabled));
    printf("  Cores: %s\n", bool_to_on_off(state->can_state.config.colors_enabled));
    printf("  Serial monitor web: %s\n", bool_to_on_off(state->can_state.config.ui_can_monitor_enabled));
    if (state->can_state.config.filter_id_enabled) {
        printf("  Filtro ID: 0x%03X\n", state->can_state.config.filter_id);
    } else {
        printf("  Filtro ID: (desativado)\n");
    }
    printf("  Log CAN: %s\n", state->can_state.config.logfile_path);
    printf("  DBC VCU carregado: %s\n", state->can_state.dbc_vcu_database.loaded ? "sim" : "nao");
    if (state->can_state.dbc_vcu_database.loaded) {
        printf("  DBC VCU path: %s\n", state->can_state.dbc_vcu_database.path);
        printf("  DBC VCU mensagens: %d\n", state->can_state.dbc_vcu_database.message_count);
        printf("  DBC VCU sinais: %d\n", state->can_state.dbc_vcu_database.total_signal_count);
    } else if (state->can_state.config.dbc_vcu_path[0] != '\0') {
        printf("  DBC VCU path: %s\n", state->can_state.config.dbc_vcu_path);
    }
    printf("  DBC Inverter carregado: %s\n", state->can_state.dbc_inverter_database.loaded ? "sim" : "nao");
    if (state->can_state.dbc_inverter_database.loaded) {
        printf("  DBC Inverter path: %s\n", state->can_state.dbc_inverter_database.path);
        printf("  DBC Inverter mensagens: %d\n", state->can_state.dbc_inverter_database.message_count);
        printf("  DBC Inverter sinais: %d\n", state->can_state.dbc_inverter_database.total_signal_count);
    } else if (state->can_state.config.dbc_inverter_path[0] != '\0') {
        printf("  DBC Inverter path: %s\n", state->can_state.config.dbc_inverter_path);
    }
    printf("  Porta aberta: %s\n", InterlockedCompareExchange((LONG *)&state->can_state.opened, 0, 0) ? "sim" : "nao");
    printf("  Barramento ativo: %s\n", InterlockedCompareExchange((LONG *)&state->can_state.running, 0, 0) ? "sim" : "nao");
    if (state->can_state.config.inverter_left_base_set) {
        printf("  Inverter A base: 0x%08X\n", state->can_state.config.inverter_left_base);
    } else {
        printf("  Inverter A base: (nao definida)\n");
    }
    if (state->can_state.config.inverter_right_base_set) {
        printf("  Inverter B base: 0x%08X\n", state->can_state.config.inverter_right_base);
    } else {
        printf("  Inverter B base: (nao definida)\n");
    }
}

/**
 * @brief Exibe a porta atual e a lista de portas COM detectadas.
 *
 * @param state Estado global do terminal serial.
 */
static void print_com_ports(const SerialState *state)
{
    char ports[MAX_COM_PORTS][32];
    int count = list_available_ports(ports, MAX_COM_PORTS);
    int index;

    printf("Porta atual: %s\n", state->config.com_port[0] == '\0' ? "(nao definida)" : state->config.com_port);
    printf("Portas disponiveis:\n");

    if (count == 0) {
        printf("  Nenhuma porta COM encontrada.\n");
        return;
    }

    for (index = 0; index < count; index++) {
        printf("  %s\n", ports[index]);
    }
}

/**
 * @brief Exibe informacoes sobre a aplicacao e o ambiente de compilacao.
 *
 * @param state Estado global do terminal serial.
 */
static void print_about(const SerialState *state)
{
    printf("Terminal BMT\n");
    printf("  Versao: %s\n", BMT_VERSION);
    printf("  Arquivo de configuracao: %s\n", CONFIG_FILE_PATH);
    printf("  Arquivo de log atual: %s\n", state->config.logfile_path);
    printf("  Compilado em: %s %s\n", __DATE__, __TIME__);
#ifdef __GNUC__
    printf("  Compilador: GCC %s\n", __VERSION__);
#else
    printf("  Compilador: desconhecido\n");
#endif
}

/**
 * @brief Atualiza o estado da colorizacao de logs do ESP32.
 *
 * @param state Estado global do terminal serial.
 * @param target Nome do alvo de logs informado no comando.
 * @param value Estado desejado, on ou off.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_log_mode(SerialState *state, const char *target, const char *value)
{
    if (!equals_ignore_case(target, "esp32")) {
        fprintf(stderr, "Alvo de logs invalido. Use 'bmt -logs esp32 on' ou 'bmt -logs esp32 off'.\n");
        return 0;
    }

    if (equals_ignore_case(value, "on")) {
        state->config.esp32_log_colors_enabled = 1;
        printf("Colorizacao de logs ESP32 ativada.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->config.esp32_log_colors_enabled = 0;
        printf("Colorizacao de logs ESP32 desativada.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    fprintf(stderr, "Valor invalido. Use 'on' ou 'off'.\n");
    return 0;
}

/**
 * @brief Define a porta COM ativa usada pela comunicacao serial.
 *
 * @param state Estado global do terminal serial.
 * @param value Nome da porta informado pelo usuario.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_com_port(SerialState *state, const char *value)
{
    char normalized_port[32];

    copy_uppercase(normalized_port, sizeof(normalized_port), value);
    if (strncmp(normalized_port, "COM", 3) != 0 || strlen(normalized_port) < 4) {
        fprintf(stderr, "Porta invalida. Use o formato COMx, ex: COM3.\n");
        return 0;
    }

    snprintf(state->config.com_port, sizeof(state->config.com_port), "%s", normalized_port);
    update_web_ui_serial_snapshot(state);
    printf("Porta COM definida para %s.\n", state->config.com_port);
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza o baud rate da configuracao serial.
 *
 * @param state Estado global do terminal serial.
 * @param value Texto contendo o baud rate desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_baud_rate(SerialState *state, const char *value)
{
    char *end = NULL;
    unsigned long baud_rate = strtoul(value, &end, 10);

    if (end == value || *end != '\0' || baud_rate == 0) {
        fprintf(stderr, "Baud rate invalido.\n");
        return 0;
    }

    state->config.baud_rate = (DWORD)baud_rate;
    update_web_ui_serial_snapshot(state);
    printf("Baud rate definido para %lu.\n", baud_rate);
    auto_save_if_enabled(state);
    return 1;
}
/**
 * @brief Atualiza a quantidade de bits de dados da configuracao serial.
 *
 * @param state Estado global do terminal serial.
 * @param value Texto contendo a quantidade desejada de bits.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_data_bits(SerialState *state, const char *value)
{
    char *end = NULL;
    unsigned long data_bits = strtoul(value, &end, 10);

    if (end == value || *end != '\0' || data_bits < 5 || data_bits > 8) {
        fprintf(stderr, "Data bits invalidos. Use 5, 6, 7 ou 8.\n");
        return 0;
    }

    state->config.data_bits = (BYTE)data_bits;
    update_web_ui_serial_snapshot(state);
    printf("Data bits definidos para %lu.\n", data_bits);
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza o modo de paridade da configuracao serial.
 *
 * @param state Estado global do terminal serial.
 * @param value Texto contendo o modo de paridade desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_parity_mode(SerialState *state, const char *value)
{
    ParityMode parity;

    if (!parse_parity(value, &parity)) {
        fprintf(stderr, "Paridade invalida. Use none, odd, even, mark ou space.\n");
        return 0;
    }

    state->config.parity = parity;
    update_web_ui_serial_snapshot(state);
    printf("Paridade definida para %s.\n", parity_to_string(parity));
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza a configuracao de stop bits da serial.
 *
 * @param state Estado global do terminal serial.
 * @param value Texto contendo o modo de stop bits desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_stop_bits_mode(SerialState *state, const char *value)
{
    StopBitsMode stop_bits;

    if (!parse_stop_bits(value, &stop_bits)) {
        fprintf(stderr, "Stop bits invalidos. Use 1, 1.5 ou 2.\n");
        return 0;
    }

    state->config.stop_bits = stop_bits;
    update_web_ui_serial_snapshot(state);
    printf("Stop bits definidos para %s.\n", stop_bits_to_string(stop_bits));
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza o modo de controle de fluxo da serial.
 *
 * @param state Estado global do terminal serial.
 * @param value Texto contendo o modo de flow control desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_flow_control_mode(SerialState *state, const char *value)
{
    FlowControlMode flow_control;

    if (!parse_flow_control(value, &flow_control)) {
        fprintf(stderr, "Flow control invalido. Use none, xonxoff, rtscts ou dsrdtr.\n");
        return 0;
    }

    state->config.flow_control = flow_control;
    update_web_ui_serial_snapshot(state);
    printf("Flow control definido para %s.\n", flow_control_to_string(flow_control));
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza o estado da exibicao de timestamp nas linhas recebidas.
 *
 * @param state Estado global do terminal serial.
 * @param value Estado desejado, on ou off.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_timestamp_mode(SerialState *state, const char *value)
{
    if (equals_ignore_case(value, "on")) {
        state->config.timestamp_enabled = 1;
        update_web_ui_serial_snapshot(state);
        printf("Timestamp ativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->config.timestamp_enabled = 0;
        update_web_ui_serial_snapshot(state);
        printf("Timestamp desativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    fprintf(stderr, "Valor invalido. Use 'on' ou 'off'.\n");
    return 0;
}

/**
 * @brief Atualiza o filtro de logs do modo ESP32.
 *
 * @param state Estado global do terminal serial.
 * @param value Filtro desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_filter_mode(SerialState *state, const char *value)
{
    LogFilterMode filter_mode;

    if (!parse_filter_mode(value, &filter_mode)) {
        fprintf(stderr, "Filtro invalido. Use info, warning, error, debug, verbose ou all.\n");
        return 0;
    }

    state->config.filter_mode = filter_mode;
    printf("Filtro ESP32 definido para %s.\n", filter_mode_to_string(filter_mode));
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza o modo de monitor da serial.
 *
 * @param state Estado global do terminal serial.
 * @param value Modo desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_monitor_mode(SerialState *state, const char *value)
{
    MonitorMode monitor_mode;

    if (!parse_monitor_mode(value, &monitor_mode)) {
        fprintf(stderr, "Monitor invalido. Use raw ou esp32.\n");
        return 0;
    }

    state->config.monitor_mode = monitor_mode;
    printf("Modo de monitor definido para %s.\n", monitor_mode_to_string(monitor_mode));
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza o estado do log em arquivo.
 *
 * @param state Estado global do terminal serial.
 * @param value Estado desejado, on ou off.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_logfile_enabled(SerialState *state, const char *value)
{
    if (equals_ignore_case(value, "on")) {
        state->config.logfile_enabled = 1;
        update_web_ui_serial_snapshot(state);
        printf("Log em arquivo ativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->config.logfile_enabled = 0;
        update_web_ui_serial_snapshot(state);
        printf("Log em arquivo desativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    fprintf(stderr, "Valor invalido. Use 'on' ou 'off'.\n");
    return 0;
}

/**
 * @brief Atualiza o caminho do arquivo de log.
 *
 * @param state Estado global do terminal serial.
 * @param path Novo caminho desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_logfile_path(SerialState *state, const char *path)
{
    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "Informe um caminho valido para o arquivo de log.\n");
        return 0;
    }

    snprintf(state->config.logfile_path, sizeof(state->config.logfile_path), "%s", path);
    update_web_ui_serial_snapshot(state);
    printf("Arquivo de log definido para %s.\n", state->config.logfile_path);
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza o estado do autosave.
 *
 * @param state Estado global do terminal serial.
 * @param value Estado desejado, on ou off.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_autosave_mode(SerialState *state, const char *value)
{
    if (equals_ignore_case(value, "on")) {
        state->config.autosave_enabled = 1;
        web_ui_set_autosave_enabled(&state->web_ui, 1);
        update_web_ui_serial_snapshot(state);
        save_config(state);
        printf("Autosave ativado.\n");
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->config.autosave_enabled = 0;
        web_ui_set_autosave_enabled(&state->web_ui, 0);
        update_web_ui_serial_snapshot(state);
        printf("Autosave desativado.\n");
        return 1;
    }

    fprintf(stderr, "Valor invalido. Use 'on' ou 'off'.\n");
    return 0;
}

/**
 * @brief Atualiza o estado da reconexao automatica.
 *
 * @param state Estado global do terminal serial.
 * @param value Estado desejado, on ou off.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_reconnect_mode(SerialState *state, const char *value)
{
    if (equals_ignore_case(value, "on")) {
        state->config.reconnect_enabled = 1;
        update_web_ui_serial_snapshot(state);
        printf("Reconnect automatico ativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->config.reconnect_enabled = 0;
        update_web_ui_serial_snapshot(state);
        printf("Reconnect automatico desativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    fprintf(stderr, "Valor invalido. Use 'on' ou 'off'.\n");
    return 0;
}
/**
 * @brief Atualiza o tema visual usado nos logs coloridos.
 *
 * @param state Estado global do terminal serial.
 * @param value Tema desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_theme_mode(SerialState *state, const char *value)
{
    ThemeMode theme_mode;

    if (!parse_theme_mode(value, &theme_mode)) {
        fprintf(stderr, "Tema invalido. Use default, light ou highcontrast.\n");
        return 0;
    }

    state->config.theme_mode = theme_mode;
    printf("Tema definido para %s.\n", theme_mode_to_string(theme_mode));
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza o snapshot serial consumido pela UI web local.
 *
 * @param state Estado global do terminal serial.
 */
static void update_web_ui_serial_snapshot(SerialState *state)
{
    WebUiSerialSnapshot snapshot;

    ZeroMemory(&snapshot, sizeof(snapshot));
    snprintf(snapshot.com_port, sizeof(snapshot.com_port), "%s", state->config.com_port);
    snapshot.baud_rate = (unsigned int)state->config.baud_rate;
    snapshot.data_bits = (unsigned int)state->config.data_bits;
    snprintf(snapshot.parity, sizeof(snapshot.parity), "%s", parity_to_string(state->config.parity));
    snprintf(snapshot.stop_bits, sizeof(snapshot.stop_bits), "%s", stop_bits_to_string(state->config.stop_bits));
    snprintf(snapshot.flow_control, sizeof(snapshot.flow_control), "%s", flow_control_to_string(state->config.flow_control));
    snapshot.timestamp_enabled = state->config.timestamp_enabled;
    snapshot.logfile_enabled = state->config.logfile_enabled;
    snapshot.reconnect_enabled = state->config.reconnect_enabled;
    snprintf(snapshot.logfile_path, sizeof(snapshot.logfile_path), "%s", state->config.logfile_path);
    snapshot.serial_running = InterlockedCompareExchange(&state->running, 0, 0) ? 1 : 0;
    web_ui_set_serial_snapshot(&state->web_ui, &snapshot);
}

/**
 * @brief Define a porta COM do adaptador CAN/SLCAN.
 *
 * @param state Estado global do terminal.
 * @param value Porta COM informada pelo usuario.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_can_com_port(SerialState *state, const char *value)
{
    char normalized_port[32];

    copy_uppercase(normalized_port, sizeof(normalized_port), value);
    if (strncmp(normalized_port, "COM", 3) != 0 || strlen(normalized_port) < 4) {
        fprintf(stderr, "Porta CAN invalida. Use o formato COMx, ex: COM9.\n");
        return 0;
    }

    snprintf(state->can_state.config.com_port, sizeof(state->can_state.config.com_port), "%s", normalized_port);
    printf("Porta CAN definida para %s.\n", state->can_state.config.com_port);
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Define o bitrate CAN usado pelo backend SLCAN.
 *
 * @param state Estado global do terminal.
 * @param value Texto contendo o bitrate desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_can_bitrate(SerialState *state, const char *value)
{
    char *end = NULL;
    unsigned long bitrate = strtoul(value, &end, 10);
    CanConfig validation_config;

    can_set_default_config(&validation_config);
    if (end == value || *end != '\0') {
        fprintf(stderr, "Bitrate CAN invalido.\n");
        return 0;
    }

    validation_config.bitrate = (unsigned int)bitrate;
    if (!can_apply_config_entry(&validation_config, "can_bitrate", value)) {
        fprintf(stderr, "Bitrate CAN nao suportado pelo SLCAN. Use 10000, 20000, 50000, 100000, 125000, 250000, 500000, 800000 ou 1000000.\n");
        return 0;
    }

    state->can_state.config.bitrate = (unsigned int)bitrate;
    printf("Bitrate CAN definido para %u.\n", state->can_state.config.bitrate);
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza o modo de exibicao do monitor CAN.
 *
 * @param state Estado global do terminal.
 * @param value Modo desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_can_view_mode(SerialState *state, const char *value)
{
    CanViewMode view_mode;

    if (!can_parse_view_mode(value, &view_mode)) {
        fprintf(stderr, "View CAN invalida. Use raw ou table.\n");
        return 0;
    }

    state->can_state.config.view_mode = view_mode;
    printf("View CAN definida para %s.\n", can_view_mode_to_string(view_mode));
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Atualiza a exibicao de timestamp no modo CAN.
 *
 * @param state Estado global do terminal.
 * @param value Estado desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_can_timestamp_mode(SerialState *state, const char *value)
{
    if (equals_ignore_case(value, "on")) {
        state->can_state.config.timestamp_enabled = 1;
        printf("Timestamp CAN ativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->can_state.config.timestamp_enabled = 0;
        printf("Timestamp CAN desativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    fprintf(stderr, "Valor invalido. Use 'on' ou 'off'.\n");
    return 0;
}

/**
 * @brief Ativa ou desativa o dashboard de monitor CAN bruto na UI web.
 *
 * @param state Estado global do terminal.
 * @param value Estado desejado, on ou off.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_can_ui_monitor_mode(SerialState *state, const char *value)
{
    if (equals_ignore_case(value, "on")) {
        state->can_state.config.ui_can_monitor_enabled = 1;
        printf("Dashboard CAN Monitor ativado. VCU e Inverters ficam ocultos na UI.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->can_state.config.ui_can_monitor_enabled = 0;
        printf("Dashboard CAN Monitor desativado. VCU e Inverters voltam a ficar disponiveis.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    fprintf(stderr, "Valor invalido. Use 'on' ou 'off'.\n");
    return 0;
}

/**
 * @brief Aplica configuracoes CAN vindas da UI web usando a mesma validacao do terminal.
 *
 * @param context Ponteiro opaco para o estado global do terminal.
 * @param com_port Porta COM informada na pagina de configuracao.
 * @param bitrate Bitrate CAN desejado.
 * @param message Buffer para retorno textual ao frontend.
 * @param message_size Tamanho do buffer de retorno.
 * @return int Retorna 1 quando pelo menos uma configuracao foi aplicada com sucesso.
 */
static int web_apply_can_settings_callback(void *context, const char *com_port, unsigned int bitrate, char *message, size_t message_size)
{
    SerialState *state = (SerialState *)context;
    int changed = 0;
    int success = 1;
    char bitrate_text[16];

    if (com_port != NULL && com_port[0] != '\0') {
        success = set_can_com_port(state, com_port) && success;
        changed = 1;
    }

    if (bitrate > 0U) {
        snprintf(bitrate_text, sizeof(bitrate_text), "%u", bitrate);
        success = set_can_bitrate(state, bitrate_text) && success;
        changed = 1;
    }

    if (!changed) {
        snprintf(message, message_size, "Nenhuma alteracao foi enviada.");
        return 0;
    }

    if (!success) {
        snprintf(message, message_size, "Falha ao aplicar uma ou mais configuracoes CAN.");
        return 0;
    }

    snprintf(message, message_size, "Configuracoes CAN aplicadas com sucesso.");
    return 1;
}

/**
 * @brief Lista as portas COM detectadas para a UI web.
 *
 * @param context Ponteiro opaco para o estado global do terminal.
 * @param ports Vetor de saida para nomes das portas.
 * @param max_ports Quantidade maxima de portas a preencher.
 * @return int Quantidade de portas detectadas.
 */
static int web_list_ports_callback(void *context, char ports[][32], int max_ports)
{
    (void)context;
    return list_available_ports(ports, max_ports);
}

/**
 * @brief Aplica configuracoes seriais vindas da UI web.
 *
 * @param context Ponteiro opaco para o estado global do terminal.
 * @param com_port Porta COM serial desejada.
 * @param baud_rate Baud rate desejado.
 * @param data_bits Quantidade de bits de dados.
 * @param parity Paridade desejada.
 * @param stop_bits Stop bits desejados.
 * @param flow_control Modo de flow control desejado.
 * @param timestamp_enabled Estado do timestamp na serial.
 * @param logfile_enabled Estado do log em arquivo.
 * @param logfile_path Caminho do arquivo de log.
 * @param reconnect_enabled Estado do reconnect automatico.
 * @param message Buffer de retorno textual ao frontend.
 * @param message_size Tamanho do buffer de retorno.
 * @return int Retorna 1 quando a configuracao foi aplicada com sucesso.
 */
static int web_apply_serial_settings_callback(void *context,
                                              const char *com_port,
                                              unsigned int baud_rate,
                                              unsigned int data_bits,
                                              const char *parity,
                                              const char *stop_bits,
                                              const char *flow_control,
                                              int timestamp_enabled,
                                              int logfile_enabled,
                                              const char *logfile_path,
                                              int reconnect_enabled,
                                              char *message,
                                              size_t message_size)
{
    SerialState *state = (SerialState *)context;
    int success = 1;
    int changed = 0;
    char numeric_text[16];

    if (com_port != NULL && com_port[0] != '\0') {
        success = set_com_port(state, com_port) && success;
        changed = 1;
    }

    if (baud_rate > 0U) {
        snprintf(numeric_text, sizeof(numeric_text), "%u", baud_rate);
        success = set_baud_rate(state, numeric_text) && success;
        changed = 1;
    }

    if (data_bits >= 5U && data_bits <= 8U) {
        snprintf(numeric_text, sizeof(numeric_text), "%u", data_bits);
        success = set_data_bits(state, numeric_text) && success;
        changed = 1;
    }

    if (parity != NULL && parity[0] != '\0') {
        success = set_parity_mode(state, parity) && success;
        changed = 1;
    }

    if (stop_bits != NULL && stop_bits[0] != '\0') {
        success = set_stop_bits_mode(state, stop_bits) && success;
        changed = 1;
    }

    if (flow_control != NULL && flow_control[0] != '\0') {
        success = set_flow_control_mode(state, flow_control) && success;
        changed = 1;
    }

    success = set_timestamp_mode(state, timestamp_enabled ? "on" : "off") && success;
    success = set_logfile_enabled(state, logfile_enabled ? "on" : "off") && success;
    success = set_reconnect_mode(state, reconnect_enabled ? "on" : "off") && success;
    changed = 1;

    if (logfile_path != NULL && logfile_path[0] != '\0') {
        success = set_logfile_path(state, logfile_path) && success;
    }

    if (!changed) {
        snprintf(message, message_size, "Nenhuma alteracao serial foi enviada.");
        return 0;
    }

    if (!success) {
        snprintf(message, message_size, "Falha ao aplicar uma ou mais configuracoes seriais.");
        return 0;
    }

    snprintf(message, message_size, "Configuracoes seriais aplicadas com sucesso.");
    return 1;
}

/**
 * @brief Carrega um arquivo DBC a partir da pagina web.
 *
 * @param context Ponteiro opaco para o estado global do terminal.
 * @param path Caminho informado na UI.
 * @param message Buffer para retorno textual ao frontend.
 * @param message_size Tamanho do buffer de retorno.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int web_load_dbc_callback(void *context, const char *path, char *message, size_t message_size)
{
    SerialState *state = (SerialState *)context;

    if (!load_can_dbc(state, "vcu", path)) {
        snprintf(message, message_size, "Nao foi possivel carregar o DBC informado.");
        return 0;
    }

    snprintf(message, message_size, "DBC carregado com sucesso.");
    return 1;
}

/**
 * @brief Executa acoes operacionais disparadas pela pagina de configuracao.
 *
 * @param context Ponteiro opaco para o estado global do terminal.
 * @param action Nome da acao solicitada pelo frontend.
 * @param message Buffer para retorno textual ao frontend.
 * @param message_size Tamanho do buffer de retorno.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int web_execute_action_callback(void *context, const char *action, char *message, size_t message_size)
{
    SerialState *state = (SerialState *)context;

    if (action == NULL || action[0] == '\0') {
        snprintf(message, message_size, "Acao nao informada.");
        return 0;
    }

    if (equals_ignore_case(action, "can_open")) {
        if (!open_can_interface(state)) {
            snprintf(message, message_size, "Falha ao abrir a interface CAN.");
            return 0;
        }
        snprintf(message, message_size, "Interface CAN aberta.");
        return 1;
    }

    if (equals_ignore_case(action, "can_start")) {
        if (!start_can_interface(state)) {
            snprintf(message, message_size, "Falha ao iniciar o barramento CAN.");
            return 0;
        }
        snprintf(message, message_size, "Barramento CAN iniciado.");
        return 1;
    }

    if (equals_ignore_case(action, "can_stop")) {
        stop_can_interface(state);
        snprintf(message, message_size, "Barramento CAN parado.");
        return 1;
    }

    if (equals_ignore_case(action, "can_monitor_on")) {
        if (!set_can_ui_monitor_mode(state, "on")) {
            snprintf(message, message_size, "Falha ao ativar o CAN Monitor.");
            return 0;
        }
        snprintf(message, message_size, "CAN Monitor ativado.");
        return 1;
    }

    if (equals_ignore_case(action, "can_monitor_off")) {
        if (!set_can_ui_monitor_mode(state, "off")) {
            snprintf(message, message_size, "Falha ao desativar o CAN Monitor.");
            return 0;
        }
        snprintf(message, message_size, "CAN Monitor desativado.");
        return 1;
    }

    if (equals_ignore_case(action, "serial_start")) {
        if (!start_serial(state)) {
            snprintf(message, message_size, "Falha ao iniciar a comunicacao serial.");
            return 0;
        }
        update_web_ui_serial_snapshot(state);
        snprintf(message, message_size, "Comunicacao serial iniciada.");
        return 1;
    }

    if (equals_ignore_case(action, "serial_stop")) {
        stop_serial(state);
        update_web_ui_serial_snapshot(state);
        snprintf(message, message_size, "Comunicacao serial parada.");
        return 1;
    }

    if (equals_ignore_case(action, "serial_log_clear")) {
        serial_monitor_clear(&state->serial_monitor);
        snprintf(message, message_size, "Monitor serial web limpo.");
        return 1;
    }

    if (equals_ignore_case(action, "can_log_clear")) {
        serial_monitor_clear(&state->can_monitor);
        snprintf(message, message_size, "Monitor CAN web limpo.");
        return 1;
    }

    if (equals_ignore_case(action, "can_close")) {
        close_can_interface(state);
        snprintf(message, message_size, "Interface CAN fechada.");
        return 1;
    }

    if (equals_ignore_case(action, "dbc_unload")) {
        unload_can_dbc(state, "vcu");
        snprintf(message, message_size, "DBC removido da memoria.");
        return 1;
    }

    if (equals_ignore_case(action, "config_save")) {
        if (!save_config(state)) {
            snprintf(message, message_size, "Falha ao salvar configuracao.");
            return 0;
        }
        snprintf(message, message_size, "Configuracao salva.");
        return 1;
    }

    if (equals_ignore_case(action, "autosave_on")) {
        if (!set_autosave_mode(state, "on")) {
            snprintf(message, message_size, "Falha ao ativar o autosave.");
            return 0;
        }
        snprintf(message, message_size, "Autosave ativado.");
        return 1;
    }

    if (equals_ignore_case(action, "autosave_off")) {
        if (!set_autosave_mode(state, "off")) {
            snprintf(message, message_size, "Falha ao desativar o autosave.");
            return 0;
        }
        snprintf(message, message_size, "Autosave desativado.");
        return 1;
    }

    snprintf(message, message_size, "Acao desconhecida: %s", action);
    return 0;
}

/**
 * @brief Atualiza a colorizacao do monitor CAN.
 *
 * @param state Estado global do terminal.
 * @param value Estado desejado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_can_color_mode(SerialState *state, const char *value)
{
    if (equals_ignore_case(value, "on")) {
        state->can_state.config.colors_enabled = 1;
        printf("Cores do monitor CAN ativadas.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->can_state.config.colors_enabled = 0;
        printf("Cores do monitor CAN desativadas.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    fprintf(stderr, "Valor invalido. Use 'on' ou 'off'.\n");
    return 0;
}

/**
 * @brief Atualiza o filtro por identificador CAN.
 *
 * @param state Estado global do terminal.
 * @param value Identificador informado pelo usuario.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int set_can_filter_id(SerialState *state, const char *value)
{
    char *end = NULL;
    unsigned long can_id = strtoul(value, &end, 0);

    if (end == value || *end != '\0' || can_id > 0x7FFUL) {
        fprintf(stderr, "ID CAN invalido. Use um valor padrao de 11 bits, ex: 123 ou 0x123.\n");
        return 0;
    }

    state->can_state.config.filter_id_enabled = 1;
    state->can_state.config.filter_id = (unsigned int)can_id;
    printf("Filtro CAN definido para 0x%03X.\n", state->can_state.config.filter_id);
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Remove o filtro de identificador CAN.
 *
 * @param state Estado global do terminal.
 */
static void clear_can_filter(SerialState *state)
{
    state->can_state.config.filter_id_enabled = 0;
    state->can_state.config.filter_id = 0;
    printf("Filtro CAN removido.\n");
    auto_save_if_enabled(state);
}

/**
 * @brief Exibe os contadores atuais da sessao CAN.
 *
 * @param state Estado global do terminal.
 */
static void print_can_stats(const SerialState *state)
{
    printf("Estatisticas CAN:\n");
    printf("  RX total: %lu\n", state->can_state.stats.rx_count);
    printf("  TX total: %lu\n", state->can_state.stats.tx_count);
    printf("  Filtrados: %lu\n", state->can_state.stats.filtered_count);
    printf("  Erros de parse: %lu\n", state->can_state.stats.parse_error_count);
    printf("  Perdidos: %lu\n", state->can_state.stats.dropped_count);
    printf("  Ultimo ID: 0x%03X\n", state->can_state.stats.last_id);
}

/**
 * @brief Exibe o resumo do banco DBC atualmente carregado.
 *
 * @param state Estado global do terminal.
 */
static void print_can_dbc_info(const SerialState *state)
{
    printf("DBC VCU:\n");
    if (state->can_state.dbc_vcu_database.loaded) {
        printf("  Estado: carregado\n");
        printf("  Caminho: %s\n", state->can_state.dbc_vcu_database.path);
        printf("  Versao: %s\n",
               state->can_state.dbc_vcu_database.version[0] == '\0' ? "(nao definida)" : state->can_state.dbc_vcu_database.version);
        printf("  Mensagens: %d\n", state->can_state.dbc_vcu_database.message_count);
        printf("  Sinais: %d\n", state->can_state.dbc_vcu_database.total_signal_count);
    } else {
        printf("  Estado: nao carregado\n");
        if (state->can_state.config.dbc_vcu_path[0] != '\0') {
            printf("  Caminho salvo: %s\n", state->can_state.config.dbc_vcu_path);
        }
    }

    printf("DBC Inverter:\n");
    if (state->can_state.dbc_inverter_database.loaded) {
        printf("  Estado: carregado\n");
        printf("  Caminho: %s\n", state->can_state.dbc_inverter_database.path);
        printf("  Versao: %s\n",
               state->can_state.dbc_inverter_database.version[0] == '\0' ? "(nao definida)" : state->can_state.dbc_inverter_database.version);
        printf("  Mensagens: %d\n", state->can_state.dbc_inverter_database.message_count);
        printf("  Sinais: %d\n", state->can_state.dbc_inverter_database.total_signal_count);
    } else {
        printf("  Estado: nao carregado\n");
        if (state->can_state.config.dbc_inverter_path[0] != '\0') {
            printf("  Caminho salvo: %s\n", state->can_state.config.dbc_inverter_path);
        }
    }
}

/**
 * @brief Resolve o banco DBC e o caminho persistente associados a um alvo logico.
 *
 * @param state Estado global do terminal.
 * @param target Nome do alvo desejado: vcu ou inverter.
 * @param database Recebe o ponteiro do banco DBC correspondente.
 * @param config_path Recebe o ponteiro para o caminho persistente correspondente.
 * @return int Retorna 1 quando o alvo foi reconhecido.
 */
static int resolve_can_dbc_target(SerialState *state, const char *target, DbcDatabase **database, char **config_path)
{
    if (equals_ignore_case(target, "vcu")) {
        if (database != NULL) {
            *database = &state->can_state.dbc_vcu_database;
        }
        if (config_path != NULL) {
            *config_path = state->can_state.config.dbc_vcu_path;
        }
        return 1;
    }

    if (equals_ignore_case(target, "inverter")) {
        if (database != NULL) {
            *database = &state->can_state.dbc_inverter_database;
        }
        if (config_path != NULL) {
            *config_path = state->can_state.config.dbc_inverter_path;
        }
        return 1;
    }

    return 0;
}

/**
 * @brief Carrega um arquivo DBC em memoria para uso futuro do decoder CAN.
 *
 * @param state Estado global do terminal.
 * @param target Alvo logico do DBC, como vcu ou inverter.
 * @param path Caminho do arquivo DBC.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int load_can_dbc(SerialState *state, const char *target, const char *path)
{
    DbcDatabase *database = NULL;
    char *config_path = NULL;

    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "Informe o caminho do arquivo DBC.\n");
        return 0;
    }

    if (!resolve_can_dbc_target(state, target, &database, &config_path)) {
        fprintf(stderr, "Alvo DBC invalido. Use 'vcu' ou 'inverter'.\n");
        return 0;
    }

    if (!dbc_load_file(path, database)) {
        fprintf(stderr, "Nao foi possivel carregar o DBC em %s.\n", path);
        return 0;
    }

    snprintf(config_path, CAN_LOG_PATH_SIZE, "%s", path);
    InterlockedExchange(&state->web_ui.dbc_loaded, state->can_state.dbc_vcu_database.loaded ? 1 : 0);
    printf("DBC %s carregado com sucesso: %s\n", target, database->path);
    printf("Mensagens: %d | Sinais: %d\n",
           database->message_count,
           database->total_signal_count);
    if (equals_ignore_case(target, "inverter")) {
        const DbcMessage *commands_template = dbc_find_message_by_name(database, "COMMANDS_RX_MSG_ID_1");
        if (commands_template != NULL && !state->can_state.config.inverter_left_base_set) {
            state->can_state.config.inverter_left_base = commands_template->id;
            state->can_state.config.inverter_left_base_set = 1;
            printf("Base do Inverter A ajustada automaticamente para 0x%08X.\n",
                   state->can_state.config.inverter_left_base);
        }
        web_ui_reset_inverters(&state->web_ui);
    }
    if (equals_ignore_case(target, "vcu")) {
        web_ui_reset_dashboard(&state->web_ui);
        if (state->web_ui.config.enabled && !InterlockedCompareExchange(&state->web_ui.running, 0, 0)) {
            web_ui_start(&state->web_ui, &state->can_state.dbc_vcu_database);
        }
    }
    auto_save_if_enabled(state);
    return 1;
}

/**
 * @brief Remove da memoria o banco DBC atualmente carregado.
 *
 * @param state Estado global do terminal.
 * @param target Alvo logico do DBC, como vcu ou inverter.
 */
static void unload_can_dbc(SerialState *state, const char *target)
{
    DbcDatabase *database = NULL;
    char *config_path = NULL;

    if (!resolve_can_dbc_target(state, target, &database, &config_path)) {
        fprintf(stderr, "Alvo DBC invalido. Use 'vcu' ou 'inverter'.\n");
        return;
    }

    if (InterlockedCompareExchange(&state->web_ui.running, 0, 0)) {
        web_ui_stop(&state->web_ui);
    }

    dbc_reset_database(database);
    config_path[0] = '\0';
    InterlockedExchange(&state->web_ui.dbc_loaded, state->can_state.dbc_vcu_database.loaded ? 1 : 0);
    if (equals_ignore_case(target, "vcu")) {
        web_ui_reset_dashboard(&state->web_ui);
    } else if (equals_ignore_case(target, "inverter")) {
        web_ui_reset_inverters(&state->web_ui);
    }
    auto_save_if_enabled(state);
    printf("DBC %s removido da memoria.\n", target);
}

/**
 * @brief Abre a interface CAN/SLCAN usando a porta COM configurada.
 *
 * @param state Estado global do terminal.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int open_can_interface(SerialState *state)
{
    if (InterlockedCompareExchange(&state->running, 0, 0)) {
        fprintf(stderr, "Pare a comunicacao serial antes de abrir a interface CAN.\n");
        return 0;
    }

    if (!can_slcan_open(&state->can_state)) {
        fprintf(stderr, "Nao foi possivel abrir a interface CAN em %s. Codigo: %lu\n",
                state->can_state.config.com_port[0] == '\0' ? "(nao definida)" : state->can_state.config.com_port,
                GetLastError());
        return 0;
    }

    printf("Interface CAN aberta em %s com bitrate %u.\n",
           state->can_state.config.com_port,
           state->can_state.config.bitrate);
    web_ui_set_can_runtime(&state->web_ui, 1, InterlockedCompareExchange(&state->can_state.running, 0, 0));
    return 1;
}

/**
 * @brief Inicia o barramento CAN no adaptador SLCAN.
 *
 * @param state Estado global do terminal.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int start_can_interface(SerialState *state)
{
    if (InterlockedCompareExchange(&state->running, 0, 0)) {
        fprintf(stderr, "Pare a comunicacao serial antes de iniciar o monitor CAN.\n");
        return 0;
    }

    if (InterlockedCompareExchange(&state->can_state.running, 0, 0)) {
        fprintf(stderr, "O monitor CAN ja esta ativo.\n");
        return 0;
    }

    if (!InterlockedCompareExchange(&state->can_state.opened, 0, 0) && !open_can_interface(state)) {
        return 0;
    }

    InterlockedExchange(&state->paused, 0);
    state->paused_length = 0;
    state->paused_output[0] = '\0';
    state->paused_dropped_messages = 0;
    state->can_state.partial_length = 0;
    state->can_state.partial_line[0] = '\0';
    state->can_state.table_header_printed = 0;

    if (!can_slcan_start(&state->can_state)) {
        fprintf(stderr, "Nao foi possivel iniciar o barramento CAN. Codigo: %lu\n", GetLastError());
        return 0;
    }

    state->can_state.reader_thread = CreateThread(NULL, 0, can_reader_thread, state, 0, &state->can_state.reader_thread_id);
    if (state->can_state.reader_thread == NULL) {
        fprintf(stderr, "Nao foi possivel iniciar a thread de leitura CAN. Codigo: %lu\n", GetLastError());
        can_slcan_stop(&state->can_state);
        return 0;
    }

    state->can_state.hotkey_thread = CreateThread(NULL, 0, can_hotkey_thread, state, 0, &state->can_state.hotkey_thread_id);
    if (state->can_state.hotkey_thread == NULL) {
        fprintf(stderr, "Nao foi possivel iniciar a thread de atalhos CAN. Codigo: %lu\n", GetLastError());
        can_slcan_stop(&state->can_state);
        WaitForSingleObject(state->can_state.reader_thread, INFINITE);
        CloseHandle(state->can_state.reader_thread);
        state->can_state.reader_thread = NULL;
        state->can_state.reader_thread_id = 0;
        return 0;
    }

    printf("Barramento CAN iniciado em modo SLCAN.\n");
    printf("Use 'p' para pausar, 'r' para retomar e Ctrl+Shift+T para voltar ao prompt.\n\n");
    web_ui_set_can_runtime(&state->web_ui, 1, 1);
    return 1;
}

/**
 * @brief Para o barramento CAN sem fechar a porta COM.
 *
 * @param state Estado global do terminal.
 */
static void stop_can_interface(SerialState *state)
{
    DWORD current_thread_id = GetCurrentThreadId();

    if (!InterlockedCompareExchange(&state->can_state.running, 0, 0)) {
        printf("O barramento CAN ja esta parado.\n");
        return;
    }

    can_slcan_stop(&state->can_state);
    InterlockedExchange(&state->paused, 0);

    if (state->can_state.reader_thread != NULL && state->can_state.reader_thread_id != current_thread_id) {
        WaitForSingleObject(state->can_state.reader_thread, INFINITE);
        CloseHandle(state->can_state.reader_thread);
        state->can_state.reader_thread = NULL;
        state->can_state.reader_thread_id = 0;
    }

    if (state->can_state.hotkey_thread != NULL && state->can_state.hotkey_thread_id != current_thread_id) {
        WaitForSingleObject(state->can_state.hotkey_thread, INFINITE);
        CloseHandle(state->can_state.hotkey_thread);
        state->can_state.hotkey_thread = NULL;
        state->can_state.hotkey_thread_id = 0;
    }

    if (state->can_state.reader_thread_id == current_thread_id) {
        state->can_state.reader_thread = NULL;
        state->can_state.reader_thread_id = 0;
    }

    if (state->can_state.hotkey_thread_id == current_thread_id) {
        CloseHandle(state->can_state.hotkey_thread);
        state->can_state.hotkey_thread = NULL;
        state->can_state.hotkey_thread_id = 0;
    }

    printf("Barramento CAN parado.\n");
    web_ui_set_can_runtime(&state->web_ui, InterlockedCompareExchange(&state->can_state.opened, 0, 0), 0);
}

/**
 * @brief Fecha a interface CAN e libera os recursos associados.
 *
 * @param state Estado global do terminal.
 */
static void close_can_interface(SerialState *state)
{
    if (!InterlockedCompareExchange(&state->can_state.opened, 0, 0)) {
        printf("A interface CAN ja esta fechada.\n");
        return;
    }

    if (InterlockedCompareExchange(&state->can_state.running, 0, 0)) {
        stop_can_interface(state);
    }

    can_slcan_close(&state->can_state);
    printf("Interface CAN fechada.\n");
    web_ui_set_can_runtime(&state->web_ui, 0, 0);
}

/**
 * @brief Interpreta os bytes do comando de envio CAN e transmite um frame padrao.
 *
 * @param state Estado global do terminal.
 * @param rest Texto restante da linha de comando.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int send_can_frame(SerialState *state, char *rest)
{
    char *id_token = next_token(&rest);
    unsigned char data[8];
    unsigned char dlc = 0;
    char *end = NULL;
    unsigned long can_id;

    if (id_token == NULL) {
        fprintf(stderr, "Informe o ID CAN e os bytes a serem enviados.\n");
        return 0;
    }

    can_id = strtoul(id_token, &end, 0);
    if (end == id_token || *end != '\0' || can_id > 0x7FFUL) {
        fprintf(stderr, "ID CAN invalido. Use um identificador padrao de 11 bits.\n");
        return 0;
    }

    while (dlc < 8) {
        char *byte_token = next_token(&rest);
        unsigned long byte_value;

        if (byte_token == NULL) {
            break;
        }

        byte_value = strtoul(byte_token, &end, 16);
        if (end == byte_token || *end != '\0' || byte_value > 0xFFUL) {
            fprintf(stderr, "Byte CAN invalido: %s\n", byte_token);
            return 0;
        }

        data[dlc++] = (unsigned char)byte_value;
    }

    if (!can_slcan_send_standard(&state->can_state, (unsigned int)can_id, data, dlc)) {
        fprintf(stderr, "Nao foi possivel enviar o frame CAN. Codigo: %lu\n", GetLastError());
        return 0;
    }

    printf("Frame CAN 0x%03X enviado com %u byte(s).\n", (unsigned int)can_id, dlc);
    return 1;
}

/**
 * @brief Interpreta e executa os subcomandos do namespace bmt -can.
 *
 * @param state Estado global do terminal.
 * @param rest Texto restante da linha de comando.
 * @return int Retorna 1 quando o comando foi reconhecido, caso contrario 0.
 */
static int process_can_command(SerialState *state, char *rest)
{
    char *subcommand = next_token(&rest);
    char *arg1;
    char *arg2;

    if (subcommand == NULL) {
        print_can_status(state);
        return 1;
    }

    if (equals_ignore_case(subcommand, "help")) {
        print_can_help();
        return 1;
    }

    if (equals_ignore_case(subcommand, "list")) {
        char ports[MAX_COM_PORTS][32];
        int count = list_available_ports(ports, MAX_COM_PORTS);
        int index;

        printf("Portas COM candidatas para CAN/SLCAN:\n");
        if (count == 0) {
            printf("  Nenhuma porta COM encontrada.\n");
            return 1;
        }

        for (index = 0; index < count; index++) {
            printf("  %s\n", ports[index]);
        }
        return 1;
    }

    if (equals_ignore_case(subcommand, "com")) {
        rest = trim_whitespace(rest);
        if (rest[0] == '\0') {
            printf("Porta CAN atual: %s\n",
                   state->can_state.config.com_port[0] == '\0' ? "(nao definida)" : state->can_state.config.com_port);
        } else {
            set_can_com_port(state, rest);
        }
        return 1;
    }

    if (equals_ignore_case(subcommand, "bitrate")) {
        rest = trim_whitespace(rest);
        if (rest[0] == '\0') {
            printf("Bitrate CAN atual: %u\n", state->can_state.config.bitrate);
        } else {
            set_can_bitrate(state, rest);
        }
        return 1;
    }

    if (equals_ignore_case(subcommand, "view")) {
        rest = trim_whitespace(rest);
        if (rest[0] == '\0') {
            printf("View CAN atual: %s\n", can_view_mode_to_string(state->can_state.config.view_mode));
        } else {
            set_can_view_mode(state, rest);
        }
        return 1;
    }

    if (equals_ignore_case(subcommand, "timestamp")) {
        rest = trim_whitespace(rest);
        if (rest[0] == '\0') {
            printf("Timestamp CAN: %s\n", bool_to_on_off(state->can_state.config.timestamp_enabled));
        } else {
            set_can_timestamp_mode(state, rest);
        }
        return 1;
    }

    if (equals_ignore_case(subcommand, "color")) {
        rest = trim_whitespace(rest);
        if (rest[0] == '\0') {
            printf("Cores CAN: %s\n", bool_to_on_off(state->can_state.config.colors_enabled));
        } else {
            set_can_color_mode(state, rest);
        }
        return 1;
    }

    if (equals_ignore_case(subcommand, "serialmonitor")) {
        rest = trim_whitespace(rest);
        if (rest[0] == '\0') {
            printf("Dashboard CAN Monitor: %s\n", bool_to_on_off(state->can_state.config.ui_can_monitor_enabled));
        } else {
            set_can_ui_monitor_mode(state, rest);
        }
        return 1;
    }

    if (equals_ignore_case(subcommand, "filter")) {
        arg1 = next_token(&rest);
        arg2 = next_token(&rest);

        if (arg1 == NULL) {
            if (state->can_state.config.filter_id_enabled) {
                printf("Filtro CAN atual: 0x%03X\n", state->can_state.config.filter_id);
            } else {
                printf("Filtro CAN atual: (desativado)\n");
            }
            return 1;
        }

        if (equals_ignore_case(arg1, "clear")) {
            clear_can_filter(state);
            return 1;
        }

        if (equals_ignore_case(arg1, "id") && arg2 != NULL) {
            set_can_filter_id(state, arg2);
            return 1;
        }

        fprintf(stderr, "Uso invalido. Use 'bmt -can filter id <hex>' ou 'bmt -can filter clear'.\n");
        return 1;
    }

    if (equals_ignore_case(subcommand, "dbc")) {
        arg1 = next_token(&rest);
        if (arg1 == NULL || equals_ignore_case(arg1, "info")) {
            print_can_dbc_info(state);
            return 1;
        }

        if (equals_ignore_case(arg1, "load")) {
            arg2 = next_token(&rest);
            rest = trim_whitespace(rest);
            if (arg2 == NULL || rest[0] == '\0') {
                fprintf(stderr, "Uso invalido. Use 'bmt -can dbc load vcu <arquivo>' ou 'bmt -can dbc load inverter <arquivo>'.\n");
                return 1;
            }
            load_can_dbc(state, arg2, rest);
            return 1;
        }

        if (equals_ignore_case(arg1, "unload")) {
            arg2 = next_token(&rest);
            if (arg2 == NULL) {
                fprintf(stderr, "Uso invalido. Use 'bmt -can dbc unload vcu' ou 'bmt -can dbc unload inverter'.\n");
                return 1;
            }
            unload_can_dbc(state, arg2);
            return 1;
        }

        fprintf(stderr, "Uso invalido. Use 'bmt -can dbc info', 'bmt -can dbc load vcu|inverter <arquivo>' ou 'bmt -can dbc unload vcu|inverter'.\n");
        return 1;
    }

    if (equals_ignore_case(subcommand, "inverter")) {
        arg1 = next_token(&rest);
        if (arg1 == NULL || equals_ignore_case(arg1, "status")) {
            printf("Inverter A base: %s\n",
                   state->can_state.config.inverter_left_base_set ? "definida" : "nao definida");
            if (state->can_state.config.inverter_left_base_set) {
                printf("  ID base A: 0x%08X\n", state->can_state.config.inverter_left_base);
            }
            printf("Inverter B base: %s\n",
                   state->can_state.config.inverter_right_base_set ? "definida" : "nao definida");
            if (state->can_state.config.inverter_right_base_set) {
                printf("  ID base B: 0x%08X\n", state->can_state.config.inverter_right_base);
            }
            return 1;
        }

        if (equals_ignore_case(arg1, "base")) {
            char *side = next_token(&rest);
            char *value_text = next_token(&rest);
            char *end = NULL;
            unsigned long base_value;

            if (side == NULL || value_text == NULL) {
                fprintf(stderr, "Uso invalido. Use 'bmt -can inverter base left <id>' ou 'bmt -can inverter base right <id>'.\n");
                return 1;
            }

            base_value = strtoul(value_text, &end, 0);
            if (end == value_text || *end != '\0') {
                fprintf(stderr, "Base do inversor invalida.\n");
                return 1;
            }

            if (equals_ignore_case(side, "left") || equals_ignore_case(side, "a")) {
                state->can_state.config.inverter_left_base = (unsigned int)base_value;
                state->can_state.config.inverter_left_base_set = 1;
                printf("Base do Inverter A ajustada para 0x%08X.\n", state->can_state.config.inverter_left_base);
                auto_save_if_enabled(state);
                return 1;
            }

            if (equals_ignore_case(side, "right") || equals_ignore_case(side, "b")) {
                state->can_state.config.inverter_right_base = (unsigned int)base_value;
                state->can_state.config.inverter_right_base_set = 1;
                printf("Base do Inverter B ajustada para 0x%08X.\n", state->can_state.config.inverter_right_base);
                auto_save_if_enabled(state);
                return 1;
            }

            fprintf(stderr, "Lado invalido. Use 'left'/'a' ou 'right'/'b'.\n");
            return 1;
        }

        fprintf(stderr, "Uso invalido. Use 'bmt -can inverter status' ou 'bmt -can inverter base <left|right> <id>'.\n");
        return 1;
    }

    if (equals_ignore_case(subcommand, "open")) {
        open_can_interface(state);
        return 1;
    }

    if (equals_ignore_case(subcommand, "start")) {
        start_can_interface(state);
        return 1;
    }

    if (equals_ignore_case(subcommand, "stop")) {
        stop_can_interface(state);
        return 1;
    }

    if (equals_ignore_case(subcommand, "close")) {
        close_can_interface(state);
        return 1;
    }

    if (equals_ignore_case(subcommand, "send")) {
        send_can_frame(state, rest);
        return 1;
    }

    if (equals_ignore_case(subcommand, "stats")) {
        print_can_stats(state);
        return 1;
    }

    if (equals_ignore_case(subcommand, "save")) {
        if (save_config(state)) {
            printf("Configuracao CAN salva em %s.\n", CONFIG_FILE_PATH);
        } else {
            fprintf(stderr, "Nao foi possivel salvar a configuracao CAN.\n");
        }
        return 1;
    }

    if (equals_ignore_case(subcommand, "reload")) {
        reload_config(state);
        return 1;
    }

    fprintf(stderr, "Subcomando CAN desconhecido: %s\n", subcommand);
    return 1;
}

/**
 * @brief Interpreta e executa os comandos do subsistema de UI web local.
 *
 * @param state Estado global do terminal.
 * @param rest Texto restante da linha de comando.
 * @return int Retorna 1 quando o comando foi reconhecido.
 */
static int process_ui_command(SerialState *state, char *rest)
{
    char *subcommand = next_token(&rest);
    char *arg1;
    unsigned long port;
    char *end = NULL;

    if (subcommand == NULL || equals_ignore_case(subcommand, "status")) {
        web_ui_print_status(&state->web_ui, &state->can_state.dbc_vcu_database);
        return 1;
    }

    if (equals_ignore_case(subcommand, "on")) {
        if (!state->can_state.dbc_vcu_database.loaded && !state->can_state.config.ui_can_monitor_enabled) {
            fprintf(stderr, "Carregue o DBC da VCU com 'bmt -can dbc load vcu <arquivo.dbc>' ou ative 'bmt -can serialmonitor on'.\n");
            return 1;
        }

        state->web_ui.config.enabled = 1;
        if (!web_ui_start(&state->web_ui, &state->can_state.dbc_vcu_database)) {
            fprintf(stderr, "Nao foi possivel iniciar a UI web na porta %u.\n", (unsigned int)state->web_ui.config.port);
            return 1;
        }

        printf("UI web iniciada em http://127.0.0.1:%u/\n", (unsigned int)state->web_ui.config.port);
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(subcommand, "off")) {
        state->web_ui.config.enabled = 0;
        web_ui_stop(&state->web_ui);
        printf("UI web encerrada.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(subcommand, "open")) {
        if (!InterlockedCompareExchange(&state->web_ui.running, 0, 0)) {
            if (!state->can_state.dbc_vcu_database.loaded && !state->can_state.config.ui_can_monitor_enabled) {
                fprintf(stderr, "Carregue o DBC da VCU com 'bmt -can dbc load vcu <arquivo.dbc>' ou ative 'bmt -can serialmonitor on'.\n");
                return 1;
            }

            state->web_ui.config.enabled = 1;
            if (!web_ui_start(&state->web_ui, &state->can_state.dbc_vcu_database)) {
                fprintf(stderr, "Nao foi possivel iniciar a UI web na porta %u.\n", (unsigned int)state->web_ui.config.port);
                return 1;
            }
        }

        if (!web_ui_open_browser(&state->web_ui)) {
            fprintf(stderr, "Nao foi possivel abrir o navegador padrao.\n");
            return 1;
        }

        printf("Abrindo dashboard em http://127.0.0.1:%u/\n", (unsigned int)state->web_ui.config.port);
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(subcommand, "port")) {
        arg1 = next_token(&rest);
        if (arg1 == NULL) {
            printf("Porta da UI web: %u\n", (unsigned int)state->web_ui.config.port);
            return 1;
        }

        port = strtoul(arg1, &end, 10);
        if (end == arg1 || *end != '\0' || port == 0UL || port > 65535UL) {
            fprintf(stderr, "Porta invalida. Use um valor entre 1 e 65535.\n");
            return 1;
        }

        if (InterlockedCompareExchange(&state->web_ui.running, 0, 0)) {
            web_ui_stop(&state->web_ui);
        }

        state->web_ui.config.port = (unsigned short)port;
        printf("Porta da UI web ajustada para %u.\n", (unsigned int)state->web_ui.config.port);
        auto_save_if_enabled(state);
        return 1;
    }

    fprintf(stderr, "Uso invalido. Use 'bmt -ui status', 'bmt -ui on', 'bmt -ui off', 'bmt -ui open' ou 'bmt -ui port <valor>'.\n");
    return 1;
}

/**
 * @brief Restaura a configuracao padrao do terminal.
 *
 * @param state Estado global do terminal serial.
 */
static void reset_config(SerialState *state)
{
    if (InterlockedCompareExchange(&state->web_ui.running, 0, 0)) {
        web_ui_stop(&state->web_ui);
    }

    set_default_config(&state->config);
    can_set_default_config(&state->can_state.config);
    web_ui_set_default_config(&state->web_ui.config);
    web_ui_reset_dashboard(&state->web_ui);
    web_ui_set_autosave_enabled(&state->web_ui, state->config.autosave_enabled);
    update_web_ui_serial_snapshot(state);
    printf("Configuracao restaurada para os valores padrao.\n");
    auto_save_if_enabled(state);
}

/**
 * @brief Recarrega a configuracao a partir do arquivo persistente.
 *
 * @param state Estado global do terminal serial.
 */
static void reload_config(SerialState *state)
{
    SerialState config_state;

    ZeroMemory(&config_state, sizeof(config_state));
    set_default_config(&config_state.config);
    can_set_default_config(&config_state.can_state.config);
    web_ui_set_default_config(&config_state.web_ui.config);
    if (!load_config(&config_state)) {
        fprintf(stderr, "Nao foi possivel carregar %s.\n", CONFIG_FILE_PATH);
        return;
    }

    state->config = config_state.config;
    state->can_state.config = config_state.can_state.config;
    state->web_ui.config = config_state.web_ui.config;
    web_ui_reset_dashboard(&state->web_ui);
    web_ui_set_autosave_enabled(&state->web_ui, state->config.autosave_enabled);
    update_web_ui_serial_snapshot(state);
    dbc_reset_database(&state->can_state.dbc_vcu_database);
    dbc_reset_database(&state->can_state.dbc_inverter_database);
    if (state->can_state.config.dbc_vcu_path[0] != '\0') {
        load_can_dbc(state, "vcu", state->can_state.config.dbc_vcu_path);
    }
    if (state->can_state.config.dbc_inverter_path[0] != '\0') {
        load_can_dbc(state, "inverter", state->can_state.config.dbc_inverter_path);
    }
    if (InterlockedCompareExchange(&state->web_ui.running, 0, 0)) {
        web_ui_stop(&state->web_ui);
    }
    if (state->web_ui.config.enabled &&
        (state->can_state.dbc_vcu_database.loaded || state->can_state.config.ui_can_monitor_enabled)) {
        web_ui_start(&state->web_ui, &state->can_state.dbc_vcu_database);
    }
    update_web_ui_serial_snapshot(state);
    printf("Configuracao recarregada de %s.\n", CONFIG_FILE_PATH);
}

/**
 * @brief Abre a porta configurada e inicia as threads auxiliares da comunicacao serial.
 *
 * @param state Estado global do terminal serial.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int start_serial(SerialState *state)
{
    if (InterlockedCompareExchange(&state->can_state.running, 0, 0)) {
        fprintf(stderr, "Pare o barramento CAN antes de iniciar a comunicacao serial.\n");
        return 0;
    }

    if (state->config.com_port[0] == '\0') {
        fprintf(stderr, "Nenhuma porta COM definida. Use 'bmt -com COMx' primeiro.\n");
        return 0;
    }

    if (InterlockedCompareExchange(&state->running, 0, 0)) {
        fprintf(stderr, "A comunicacao serial ja esta ativa.\n");
        return 0;
    }

    InterlockedExchange(&state->stop_requested, 0);
    InterlockedExchange(&state->paused, 0);
    state->partial_length = 0;
    state->paused_length = 0;
    state->paused_output[0] = '\0';
    state->paused_dropped_messages = 0;
    state->partial_line[0] = '\0';

    if (!open_serial_handle(state)) {
        fprintf(stderr, "Nao foi possivel abrir ou configurar %s. Codigo: %lu\n",
                state->config.com_port,
                GetLastError());
        return 0;
    }

    InterlockedExchange(&state->running, 1);
    state->reader_thread = CreateThread(NULL, 0, serial_reader_thread, state, 0, &state->reader_thread_id);
    if (state->reader_thread == NULL) {
        fprintf(stderr, "Nao foi possivel iniciar a thread de leitura. Codigo: %lu\n", GetLastError());
        close_serial_handle(state);
        InterlockedExchange(&state->running, 0);
        return 0;
    }

    state->hotkey_thread = CreateThread(NULL, 0, serial_hotkey_thread, state, 0, &state->hotkey_thread_id);
    if (state->hotkey_thread == NULL) {
        fprintf(stderr, "Nao foi possivel iniciar a thread do atalho. Codigo: %lu\n", GetLastError());
        request_serial_stop(state);
        WaitForSingleObject(state->reader_thread, INFINITE);
        CloseHandle(state->reader_thread);
        state->reader_thread = NULL;
        state->reader_thread_id = 0;
        close_serial_handle(state);
        return 0;
    }

    printf("Comunicacao iniciada em %s com baud %lu.\n",
           state->config.com_port,
           (unsigned long)state->config.baud_rate);
    printf("Use 'p' para pausar, 'r' para retomar e Ctrl+Shift+T para voltar ao prompt.\n\n");
    update_web_ui_serial_snapshot(state);
    return 1;
}

/**
 * @brief Encerra a leitura serial e fecha os recursos associados a porta.
 *
 * @param state Estado global do terminal serial.
 */
static void stop_serial(SerialState *state)
{
    DWORD current_thread_id = GetCurrentThreadId();

    if (!InterlockedCompareExchange(&state->running, 0, 0) &&
        state->serial_handle == NULL &&
        state->reader_thread == NULL &&
        state->hotkey_thread == NULL) {
        printf("A comunicacao serial ja esta parada.\n");
        return;
    }

    request_serial_stop(state);
    InterlockedExchange(&state->paused, 0);

    if (state->reader_thread != NULL && state->reader_thread_id != current_thread_id) {
        WaitForSingleObject(state->reader_thread, INFINITE);
        CloseHandle(state->reader_thread);
        state->reader_thread = NULL;
        state->reader_thread_id = 0;
    }

    if (state->hotkey_thread != NULL && state->hotkey_thread_id != current_thread_id) {
        WaitForSingleObject(state->hotkey_thread, INFINITE);
        CloseHandle(state->hotkey_thread);
        state->hotkey_thread = NULL;
        state->hotkey_thread_id = 0;
    }

    if (state->reader_thread_id == current_thread_id) {
        state->reader_thread = NULL;
        state->reader_thread_id = 0;
    }

    if (state->hotkey_thread_id == current_thread_id) {
        CloseHandle(state->hotkey_thread);
        state->hotkey_thread = NULL;
        state->hotkey_thread_id = 0;
    }

    close_serial_handle(state);
    update_web_ui_serial_snapshot(state);
    printf("Comunicacao serial encerrada.\n");
}

/**
 * @brief Inicializa o estado interno do terminal.
 *
 * @param state Estado global do terminal serial.
 */
static void initialize_serial_state(SerialState *state)
{
    WebUiCallbacks web_callbacks;

    ZeroMemory(state, sizeof(*state));
    set_default_config(&state->config);
    can_set_default_config(&state->can_state.config);
    web_ui_initialize(&state->web_ui);
    serial_monitor_initialize(&state->serial_monitor);
    serial_monitor_initialize(&state->can_monitor);
    web_ui_set_autosave_enabled(&state->web_ui, state->config.autosave_enabled);
    can_reset_stats(&state->can_state.stats);
    state->serial_handle = NULL;
    state->reader_thread = NULL;
    state->hotkey_thread = NULL;
    state->reader_thread_id = 0;
    state->hotkey_thread_id = 0;
    state->partial_length = 0;
    state->paused_length = 0;
    state->paused_output[0] = '\0';
    state->paused_dropped_messages = 0;
    state->history_count = 0;
    state->can_state.handle = NULL;
    state->can_state.reader_thread = NULL;
    state->can_state.hotkey_thread = NULL;
    state->can_state.reader_thread_id = 0;
    state->can_state.hotkey_thread_id = 0;
    state->can_state.stop_requested = 0;
    state->can_state.partial_length = 0;
    state->can_state.partial_line[0] = '\0';
    state->can_state.table_header_printed = 0;
    dbc_reset_database(&state->can_state.dbc_vcu_database);
    dbc_reset_database(&state->can_state.dbc_inverter_database);
    web_ui_set_can_runtime(&state->web_ui, 0, 0);
    InterlockedExchange(&state->web_ui.dbc_loaded, 0);
    ZeroMemory(&web_callbacks, sizeof(web_callbacks));
    web_callbacks.context = state;
    web_callbacks.list_ports = web_list_ports_callback;
    web_callbacks.apply_serial_settings = web_apply_serial_settings_callback;
    web_callbacks.apply_can_settings = web_apply_can_settings_callback;
    web_callbacks.load_dbc = web_load_dbc_callback;
    web_callbacks.execute_action = web_execute_action_callback;
    web_ui_bind_runtime(&state->web_ui,
                        &state->can_state.config,
                        &state->can_state.dbc_vcu_database,
                        &state->can_state.dbc_inverter_database,
                        &state->serial_monitor,
                        &state->can_monitor,
                        &web_callbacks);
    update_web_ui_serial_snapshot(state);
    InitializeCriticalSection(&state->console_lock);
}

/**
 * @brief Exibe o estado do logfile atual.
 *
 * @param state Estado global do terminal serial.
 */
static void print_logfile_status(const SerialState *state)
{
    printf("Log em arquivo: %s\n", bool_to_on_off(state->config.logfile_enabled));
    printf("Arquivo atual: %s\n", state->config.logfile_path);
}
/**
 * @brief Interpreta e executa um comando digitado no terminal.
 *
 * @param state Estado global do terminal serial.
 * @param input Linha de comando lida do terminal.
 * @param should_exit Flag usada para sinalizar o encerramento do programa.
 */
static void process_command(SerialState *state, char *input, int *should_exit)
{
    char original_input[INPUT_BUFFER_SIZE];
    char *cursor;
    char *prefix;
    char *command;
    char *arg1;
    char *arg2;
    char *rest;

    trim_newline(input);
    if (input[0] == '\0') {
        return;
    }

    snprintf(original_input, sizeof(original_input), "%s", input);
    add_history_entry(state, original_input);

    cursor = input;
    prefix = next_token(&cursor);
    command = next_token(&cursor);
    rest = trim_whitespace(cursor);

    if (prefix == NULL || !equals_ignore_case(prefix, "bmt")) {
        fprintf(stderr, "Comando invalido. Todos os comandos devem comecar com 'bmt'.\n");
        return;
    }

    if (command == NULL) {
        fprintf(stderr, "Faltou a opcao. Use 'bmt -list'.\n");
        return;
    }

    printf("\n");

    if (equals_ignore_case(command, "-list")) {
        print_help();
        return;
    }

    if (equals_ignore_case(command, "-help")) {
        if (rest[0] == '\0') {
            print_help();
        } else {
            print_command_help(rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-com")) {
        if (rest[0] == '\0' || equals_ignore_case(rest, "refresh")) {
            print_com_ports(state);
        } else {
            set_com_port(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-baud")) {
        if (rest[0] == '\0') {
            printf("Baud rate atual: %lu\n", (unsigned long)state->config.baud_rate);
        } else {
            set_baud_rate(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-parity")) {
        if (rest[0] == '\0') {
            printf("Paridade atual: %s\n", parity_to_string(state->config.parity));
        } else {
            set_parity_mode(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-databits")) {
        if (rest[0] == '\0') {
            printf("Data bits atuais: %u\n", (unsigned int)state->config.data_bits);
        } else {
            set_data_bits(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-stopbits")) {
        if (rest[0] == '\0') {
            printf("Stop bits atuais: %s\n", stop_bits_to_string(state->config.stop_bits));
        } else {
            set_stop_bits_mode(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-flow")) {
        if (rest[0] == '\0') {
            printf("Flow control atual: %s\n", flow_control_to_string(state->config.flow_control));
        } else {
            set_flow_control_mode(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-send")) {
        if (rest[0] == '\0') {
            fprintf(stderr, "Informe o texto a ser enviado.\n");
        } else if (send_serial_bytes(state, (const unsigned char *)rest, strlen(rest))) {
            printf("Texto enviado com sucesso.\n");
        }
        return;
    }

    if (equals_ignore_case(command, "-sendhex")) {
        if (rest[0] == '\0') {
            fprintf(stderr, "Informe os bytes hexadecimais a serem enviados.\n");
        } else {
            send_serial_hex(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-logs")) {
        arg1 = next_token(&rest);
        arg2 = next_token(&rest);
        if (arg1 == NULL || arg2 == NULL) {
            printf("Logs ESP32 coloridos: %s\n", bool_to_on_off(state->config.esp32_log_colors_enabled));
        } else {
            set_log_mode(state, arg1, arg2);
        }
        return;
    }

    if (equals_ignore_case(command, "-timestamp")) {
        if (rest[0] == '\0') {
            printf("Timestamp: %s\n", bool_to_on_off(state->config.timestamp_enabled));
        } else {
            set_timestamp_mode(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-filter")) {
        if (rest[0] == '\0') {
            printf("Filtro atual: %s\n", filter_mode_to_string(state->config.filter_mode));
        } else {
            set_filter_mode(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-monitor")) {
        if (rest[0] == '\0') {
            printf("Monitor atual: %s\n", monitor_mode_to_string(state->config.monitor_mode));
        } else {
            set_monitor_mode(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-logfile")) {
        arg1 = next_token(&rest);
        if (arg1 == NULL) {
            print_logfile_status(state);
            return;
        }

        if (equals_ignore_case(arg1, "path")) {
            rest = trim_whitespace(rest);
            set_logfile_path(state, rest);
            return;
        }

        set_logfile_enabled(state, arg1);
        return;
    }

    if (equals_ignore_case(command, "-autosave")) {
        if (rest[0] == '\0') {
            printf("Autosave: %s\n", bool_to_on_off(state->config.autosave_enabled));
        } else {
            set_autosave_mode(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-reconnect")) {
        if (rest[0] == '\0') {
            printf("Reconnect: %s\n", bool_to_on_off(state->config.reconnect_enabled));
        } else {
            set_reconnect_mode(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-theme")) {
        if (rest[0] == '\0') {
            printf("Tema atual: %s\n", theme_mode_to_string(state->config.theme_mode));
            printf("Temas disponiveis: default, light, highcontrast\n");
        } else {
            set_theme_mode(state, rest);
        }
        return;
    }

    if (equals_ignore_case(command, "-can")) {
        process_can_command(state, rest);
        return;
    }

    if (equals_ignore_case(command, "-ui")) {
        process_ui_command(state, rest);
        return;
    }

    if (equals_ignore_case(command, "-save")) {
        if (save_config(state)) {
            printf("Configuracao salva em %s.\n", CONFIG_FILE_PATH);
        } else {
            fprintf(stderr, "Nao foi possivel salvar a configuracao em %s.\n", CONFIG_FILE_PATH);
        }
        return;
    }

    if (equals_ignore_case(command, "-reload")) {
        reload_config(state);
        return;
    }

    if (equals_ignore_case(command, "-reset")) {
        reset_config(state);
        return;
    }

    if (equals_ignore_case(command, "-clear")) {
        clear_console();
        return;
    }

    if (equals_ignore_case(command, "-status")) {
        print_status(state);
        return;
    }

    if (equals_ignore_case(command, "-history")) {
        print_history(state);
        return;
    }

    if (equals_ignore_case(command, "-about")) {
        print_about(state);
        return;
    }

    if (equals_ignore_case(command, "-start")) {
        start_serial(state);
        return;
    }

    if (equals_ignore_case(command, "-stop")) {
        stop_serial(state);
        return;
    }

    if (equals_ignore_case(command, "-exit")) {
        *should_exit = 1;
        return;
    }

    fprintf(stderr, "Opcao desconhecida: %s\n", command);
}

/**
 * @brief Executa a aplicacao principal do Terminal BMT.
 *
 * @return int Retorna 0 quando a aplicacao finaliza corretamente.
 */
int terminal_run(void)
{
    SerialState *state;
    char input[INPUT_BUFFER_SIZE];
    int should_exit = 0;
    int config_loaded;

    state = (SerialState *)calloc(1, sizeof(*state));
    if (state == NULL) {
        fprintf(stderr, "Falha ao alocar memoria para o estado do terminal.\n");
        return 1;
    }

    initialize_serial_state(state);
    config_loaded = load_config(state);
    web_ui_set_autosave_enabled(&state->web_ui, state->config.autosave_enabled);
    if (state->can_state.config.dbc_vcu_path[0] != '\0') {
        load_can_dbc(state, "vcu", state->can_state.config.dbc_vcu_path);
    }
    if (state->can_state.config.dbc_inverter_path[0] != '\0') {
        load_can_dbc(state, "inverter", state->can_state.config.dbc_inverter_path);
    }
    if (state->web_ui.config.enabled &&
        (state->can_state.dbc_vcu_database.loaded || state->can_state.config.ui_can_monitor_enabled)) {
        web_ui_start(&state->web_ui, &state->can_state.dbc_vcu_database);
    }
    print_banner(config_loaded);

    while (!should_exit) {
        if (InterlockedCompareExchange(&state->running, 0, 0) ||
            InterlockedCompareExchange(&state->can_state.running, 0, 0)) {
            Sleep(50);
            continue;
        }

        print_prompt();
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        process_command(state, input, &should_exit);
    }

    if (InterlockedCompareExchange(&state->running, 0, 0)) {
        stop_serial(state);
    }

    if (InterlockedCompareExchange(&state->can_state.opened, 0, 0)) {
        close_can_interface(state);
    }

    web_ui_destroy(&state->web_ui);
    serial_monitor_destroy(&state->serial_monitor);
    serial_monitor_destroy(&state->can_monitor);
    DeleteCriticalSection(&state->console_lock);
    free(state);
    return 0;
}
