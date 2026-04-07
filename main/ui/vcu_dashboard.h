#ifndef VCU_DASHBOARD_H
#define VCU_DASHBOARD_H

#include <stddef.h>
#include <windows.h>

#include "can_types.h"
#include "dbc_types.h"

/**
 * @brief Mantem o snapshot mais recente dos sinais usados pelo dashboard VCU.
 */
typedef struct VcuDashboardData {
    int has_status;
    int has_pedals;
    SYSTEMTIME last_update;
    char vcu_state[64];
    char command_source[32];
    int torque_allowed;
    int torque_limited;
    char throttle_health[32];
    char brake_health[32];
    int regen_enabled;
    int yaw_enabled;
    int traction_enabled;
    int brake_dominance_hard;
    int hw_fault;
    int can_fault;
    int inverter_handshake_fault;
    int remote_authorized;
    int tcu_heartbeat_ok;
    int hpc_heartbeat_ok;
    int startup_failed;
    int controlled_reset_req;
    char throttle_state[32];
    char brake_state[32];
    int driver_takeover;
    int inverter_handshake_ok;
    int alive_counter;
    int status_version;
    double longitudinal_request;
    char throttle_raw_health[32];
    char brake_raw_health[32];
    int pedals_valid;
    int pedals_tx_enabled;
    int pedals_alive_counter;
} VcuDashboardData;

/**
 * @brief Reinicializa o estado do dashboard VCU para os valores padrao.
 *
 * @param data Estrutura do dashboard a ser reinicializada.
 */
void vcu_dashboard_reset(VcuDashboardData *data);

/**
 * @brief Atualiza o snapshot do dashboard a partir de um frame CAN decodificado por DBC.
 *
 * @param data Estrutura que recebera os valores atualizados.
 * @param database Banco DBC carregado.
 * @param frame Frame CAN recebido.
 * @return int Retorna 1 quando o frame foi reconhecido pelo dashboard, caso contrario 0.
 */
int vcu_dashboard_update_from_frame(VcuDashboardData *data, const DbcDatabase *database, const CanFrame *frame);

/**
 * @brief Serializa o snapshot do dashboard em JSON para a API web.
 *
 * @param data Estado atual do dashboard.
 * @param buffer Buffer de destino.
 * @param buffer_size Tamanho do buffer de destino.
 */
void vcu_dashboard_build_json(const VcuDashboardData *data, char *buffer, size_t buffer_size);

#endif
