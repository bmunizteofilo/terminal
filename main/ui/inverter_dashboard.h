#ifndef INVERTER_DASHBOARD_H
#define INVERTER_DASHBOARD_H

#include <stddef.h>
#include <windows.h>

#include "can_types.h"
#include "dbc_types.h"

/**
 * @brief Configura as bases de identificadores para os dois inversores.
 */
typedef struct InverterDashboardConfig {
    unsigned int left_base_id;
    unsigned int right_base_id;
    int left_base_set;
    int right_base_set;
} InverterDashboardConfig;

/**
 * @brief Representa o snapshot resumido de um lado do painel de inversor.
 */
typedef struct InverterDashboardSide {
    int configured;
    int online;
    SYSTEMTIME last_update;
    unsigned int command_id;
    unsigned int measures_id;
    unsigned int states_id;
    unsigned int gd_id;
    int onoff;
    char control_mode[32];
    char direction[32];
    double speed_ref;
    double torque_ref;
    double voltage_dc;
    double current_ac;
    double voltage_ac;
    double speed_motor;
    double temp_motor;
    double temp_motor_aux;
    double temp_igbt;
    char state[32];
    char event[48];
    int over_speed;
    int over_voltage;
    int under_voltage;
    int over_temp_igbt;
    int over_temp_motor;
    int foc_error;
    int run_error;
    int init_error;
    int gd_ready_count;
    int gd_fault_count;
} InverterDashboardSide;

/**
 * @brief Mantem o estado completo do dashboard duplo dos inversores.
 */
typedef struct InverterDashboardData {
    InverterDashboardSide left;
    InverterDashboardSide right;
} InverterDashboardData;

/**
 * @brief Define valores padrao para a configuracao do painel de inversores.
 *
 * @param config Estrutura de configuracao a ser inicializada.
 */
void inverter_dashboard_set_default_config(InverterDashboardConfig *config);

/**
 * @brief Reinicializa os dados em tempo real do painel de inversores.
 *
 * @param data Estrutura de dados a ser limpa.
 */
void inverter_dashboard_reset(InverterDashboardData *data);

/**
 * @brief Atualiza as leituras dos inversores a partir de um frame CAN.
 *
 * @param data Estado atual do dashboard de inversores.
 * @param config Configuracao das bases esquerda e direita.
 * @param database Banco DBC dos inversores.
 * @param frame Frame recebido do barramento.
 * @return int Retorna 1 quando o frame foi reconhecido pelo painel.
 */
int inverter_dashboard_update_from_frame(InverterDashboardData *data,
                                         const InverterDashboardConfig *config,
                                         const DbcDatabase *database,
                                         const CanFrame *frame);

/**
 * @brief Serializa o estado dos dois inversores para JSON.
 *
 * @param data Estado atual do dashboard.
 * @param config Configuracao das bases.
 * @param buffer Buffer de destino.
 * @param buffer_size Tamanho do buffer de destino.
 */
void inverter_dashboard_build_json(const InverterDashboardData *data,
                                   const InverterDashboardConfig *config,
                                   char *buffer,
                                   size_t buffer_size);

#endif
