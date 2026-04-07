#define _CRT_SECURE_NO_WARNINGS

#include "inverter_dashboard.h"

#include <stdio.h>
#include <string.h>

#include "dbc_parser.h"

#define INVERTER_MEASURES_OFFSET 0x1000U
#define INVERTER_STATES_OFFSET 0x1100U
#define INVERTER_GD_OFFSET 0x1200U

/**
 * @brief Escapa texto para uso seguro em JSON.
 *
 * @param text Texto de entrada.
 * @param buffer Buffer de saida.
 * @param buffer_size Tamanho do buffer de saida.
 */
static void inverter_json_escape(const char *text, char *buffer, size_t buffer_size)
{
    size_t in_index = 0U;
    size_t out_index = 0U;

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
 * @brief Reseta o estado de um dos lados do dashboard.
 *
 * @param side Lado a ser reinicializado.
 */
static void inverter_dashboard_reset_side(InverterDashboardSide *side)
{
    ZeroMemory(side, sizeof(*side));
    snprintf(side->control_mode, sizeof(side->control_mode), "N/A");
    snprintf(side->direction, sizeof(side->direction), "N/A");
    snprintf(side->state, sizeof(side->state), "N/A");
    snprintf(side->event, sizeof(side->event), "N/A");
}

/**
 * @brief Decodifica um sinal textual usando enums do DBC quando disponiveis.
 *
 * @param message Mensagem de referencia do DBC.
 * @param signal_name Nome do sinal.
 * @param frame Frame recebido.
 * @param output Buffer de saida.
 * @param output_size Tamanho do buffer de saida.
 * @return int Retorna 1 em caso de sucesso.
 */
static int inverter_decode_text_signal(const DbcMessage *message,
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
 * @brief Decodifica um sinal inteiro do frame recebido.
 *
 * @param message Mensagem de referencia do DBC.
 * @param signal_name Nome do sinal.
 * @param frame Frame recebido.
 * @param value Ponteiro de saida.
 * @return int Retorna 1 em caso de sucesso.
 */
static int inverter_decode_int_signal(const DbcMessage *message,
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
 * @brief Decodifica um sinal numerico em ponto flutuante.
 *
 * @param message Mensagem de referencia do DBC.
 * @param signal_name Nome do sinal.
 * @param frame Frame recebido.
 * @param value Ponteiro de saida.
 * @return int Retorna 1 em caso de sucesso.
 */
static int inverter_decode_double_signal(const DbcMessage *message,
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
 * @brief Atualiza um lado do painel usando o template de mensagens do DBC.
 *
 * @param side Lado do painel a ser atualizado.
 * @param frame Frame recebido.
 * @param database Banco DBC do inversor.
 * @return int Retorna 1 quando o frame foi reconhecido.
 */
static int inverter_update_side(InverterDashboardSide *side, const DbcDatabase *database, const CanFrame *frame)
{
    const DbcMessage *commands_message;
    const DbcMessage *measures_message;
    const DbcMessage *states_message;
    const DbcMessage *gd_message;
    int value;
    int gd_index;

    if (!side->configured) {
        return 0;
    }

    commands_message = dbc_find_message_by_name(database, "COMMANDS_RX_MSG_ID_1");
    measures_message = dbc_find_message_by_name(database, "MEASURES_TX_MSG_ID_1");
    states_message = dbc_find_message_by_name(database, "STATES_TX_MSG_ID_1");
    gd_message = dbc_find_message_by_name(database, "GD_TX_MSG_ID_1");

    if (frame->id == side->command_id && commands_message != NULL) {
        inverter_decode_int_signal(commands_message, "OnOff_1", frame, &side->onoff);
        inverter_decode_text_signal(commands_message, "ControlMode_1", frame, side->control_mode, sizeof(side->control_mode));
        inverter_decode_text_signal(commands_message, "Direction_1", frame, side->direction, sizeof(side->direction));
        inverter_decode_double_signal(commands_message, "SpeedRef_1", frame, &side->speed_ref);
        inverter_decode_double_signal(commands_message, "TorqueRef_1", frame, &side->torque_ref);
    } else if (frame->id == side->measures_id && measures_message != NULL) {
        inverter_decode_double_signal(measures_message, "VoltageDC_1", frame, &side->voltage_dc);
        inverter_decode_double_signal(measures_message, "CurrentAC_1", frame, &side->current_ac);
        inverter_decode_double_signal(measures_message, "VoltageAC_1", frame, &side->voltage_ac);
        inverter_decode_double_signal(measures_message, "SpeedMotor_1", frame, &side->speed_motor);
        inverter_decode_double_signal(measures_message, "TempMotor_1", frame, &side->temp_motor);
        inverter_decode_double_signal(measures_message, "TempMotor_2", frame, &side->temp_motor_aux);
    } else if (frame->id == side->states_id && states_message != NULL) {
        inverter_decode_text_signal(states_message, "State_1", frame, side->state, sizeof(side->state));
        inverter_decode_text_signal(states_message, "Event_1", frame, side->event, sizeof(side->event));
        inverter_decode_double_signal(states_message, "TempIGBT_1", frame, &side->temp_igbt);
        inverter_decode_int_signal(states_message, "OverSpeed_1", frame, &side->over_speed);
        inverter_decode_int_signal(states_message, "OverDCBusVoltage_1", frame, &side->over_voltage);
        inverter_decode_int_signal(states_message, "UnderDCBusVoltage_1", frame, &side->under_voltage);
        inverter_decode_int_signal(states_message, "OverHeatingIGBT_1", frame, &side->over_temp_igbt);
        inverter_decode_int_signal(states_message, "OverHeatingMotor_1", frame, &side->over_temp_motor);
        inverter_decode_int_signal(states_message, "FOCError_1", frame, &side->foc_error);
        inverter_decode_int_signal(states_message, "RunError_1", frame, &side->run_error);
        inverter_decode_int_signal(states_message, "InitError_1", frame, &side->init_error);
        side->gd_ready_count = 0;
        for (gd_index = 1; gd_index <= 6; gd_index++) {
            char signal_name[32];
            snprintf(signal_name, sizeof(signal_name), "GD%d_rdy_1", gd_index);
            if (inverter_decode_int_signal(states_message, signal_name, frame, &value) && value != 0) {
                side->gd_ready_count++;
            }
        }
    } else if (frame->id == side->gd_id && gd_message != NULL) {
        side->gd_fault_count = 0;
        for (gd_index = 1; gd_index <= 6; gd_index++) {
            char signal_name[40];

            snprintf(signal_name, sizeof(signal_name), "GD%d_sec_not_ready_1", gd_index);
            if (inverter_decode_int_signal(gd_message, signal_name, frame, &value) && value != 0) {
                side->gd_fault_count++;
            }

            snprintf(signal_name, sizeof(signal_name), "GD%d_ovlo2_error_1", gd_index);
            if (inverter_decode_int_signal(gd_message, signal_name, frame, &value) && value != 0) {
                side->gd_fault_count++;
            }

            snprintf(signal_name, sizeof(signal_name), "GD%d_uvlo2_error_1", gd_index);
            if (inverter_decode_int_signal(gd_message, signal_name, frame, &value) && value != 0) {
                side->gd_fault_count++;
            }

            snprintf(signal_name, sizeof(signal_name), "GD%d_ocp_error_1", gd_index);
            if (inverter_decode_int_signal(gd_message, signal_name, frame, &value) && value != 0) {
                side->gd_fault_count++;
            }

            snprintf(signal_name, sizeof(signal_name), "GD%d_desat_error_1", gd_index);
            if (inverter_decode_int_signal(gd_message, signal_name, frame, &value) && value != 0) {
                side->gd_fault_count++;
            }
        }
    } else {
        return 0;
    }

    side->online = 1;
    side->last_update = frame->timestamp;
    return 1;
}

/**
 * @brief Formata um lado do dashboard como JSON.
 *
 * @param side Snapshot do lado.
 * @param title Titulo amigavel do lado.
 * @param buffer Buffer de destino.
 * @param buffer_size Tamanho do buffer de destino.
 */
static void inverter_build_side_json(const InverterDashboardSide *side, const char *title, char *buffer, size_t buffer_size)
{
    char timestamp[32] = "";
    char state_text[64];
    char event_text[96];
    char control_mode[64];
    char direction[64];

    if (side->online) {
        snprintf(timestamp, sizeof(timestamp), "%02u:%02u:%02u.%03u",
                 (unsigned int)side->last_update.wHour,
                 (unsigned int)side->last_update.wMinute,
                 (unsigned int)side->last_update.wSecond,
                 (unsigned int)side->last_update.wMilliseconds);
    }

    inverter_json_escape(side->state, state_text, sizeof(state_text));
    inverter_json_escape(side->event, event_text, sizeof(event_text));
    inverter_json_escape(side->control_mode, control_mode, sizeof(control_mode));
    inverter_json_escape(side->direction, direction, sizeof(direction));

    snprintf(buffer,
             buffer_size,
             "{"
             "\"title\":\"%s\",\"configured\":%s,\"online\":%s,\"last_update\":\"%s\","
             "\"command_id\":%u,\"measures_id\":%u,\"states_id\":%u,\"gd_id\":%u,"
             "\"onoff\":%s,\"control_mode\":\"%s\",\"direction\":\"%s\",\"speed_ref\":%.3f,\"torque_ref\":%.3f,"
             "\"voltage_dc\":%.3f,\"current_ac\":%.3f,\"voltage_ac\":%.3f,\"speed_motor\":%.3f,"
             "\"temp_motor\":%.3f,\"temp_motor_aux\":%.3f,\"temp_igbt\":%.3f,"
             "\"state\":\"%s\",\"event\":\"%s\","
             "\"over_speed\":%s,\"over_voltage\":%s,\"under_voltage\":%s,\"over_temp_igbt\":%s,\"over_temp_motor\":%s,"
             "\"foc_error\":%s,\"run_error\":%s,\"init_error\":%s,"
             "\"gd_ready_count\":%d,\"gd_fault_count\":%d"
             "}",
             title,
             side->configured ? "true" : "false",
             side->online ? "true" : "false",
             timestamp,
             side->command_id,
             side->measures_id,
             side->states_id,
             side->gd_id,
             side->onoff ? "true" : "false",
             control_mode,
             direction,
             side->speed_ref,
             side->torque_ref,
             side->voltage_dc,
             side->current_ac,
             side->voltage_ac,
             side->speed_motor,
             side->temp_motor,
             side->temp_motor_aux,
             side->temp_igbt,
             state_text,
             event_text,
             side->over_speed ? "true" : "false",
             side->over_voltage ? "true" : "false",
             side->under_voltage ? "true" : "false",
             side->over_temp_igbt ? "true" : "false",
             side->over_temp_motor ? "true" : "false",
             side->foc_error ? "true" : "false",
             side->run_error ? "true" : "false",
             side->init_error ? "true" : "false",
             side->gd_ready_count,
             side->gd_fault_count);
}

void inverter_dashboard_set_default_config(InverterDashboardConfig *config)
{
    ZeroMemory(config, sizeof(*config));
}

void inverter_dashboard_reset(InverterDashboardData *data)
{
    inverter_dashboard_reset_side(&data->left);
    inverter_dashboard_reset_side(&data->right);
}

int inverter_dashboard_update_from_frame(InverterDashboardData *data,
                                         const InverterDashboardConfig *config,
                                         const DbcDatabase *database,
                                         const CanFrame *frame)
{
    if (data == NULL || config == NULL || database == NULL || frame == NULL || !database->loaded) {
        return 0;
    }

    data->left.configured = config->left_base_set;
    data->left.command_id = config->left_base_id;
    data->left.measures_id = config->left_base_id + INVERTER_MEASURES_OFFSET;
    data->left.states_id = config->left_base_id + INVERTER_STATES_OFFSET;
    data->left.gd_id = config->left_base_id + INVERTER_GD_OFFSET;

    data->right.configured = config->right_base_set;
    data->right.command_id = config->right_base_id;
    data->right.measures_id = config->right_base_id + INVERTER_MEASURES_OFFSET;
    data->right.states_id = config->right_base_id + INVERTER_STATES_OFFSET;
    data->right.gd_id = config->right_base_id + INVERTER_GD_OFFSET;

    if (inverter_update_side(&data->left, database, frame)) {
        return 1;
    }

    if (inverter_update_side(&data->right, database, frame)) {
        return 1;
    }

    return 0;
}

void inverter_dashboard_build_json(const InverterDashboardData *data,
                                   const InverterDashboardConfig *config,
                                   char *buffer,
                                   size_t buffer_size)
{
    char left_json[2048];
    char right_json[2048];

    (void)config;
    inverter_build_side_json(&data->left, "Inverter A", left_json, sizeof(left_json));
    inverter_build_side_json(&data->right, "Inverter B", right_json, sizeof(right_json));
    snprintf(buffer, buffer_size, "{\"left\":%s,\"right\":%s}", left_json, right_json);
}
