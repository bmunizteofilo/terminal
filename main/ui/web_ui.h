#ifndef WEB_UI_H
#define WEB_UI_H

#include <stdio.h>
#include <windows.h>

#include "can_types.h"
#include "dbc_types.h"
#include "inverter_dashboard.h"
#include "serial_monitor.h"
#include "vcu_dashboard.h"

#define WEB_UI_DEFAULT_PORT 8080

/**
 * @brief Armazena as configuracoes persistentes da UI web local.
 */
typedef struct WebUiConfig {
    int enabled;
    unsigned short port;
} WebUiConfig;

/**
 * @brief Snapshot publico das configuracoes seriais exibidas na UI web.
 */
typedef struct WebUiSerialSnapshot {
    char com_port[32];
    unsigned int baud_rate;
    unsigned int data_bits;
    char parity[16];
    char stop_bits[16];
    char flow_control[16];
    int timestamp_enabled;
    int logfile_enabled;
    int reconnect_enabled;
    char logfile_path[260];
    int serial_running;
} WebUiSerialSnapshot;

/**
 * @brief Agrupa callbacks que permitem a UI web acionar a logica principal do terminal.
 */
typedef struct WebUiCallbacks {
    void *context;
    int (*list_ports)(void *context, char ports[][32], int max_ports);
    int (*apply_serial_settings)(void *context,
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
    int (*apply_can_settings)(void *context, const char *com_port, unsigned int bitrate, char *message, size_t message_size);
    int (*load_dbc)(void *context, const char *path, char *message, size_t message_size);
    int (*execute_action)(void *context, const char *action, char *message, size_t message_size);
} WebUiCallbacks;

/**
 * @brief Mantem o estado do servidor HTTP local e o snapshot do dashboard VCU.
 */
typedef struct WebUiState {
    WebUiConfig config;
    HANDLE thread;
    DWORD thread_id;
    volatile LONG running;
    volatile LONG can_opened;
    volatile LONG can_running;
    volatile LONG dbc_loaded;
    UINT_PTR listen_socket;
    CRITICAL_SECTION lock;
    unsigned long rx_count;
    volatile LONG autosave_enabled;
    const CanConfig *can_config;
    const DbcDatabase *dbc_database;
    const DbcDatabase *inverter_dbc_database;
    SerialMonitorState *serial_monitor;
    SerialMonitorState *can_monitor;
    WebUiCallbacks callbacks;
    WebUiSerialSnapshot serial_snapshot;
    VcuDashboardData dashboard;
    InverterDashboardConfig inverter_config;
    InverterDashboardData inverter_dashboard;
} WebUiState;

/**
 * @brief Inicializa a configuracao padrao da UI web.
 *
 * @param config Estrutura de configuracao a ser inicializada.
 */
void web_ui_set_default_config(WebUiConfig *config);

/**
 * @brief Aplica uma chave de configuracao da UI carregada do arquivo persistente.
 *
 * @param config Estrutura de configuracao alvo.
 * @param key Chave lida do arquivo.
 * @param value Valor textual da chave.
 * @return int Retorna 1 quando a chave foi reconhecida, caso contrario 0.
 */
int web_ui_apply_config_entry(WebUiConfig *config, const char *key, const char *value);

/**
 * @brief Persiste as configuracoes da UI web no arquivo de configuracao.
 *
 * @param file Arquivo de configuracao aberto para escrita.
 * @param config Configuracao a ser salva.
 */
void web_ui_save_config(FILE *file, const WebUiConfig *config);

/**
 * @brief Inicializa os recursos internos do subsistema web.
 *
 * @param state Estado da UI web a ser inicializado.
 */
void web_ui_initialize(WebUiState *state);

/**
 * @brief Vincula a UI web ao estado compartilhado do backend.
 *
 * @param state Estado da UI web.
 * @param can_config Ponteiro para a configuracao CAN atual.
 * @param dbc_database Ponteiro para o banco DBC da VCU.
 * @param inverter_dbc_database Ponteiro para o banco DBC dos inversores.
 * @param serial_monitor Ponteiro para o buffer do monitor serial web.
 * @param callbacks Conjunto de callbacks de acao da UI.
 */
void web_ui_bind_runtime(WebUiState *state,
                         const CanConfig *can_config,
                         const DbcDatabase *dbc_database,
                         const DbcDatabase *inverter_dbc_database,
                         SerialMonitorState *serial_monitor,
                         SerialMonitorState *can_monitor,
                         const WebUiCallbacks *callbacks);

/**
 * @brief Libera os recursos do subsistema web.
 *
 * @param state Estado da UI web a ser destruido.
 */
void web_ui_destroy(WebUiState *state);

/**
 * @brief Inicia o servidor HTTP local da UI web.
 *
 * @param state Estado da UI web.
 * @param database Banco DBC carregado.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int web_ui_start(WebUiState *state, const DbcDatabase *database);

/**
 * @brief Encerra o servidor HTTP local da UI web.
 *
 * @param state Estado da UI web.
 */
void web_ui_stop(WebUiState *state);

/**
 * @brief Abre o navegador padrao apontando para o dashboard local.
 *
 * @param state Estado da UI web.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int web_ui_open_browser(const WebUiState *state);

/**
 * @brief Atualiza o snapshot do dashboard com base em um frame CAN.
 *
 * @param state Estado da UI web.
 * @param database Banco DBC carregado.
 * @param frame Frame CAN recebido.
 */
void web_ui_update_from_frame(WebUiState *state, const DbcDatabase *database, const CanFrame *frame);

/**
 * @brief Reinicializa o snapshot atual do dashboard VCU.
 *
 * @param state Estado da UI web.
 */
void web_ui_reset_dashboard(WebUiState *state);

/**
 * @brief Reinicializa apenas o snapshot dos inversores exibido na UI web.
 *
 * @param state Estado da UI web.
 */
void web_ui_reset_inverters(WebUiState *state);

/**
 * @brief Exibe um resumo do estado atual da UI web.
 *
 * @param state Estado da UI web.
 * @param database Banco DBC carregado.
 */
void web_ui_print_status(const WebUiState *state, const DbcDatabase *database);

/**
 * @brief Atualiza os flags de transporte CAN refletidos no dashboard web.
 *
 * @param state Estado da UI web.
 * @param can_opened Indica se a interface CAN esta aberta.
 * @param can_running Indica se o barramento CAN esta ativo.
 */
void web_ui_set_can_runtime(WebUiState *state, int can_opened, int can_running);

/**
 * @brief Atualiza o flag de autosave refletido na pagina de configuracao.
 *
 * @param state Estado da UI web.
 * @param enabled Indica se o autosave esta ativo.
 */
void web_ui_set_autosave_enabled(WebUiState *state, int enabled);

/**
 * @brief Atualiza o snapshot serial exposto na pagina de configuracao.
 *
 * @param state Estado da UI web.
 * @param snapshot Valores seriais atuais do terminal.
 */
void web_ui_set_serial_snapshot(WebUiState *state, const WebUiSerialSnapshot *snapshot);

#endif
