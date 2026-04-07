#ifndef DBC_PARSER_H
#define DBC_PARSER_H

#include "dbc_types.h"

/**
 * @brief Reinicializa o banco DBC para o estado vazio.
 *
 * @param database Banco DBC a ser reinicializado.
 */
void dbc_reset_database(DbcDatabase *database);

/**
 * @brief Carrega um subconjunto do formato DBC a partir de um arquivo em disco.
 *
 * Suporta nesta V1 as diretivas `VERSION`, `BO_`, `SG_` e `VAL_`.
 *
 * @param path Caminho do arquivo DBC.
 * @param database Banco DBC que recebera os dados parseados.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int dbc_load_file(const char *path, DbcDatabase *database);

/**
 * @brief Localiza uma mensagem pelo identificador CAN dentro do banco DBC.
 *
 * @param database Banco DBC carregado.
 * @param id Identificador CAN procurado.
 * @return const DbcMessage* Ponteiro para a mensagem encontrada ou NULL.
 */
const DbcMessage *dbc_find_message_by_id(const DbcDatabase *database, unsigned int id);

/**
 * @brief Localiza uma mensagem pelo nome simbolico dentro do banco DBC.
 *
 * @param database Banco DBC carregado.
 * @param name Nome da mensagem procurada.
 * @return const DbcMessage* Ponteiro para a mensagem encontrada ou NULL.
 */
const DbcMessage *dbc_find_message_by_name(const DbcDatabase *database, const char *name);

/**
 * @brief Localiza um sinal pelo nome dentro de uma mensagem DBC.
 *
 * @param message Mensagem DBC onde a busca sera realizada.
 * @param name Nome do sinal procurado.
 * @return const DbcSignal* Ponteiro para o sinal encontrado ou NULL.
 */
const DbcSignal *dbc_find_signal_by_name(const DbcMessage *message, const char *name);

/**
 * @brief Retorna o texto associado a um valor enumerado do sinal, quando existir.
 *
 * @param signal Sinal DBC consultado.
 * @param value Valor bruto a ser pesquisado.
 * @return const char* Texto do enum ou NULL quando nao houver correspondencia.
 */
const char *dbc_find_value_text(const DbcSignal *signal, int value);

/**
 * @brief Extrai e decodifica um sinal little-endian a partir do payload do frame.
 *
 * Esta V1 do decoder suporta sinais little-endian definidos no DBC.
 *
 * @param signal Sinal DBC a ser decodificado.
 * @param data Payload bruto do frame CAN.
 * @param data_length Quantidade de bytes validos no payload.
 * @param raw_value Ponteiro opcional para receber o valor bruto extraido.
 * @param physical_value Ponteiro opcional para receber o valor com fator e offset.
 * @return int Retorna 1 em caso de sucesso, caso contrario 0.
 */
int dbc_decode_signal(const DbcSignal *signal,
                      const unsigned char *data,
                      unsigned int data_length,
                      long long *raw_value,
                      double *physical_value);

#endif
