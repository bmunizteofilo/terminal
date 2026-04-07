#define _CRT_SECURE_NO_WARNINGS

#include "vcu_dashboard.h"

#include <stdio.h>
#include <string.h>

#include "dbc_parser.h"

/**
 * @brief Decodifica um sinal textual do DBC usando a descricao enum, quando existir.
 *
 * @param message Mensagem DBC correspondente ao frame.
 * @param signal_name Nome do sinal desejado.
 * @param frame Frame recebido.
 * @param output Buffer de destino.
 * @param output_size Tamanho do buffer de destino.
 * @return int Retorna 1 quando o sinal foi decodificado, caso contrario 0.
 */
static int vcu_decode_text_signal(const DbcMessage *message,
                                  const char *signal_name,
                                  const CanFrame *frame,
                                  char *output,
                                  size_t output_size)
{
    const DbcSignal *signal;
    const char *enum_text;
    long long raw_value;
    double physical_value;

    signal = dbc_find_signal_by_name(message, signal_name);
    if (signal == NULL || !dbc_decode_signal(signal, frame->data, frame->data_length, &raw_value, &physical_value)) {
        return 0;
    }

    enum_text = dbc_find_value_text(signal, (int)raw_value);
    if (enum_text != NULL) {
        snprintf(output, output_size, "%s", enum_text);
    } else {
        snprintf(output, output_size, "%.3f", physical_value);
    }

    return 1;
}

/**
 * @brief Decodifica um sinal inteiro do DBC.
 *
 * @param message Mensagem DBC correspondente ao frame.
 * @param signal_name Nome do sinal desejado.
 * @param frame Frame recebido.
 * @param value Ponteiro que recebera o valor inteiro.
 * @return int Retorna 1 quando o sinal foi decodificado, caso contrario 0.
 */
static int vcu_decode_int_signal(const DbcMessage *message,
                                 const char *signal_name,
                                 const CanFrame *frame,
                                 int *value)
{
    const DbcSignal *signal;
    long long raw_value;

    signal = dbc_find_signal_by_name(message, signal_name);
    if (signal == NULL || !dbc_decode_signal(signal, frame->data, frame->data_length, &raw_value, NULL)) {
        return 0;
    }

    *value = (int)raw_value;
    return 1;
}

/**
 * @brief Decodifica um sinal numerico em ponto flutuante do DBC.
 *
 * @param message Mensagem DBC correspondente ao frame.
 * @param signal_name Nome do sinal desejado.
 * @param frame Frame recebido.
 * @param value Ponteiro que recebera o valor fisico.
 * @return int Retorna 1 quando o sinal foi decodificado, caso contrario 0.
 */
static int vcu_decode_double_signal(const DbcMessage *message,
                                    const char *signal_name,
                                    const CanFrame *frame,
                                    double *value)
{
    const DbcSignal *signal;

    signal = dbc_find_signal_by_name(message, signal_name);
    if (signal == NULL || !dbc_decode_signal(signal, frame->data, frame->data_length, NULL, value)) {
        return 0;
    }

    return 1;
}

/**
 * @brief Escapa aspas e barras em uma string para uso seguro em JSON.
 *
 * @param text Texto de entrada.
 * @param buffer Buffer de destino.
 * @param buffer_size Tamanho do buffer de destino.
 */
static void json_escape(const char *text, char *buffer, size_t buffer_size)
{
    size_t in_index = 0;
    size_t out_index = 0;

    if (buffer_size == 0U) {
        return;
    }

    while (text[in_index] != '\0' && out_index + 1U < buffer_size) {
        if ((text[in_index] == '\\' || text[in_index] == '"') && out_index + 2U < buffer_size) {
            buffer[out_index++] = '\\';
            buffer[out_index++] = text[in_index++];
            continue;
        }

        buffer[out_index++] = text[in_index++];
    }

    buffer[out_index] = '\0';
}

/**
 * @brief Restaura o estado padrao do dashboard VCU.
 *
 * @param data Estrutura de dados do dashboard a ser reinicializada.
 */
void vcu_dashboard_reset(VcuDashboardData *data)
{
    ZeroMemory(data, sizeof(*data));
    snprintf(data->vcu_state, sizeof(data->vcu_state), "N/A");
    snprintf(data->command_source, sizeof(data->command_source), "N/A");
    snprintf(data->throttle_health, sizeof(data->throttle_health), "N/A");
    snprintf(data->brake_health, sizeof(data->brake_health), "N/A");
    snprintf(data->throttle_state, sizeof(data->throttle_state), "N/A");
    snprintf(data->brake_state, sizeof(data->brake_state), "N/A");
    snprintf(data->throttle_raw_health, sizeof(data->throttle_raw_health), "N/A");
    snprintf(data->brake_raw_health, sizeof(data->brake_raw_health), "N/A");
}

/**
 * @brief Serializa o estado atual do dashboard VCU em JSON.
 *
 * @param data Estrutura com os dados atuais do dashboard.
 * @param buffer Buffer de saida que recebera o JSON.
 * @param buffer_size Tamanho do buffer de saida.
 */
void vcu_dashboard_build_json(const VcuDashboardData *data, char *buffer, size_t buffer_size)
{
    char timestamp[32] = "";
    char vcu_state[128];
    char command_source[128];
    char throttle_health[128];
    char brake_health[128];
    char throttle_state[128];
    char brake_state[128];
    char throttle_raw_health[128];
    char brake_raw_health[128];

    if (data->has_status || data->has_pedals) {
        snprintf(timestamp, sizeof(timestamp), "%02u:%02u:%02u.%03u",
                 (unsigned int)data->last_update.wHour,
                 (unsigned int)data->last_update.wMinute,
                 (unsigned int)data->last_update.wSecond,
                 (unsigned int)data->last_update.wMilliseconds);
    }

    json_escape(data->vcu_state, vcu_state, sizeof(vcu_state));
    json_escape(data->command_source, command_source, sizeof(command_source));
    json_escape(data->throttle_health, throttle_health, sizeof(throttle_health));
    json_escape(data->brake_health, brake_health, sizeof(brake_health));
    json_escape(data->throttle_state, throttle_state, sizeof(throttle_state));
    json_escape(data->brake_state, brake_state, sizeof(brake_state));
    json_escape(data->throttle_raw_health, throttle_raw_health, sizeof(throttle_raw_health));
    json_escape(data->brake_raw_health, brake_raw_health, sizeof(brake_raw_health));

    snprintf(buffer, buffer_size,
             "{"
             "\"last_update\":\"%s\",\"has_status\":%s,\"has_pedals\":%s,"
             "\"vcu_state\":\"%s\",\"command_source\":\"%s\","
             "\"torque_allowed\":%s,\"torque_limited\":%s,"
             "\"throttle_health\":\"%s\",\"brake_health\":\"%s\","
             "\"regen_enabled\":%s,\"yaw_enabled\":%s,\"traction_enabled\":%s,"
             "\"brake_dominance_hard\":%s,\"hw_fault\":%s,\"can_fault\":%s,"
             "\"inverter_handshake_fault\":%s,\"remote_authorized\":%s,"
             "\"tcu_heartbeat_ok\":%s,\"hpc_heartbeat_ok\":%s,\"startup_failed\":%s,"
             "\"controlled_reset_req\":%s,\"throttle_state\":\"%s\",\"brake_state\":\"%s\","
             "\"driver_takeover\":%s,\"inverter_handshake_ok\":%s,"
             "\"alive_counter\":%d,\"status_version\":%d,\"longitudinal_request\":%.3f,"
             "\"throttle_raw_health\":\"%s\",\"brake_raw_health\":\"%s\","
             "\"pedals_valid\":%s,\"pedals_tx_enabled\":%s,\"pedals_alive_counter\":%d}",
             timestamp,
             data->has_status ? "true" : "false",
             data->has_pedals ? "true" : "false",
             vcu_state,
             command_source,
             data->torque_allowed ? "true" : "false",
             data->torque_limited ? "true" : "false",
             throttle_health,
             brake_health,
             data->regen_enabled ? "true" : "false",
             data->yaw_enabled ? "true" : "false",
             data->traction_enabled ? "true" : "false",
             data->brake_dominance_hard ? "true" : "false",
             data->hw_fault ? "true" : "false",
             data->can_fault ? "true" : "false",
             data->inverter_handshake_fault ? "true" : "false",
             data->remote_authorized ? "true" : "false",
             data->tcu_heartbeat_ok ? "true" : "false",
             data->hpc_heartbeat_ok ? "true" : "false",
             data->startup_failed ? "true" : "false",
             data->controlled_reset_req ? "true" : "false",
             throttle_state,
             brake_state,
             data->driver_takeover ? "true" : "false",
             data->inverter_handshake_ok ? "true" : "false",
             data->alive_counter,
             data->status_version,
             data->longitudinal_request,
             throttle_raw_health,
             brake_raw_health,
             data->pedals_valid ? "true" : "false",
             data->pedals_tx_enabled ? "true" : "false",
             data->pedals_alive_counter);
}

/**
 * @brief Atualiza o dashboard VCU a partir de um frame CAN e do DBC carregado.
 *
 * @param data Estrutura de dados do dashboard VCU.
 * @param database Banco DBC usado para interpretar os sinais.
 * @param frame Frame CAN recebido.
 * @return int Retorna 1 quando o frame atualizou o dashboard, caso contrario 0.
 */
int vcu_dashboard_update_from_frame(VcuDashboardData *data, const DbcDatabase *database, const CanFrame *frame)
{
    const DbcMessage *message;

    if (data == NULL || database == NULL || frame == NULL || !database->loaded) {
        return 0;
    }

    message = dbc_find_message_by_id(database, frame->id);
    if (message == NULL) {
        return 0;
    }

    if (strcmp(message->name, "VCU_Status") == 0) {
        vcu_decode_text_signal(message, "VCU_State", frame, data->vcu_state, sizeof(data->vcu_state));
        vcu_decode_text_signal(message, "VCU_CommandSource", frame, data->command_source, sizeof(data->command_source));
        vcu_decode_text_signal(message, "VCU_ThrottleHealth", frame, data->throttle_health, sizeof(data->throttle_health));
        vcu_decode_text_signal(message, "VCU_BrakeHealth", frame, data->brake_health, sizeof(data->brake_health));
        vcu_decode_text_signal(message, "VCU_ThrottleState", frame, data->throttle_state, sizeof(data->throttle_state));
        vcu_decode_text_signal(message, "VCU_BrakeState", frame, data->brake_state, sizeof(data->brake_state));
        vcu_decode_int_signal(message, "VCU_TorqueAllowed", frame, &data->torque_allowed);
        vcu_decode_int_signal(message, "VCU_TorqueLimited", frame, &data->torque_limited);
        vcu_decode_int_signal(message, "VCU_RegenEnabled", frame, &data->regen_enabled);
        vcu_decode_int_signal(message, "VCU_YawEnabled", frame, &data->yaw_enabled);
        vcu_decode_int_signal(message, "VCU_TractionEnabled", frame, &data->traction_enabled);
        vcu_decode_int_signal(message, "VCU_BrakeDominanceHard", frame, &data->brake_dominance_hard);
        vcu_decode_int_signal(message, "VCU_HwFault", frame, &data->hw_fault);
        vcu_decode_int_signal(message, "VCU_CanFault", frame, &data->can_fault);
        vcu_decode_int_signal(message, "VCU_InverterHandshakeFault", frame, &data->inverter_handshake_fault);
        vcu_decode_int_signal(message, "VCU_RemoteAuthorized", frame, &data->remote_authorized);
        vcu_decode_int_signal(message, "VCU_TcuHeartbeatOk", frame, &data->tcu_heartbeat_ok);
        vcu_decode_int_signal(message, "VCU_HpcHeartbeatOk", frame, &data->hpc_heartbeat_ok);
        vcu_decode_int_signal(message, "VCU_StartupFailed", frame, &data->startup_failed);
        vcu_decode_int_signal(message, "VCU_ControlledResetReq", frame, &data->controlled_reset_req);
        vcu_decode_int_signal(message, "VCU_DriverTakeover", frame, &data->driver_takeover);
        vcu_decode_int_signal(message, "VCU_InverterHandshakeOk", frame, &data->inverter_handshake_ok);
        vcu_decode_int_signal(message, "VCU_AliveCounter", frame, &data->alive_counter);
        vcu_decode_int_signal(message, "VCU_StatusVersion", frame, &data->status_version);
        data->has_status = 1;
    } else if (strcmp(message->name, "VCU_Pedals") == 0) {
        vcu_decode_text_signal(message, "VCU_ThrottleRawHealth", frame, data->throttle_raw_health, sizeof(data->throttle_raw_health));
        vcu_decode_text_signal(message, "VCU_BrakeRawHealth", frame, data->brake_raw_health, sizeof(data->brake_raw_health));
        vcu_decode_double_signal(message, "VCU_LongitudinalRequest", frame, &data->longitudinal_request);
        vcu_decode_int_signal(message, "VCU_PedalsValid", frame, &data->pedals_valid);
        vcu_decode_int_signal(message, "VCU_PedalsTxEnabled", frame, &data->pedals_tx_enabled);
        vcu_decode_int_signal(message, "VCU_PedalsAliveCounter", frame, &data->pedals_alive_counter);
        data->has_pedals = 1;
    } else {
        return 0;
    }

    data->last_update = frame->timestamp;
    return 1;
}
