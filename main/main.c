#define _CRT_SECURE_NO_WARNINGS

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
#define BMT_VERSION "0.2.0"

typedef enum ParityMode {
    BMT_PARITY_NONE,
    BMT_PARITY_ODD,
    BMT_PARITY_EVEN,
    BMT_PARITY_MARK,
    BMT_PARITY_SPACE
} ParityMode;

typedef enum StopBitsMode {
    BMT_STOP_BITS_1,
    BMT_STOP_BITS_1_5,
    BMT_STOP_BITS_2
} StopBitsMode;

typedef enum FlowControlMode {
    BMT_FLOW_NONE,
    BMT_FLOW_XON_XOFF,
    BMT_FLOW_RTS_CTS,
    BMT_FLOW_DSR_DTR
} FlowControlMode;

typedef enum MonitorMode {
    BMT_MONITOR_RAW,
    BMT_MONITOR_ESP32
} MonitorMode;

typedef enum LogFilterMode {
    BMT_FILTER_ALL,
    BMT_FILTER_INFO,
    BMT_FILTER_WARNING,
    BMT_FILTER_ERROR,
    BMT_FILTER_DEBUG,
    BMT_FILTER_VERBOSE
} LogFilterMode;

typedef enum ThemeMode {
    BMT_THEME_DEFAULT,
    BMT_THEME_LIGHT,
    BMT_THEME_HIGHCONTRAST
} ThemeMode;

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
} SerialState;

static int equals_ignore_case(const char *left, const char *right);
static void set_default_config(SerialConfig *config);
static int parse_parity(const char *value, ParityMode *parity);
static int parse_stop_bits(const char *value, StopBitsMode *stop_bits);
static int parse_flow_control(const char *value, FlowControlMode *flow_control);
static int parse_monitor_mode(const char *value, MonitorMode *monitor_mode);
static int parse_filter_mode(const char *value, LogFilterMode *filter_mode);
static int parse_theme_mode(const char *value, ThemeMode *theme_mode);
static const char *parity_to_string(ParityMode parity);
static const char *stop_bits_to_string(StopBitsMode stop_bits);
static const char *flow_control_to_string(FlowControlMode flow_control);
static const char *monitor_mode_to_string(MonitorMode monitor_mode);
static const char *filter_mode_to_string(LogFilterMode filter_mode);
static const char *theme_mode_to_string(ThemeMode theme_mode);
static void print_prompt(void);
static void stop_serial(SerialState *state);
static int save_config(const SerialConfig *config);
static void auto_save_if_enabled(const SerialState *state);

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
        int ctrl_pressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        int shift_pressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        int t_pressed = (GetAsyncKeyState('T') & 0x8000) != 0;
        int combo_pressed = ctrl_pressed && shift_pressed && t_pressed;
        int pause_pressed = !ctrl_pressed && !shift_pressed && (GetAsyncKeyState('P') & 0x8000) != 0;
        int resume_pressed = !ctrl_pressed && !shift_pressed && (GetAsyncKeyState('R') & 0x8000) != 0;

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
 * @param config Estrutura que recebera os valores carregados.
 * @return int Retorna 1 quando uma configuracao valida foi carregada, caso contrario 0.
 */
static int load_config(SerialConfig *config)
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

        if (apply_config_entry(config, key, value)) {
            loaded_any_value = 1;
        }
    }

    fclose(file);
    return loaded_any_value;
}

/**
 * @brief Salva a configuracao atual em disco para reutilizacao futura.
 *
 * @param config Configuracao serial a ser persistida.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
static int save_config(const SerialConfig *config)
{
    FILE *file = fopen(CONFIG_FILE_PATH, "w");

    if (file == NULL) {
        return 0;
    }

    fprintf(file, "com_port=%s\n", config->com_port);
    fprintf(file, "baud_rate=%lu\n", (unsigned long)config->baud_rate);
    fprintf(file, "data_bits=%u\n", (unsigned int)config->data_bits);
    fprintf(file, "parity=%s\n", parity_to_string(config->parity));
    fprintf(file, "stop_bits=%s\n", stop_bits_to_string(config->stop_bits));
    fprintf(file, "flow_control=%s\n", flow_control_to_string(config->flow_control));
    fprintf(file, "esp32_logs=%s\n", bool_to_on_off(config->esp32_log_colors_enabled));
    fprintf(file, "timestamp=%s\n", bool_to_on_off(config->timestamp_enabled));
    fprintf(file, "logfile=%s\n", bool_to_on_off(config->logfile_enabled));
    fprintf(file, "logfile_path=%s\n", config->logfile_path);
    fprintf(file, "autosave=%s\n", bool_to_on_off(config->autosave_enabled));
    fprintf(file, "reconnect=%s\n", bool_to_on_off(config->reconnect_enabled));
    fprintf(file, "monitor=%s\n", monitor_mode_to_string(config->monitor_mode));
    fprintf(file, "filter=%s\n", filter_mode_to_string(config->filter_mode));
    fprintf(file, "theme=%s\n", theme_mode_to_string(config->theme_mode));
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
        save_config(&state->config);
    }
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
        printf("Timestamp ativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->config.timestamp_enabled = 0;
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
        printf("Log em arquivo ativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->config.logfile_enabled = 0;
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
        save_config(&state->config);
        printf("Autosave ativado.\n");
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->config.autosave_enabled = 0;
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
        printf("Reconnect automatico ativado.\n");
        auto_save_if_enabled(state);
        return 1;
    }

    if (equals_ignore_case(value, "off")) {
        state->config.reconnect_enabled = 0;
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
 * @brief Restaura a configuracao padrao do terminal.
 *
 * @param state Estado global do terminal serial.
 */
static void reset_config(SerialState *state)
{
    set_default_config(&state->config);
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
    SerialConfig config;

    set_default_config(&config);
    if (!load_config(&config)) {
        fprintf(stderr, "Nao foi possivel carregar %s.\n", CONFIG_FILE_PATH);
        return;
    }

    state->config = config;
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
    printf("Comunicacao serial encerrada.\n");
}

/**
 * @brief Inicializa o estado interno do terminal.
 *
 * @param state Estado global do terminal serial.
 */
static void initialize_serial_state(SerialState *state)
{
    ZeroMemory(state, sizeof(*state));
    set_default_config(&state->config);
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

    if (equals_ignore_case(command, "-save")) {
        if (save_config(&state->config)) {
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
 * @brief Ponto de entrada do Terminal BMT.
 *
 * @return int Retorna 0 quando a aplicacao finaliza corretamente.
 */
int main(void)
{
    SerialState state;
    char input[INPUT_BUFFER_SIZE];
    int should_exit = 0;
    int config_loaded;

    initialize_serial_state(&state);
    config_loaded = load_config(&state.config);
    print_banner(config_loaded);

    while (!should_exit) {
        if (InterlockedCompareExchange(&state.running, 0, 0)) {
            Sleep(50);
            continue;
        }

        print_prompt();
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        process_command(&state, input, &should_exit);
    }

    if (InterlockedCompareExchange(&state.running, 0, 0)) {
        stop_serial(&state);
    }

    DeleteCriticalSection(&state.console_lock);
    return 0;
}
