#ifndef DBC_TYPES_H
#define DBC_TYPES_H

#include <windows.h>

#define DBC_MAX_MESSAGES 64
#define DBC_MAX_SIGNALS_PER_MESSAGE 64
#define DBC_MAX_ENUM_VALUES 32
#define DBC_NAME_SIZE 64
#define DBC_UNIT_SIZE 32
#define DBC_NODE_SIZE 32
#define DBC_PATH_SIZE MAX_PATH

/**
 * @brief Descreve um valor textual associado a um valor enumerado de um sinal.
 */
typedef struct DbcValueDescription {
    int value;
    char text[DBC_NAME_SIZE];
} DbcValueDescription;

/**
 * @brief Representa um sinal carregado do arquivo DBC.
 */
typedef struct DbcSignal {
    char name[DBC_NAME_SIZE];
    unsigned int start_bit;
    unsigned int bit_length;
    int is_little_endian;
    int is_signed;
    double factor;
    double offset;
    double minimum;
    double maximum;
    char unit[DBC_UNIT_SIZE];
    char receiver[DBC_NODE_SIZE];
    DbcValueDescription values[DBC_MAX_ENUM_VALUES];
    int value_count;
} DbcSignal;

/**
 * @brief Representa uma mensagem CAN carregada do arquivo DBC.
 */
typedef struct DbcMessage {
    unsigned int id;
    char name[DBC_NAME_SIZE];
    unsigned int dlc;
    char transmitter[DBC_NODE_SIZE];
    DbcSignal signals[DBC_MAX_SIGNALS_PER_MESSAGE];
    int signal_count;
} DbcMessage;

/**
 * @brief Mantem o estado em memoria do dicionario DBC carregado pelo terminal.
 */
typedef struct DbcDatabase {
    int loaded;
    char path[DBC_PATH_SIZE];
    char version[DBC_NAME_SIZE];
    DbcMessage messages[DBC_MAX_MESSAGES];
    int message_count;
    int total_signal_count;
} DbcDatabase;

#endif
