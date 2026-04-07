#ifndef CAN_TYPES_H
#define CAN_TYPES_H

#include <stddef.h>
#include <windows.h>

#include "dbc_types.h"

#define CAN_LOG_PATH_SIZE 260
#define CAN_PARTIAL_BUFFER_SIZE 256

/**
 * @brief Define os modos de exibicao disponiveis para o monitor CAN.
 */
typedef enum CanViewMode {
    CAN_VIEW_RAW,
    CAN_VIEW_TABLE
} CanViewMode;

/**
 * @brief Indica a direcao associada a um frame CAN exibido no monitor.
 */
typedef enum CanDirection {
    CAN_DIR_RX,
    CAN_DIR_TX
} CanDirection;

/**
 * @brief Representa um frame CAN classico interpretado a partir do protocolo SLCAN.
 */
typedef struct CanFrame {
    unsigned int id;
    unsigned char dlc;
    unsigned char data[8];
    unsigned char data_length;
    int is_extended;
    int is_remote;
    CanDirection direction;
    SYSTEMTIME timestamp;
} CanFrame;

/**
 * @brief Armazena a configuracao persistente e operacional do subsistema CAN.
 */
typedef struct CanConfig {
    char com_port[32];
    unsigned int bitrate;
    CanViewMode view_mode;
    int timestamp_enabled;
    int colors_enabled;
    int filter_id_enabled;
    unsigned int filter_id;
    char logfile_path[CAN_LOG_PATH_SIZE];
    char dbc_vcu_path[CAN_LOG_PATH_SIZE];
    char dbc_inverter_path[CAN_LOG_PATH_SIZE];
    unsigned int inverter_left_base;
    unsigned int inverter_right_base;
    int inverter_left_base_set;
    int inverter_right_base_set;
    int ui_can_monitor_enabled;
} CanConfig;

/**
 * @brief Consolida contadores e indicadores de uso da sessao CAN atual.
 */
typedef struct CanStats {
    unsigned long rx_count;
    unsigned long tx_count;
    unsigned long filtered_count;
    unsigned long parse_error_count;
    unsigned long dropped_count;
    unsigned int last_id;
} CanStats;

/**
 * @brief Mantem o estado completo do backend CAN/SLCAN em execucao.
 */
typedef struct CanState {
    HANDLE handle;
    HANDLE reader_thread;
    HANDLE hotkey_thread;
    DWORD reader_thread_id;
    DWORD hotkey_thread_id;
    volatile LONG opened;
    volatile LONG running;
    volatile LONG stop_requested;
    char partial_line[CAN_PARTIAL_BUFFER_SIZE];
    size_t partial_length;
    int table_header_printed;
    CanConfig config;
    CanStats stats;
    DbcDatabase dbc_vcu_database;
    DbcDatabase dbc_inverter_database;
} CanState;

#endif
