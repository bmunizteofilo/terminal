#ifndef CAN_SLCAN_H
#define CAN_SLCAN_H

#include <stdio.h>

#include "can_types.h"

/**
 * @brief Define a configuracao padrao do subsistema CAN.
 *
 * @param config Estrutura de configuracao a ser inicializada.
 */
void can_set_default_config(CanConfig *config);

/**
 * @brief Limpa todos os contadores estatisticos do CAN.
 *
 * @param stats Estrutura de estatisticas a ser reinicializada.
 */
void can_reset_stats(CanStats *stats);

/**
 * @brief Converte um texto para o enum de modo de exibicao CAN.
 *
 * @param value Texto informado pelo usuario.
 * @param view_mode Ponteiro para receber o valor convertido.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int can_parse_view_mode(const char *value, CanViewMode *view_mode);

/**
 * @brief Converte o modo de exibicao CAN para texto.
 *
 * @param view_mode Modo configurado.
 * @return const char* Texto correspondente ao modo.
 */
const char *can_view_mode_to_string(CanViewMode view_mode);

/**
 * @brief Aplica uma entrada de configuracao CAN carregada do arquivo persistente.
 *
 * @param config Configuracao alvo.
 * @param key Nome da chave.
 * @param value Valor textual associado.
 * @return int Retorna 1 quando a chave foi aplicada, caso contrario 0.
 */
int can_apply_config_entry(CanConfig *config, const char *key, const char *value);

/**
 * @brief Escreve as chaves de configuracao CAN no arquivo de persistencia.
 *
 * @param file Ponteiro do arquivo de configuracao aberto para escrita.
 * @param config Configuracao CAN a ser persistida.
 */
void can_save_config(FILE *file, const CanConfig *config);

/**
 * @brief Abre a porta COM do adaptador CAN e aplica a configuracao SLCAN inicial.
 *
 * @param state Estado global do backend CAN.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int can_slcan_open(CanState *state);

/**
 * @brief Fecha a comunicacao CAN e libera a porta COM associada.
 *
 * @param state Estado global do backend CAN.
 */
void can_slcan_close(CanState *state);

/**
 * @brief Envia o comando de abertura do barramento SLCAN.
 *
 * @param state Estado global do backend CAN.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int can_slcan_start(CanState *state);

/**
 * @brief Envia o comando de parada do barramento SLCAN.
 *
 * @param state Estado global do backend CAN.
 */
void can_slcan_stop(CanState *state);

/**
 * @brief Envia um frame CAN padrao de 11 bits pelo backend SLCAN.
 *
 * @param state Estado global do backend CAN.
 * @param can_id Identificador CAN padrao.
 * @param data Payload a ser transmitido.
 * @param dlc Quantidade de bytes do payload.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int can_slcan_send_standard(CanState *state, unsigned int can_id, const unsigned char *data, unsigned char dlc);

/**
 * @brief Interpreta uma linha SLCAN contendo um frame CAN classico.
 *
 * @param line Linha ASCII recebida do adaptador.
 * @param frame Estrutura que recebera o frame interpretado.
 * @return int Retorna 1 quando a linha representa um frame valido, caso contrario 0.
 */
int can_slcan_parse_frame(const char *line, CanFrame *frame);

#endif
