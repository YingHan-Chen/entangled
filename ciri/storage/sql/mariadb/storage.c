/*
 * Copyright (c) 2019 IOTA Stiftung
 * https://github.com/iotaledger/entangled
 *
 * Refer to the LICENSE file for licensing information
 */

#include <mysql.h>

#include "ciri/storage/sql/mariadb/connection.h"
#include "ciri/storage/sql/mariadb/wrappers.h"
#include "ciri/storage/storage.h"
#include "common/model/milestone.h"
#include "common/model/transaction.h"
#include "utils/logger_helper.h"
#include "utils/macros.h"
#include "utils/time.h"

#define MARIADB_LOGGER_ID "mariadb"

static logger_id_t logger_id;

/**
 * Private functions
 */

static void log_statement_error(MYSQL_STMT* const mariadb_statement) {
  log_error(logger_id, "Statement error with code: %d, state: %s and message: \"%s\"\n",
            mysql_stmt_errno(mariadb_statement), mysql_stmt_sqlstate(mariadb_statement),
            mysql_stmt_error(mariadb_statement));
}

static retcode_t mysql_stmt_bind_and_store_result(MYSQL_STMT* const mariadb_statement, MYSQL_BIND* const bind) {
  if (mysql_stmt_bind_result(mariadb_statement, bind) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  if (mysql_stmt_store_result(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_STORE_RESULT;
  }

  return RC_OK;
}

static retcode_t mysql_stmt_bind_param_and_execute(MYSQL_STMT* const mariadb_statement, MYSQL_BIND* const bind) {
  if (mysql_stmt_bind_param(mariadb_statement, bind) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  if (mysql_stmt_execute(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_STORE_RESULT;
  }

  return RC_OK;
}

static retcode_t execute_statement_one_result(MYSQL_STMT* const mariadb_statement, void* const buffer,
                                              enum enum_field_types const buffer_type) {
  MYSQL_BIND bind[1];

  memset(bind, 0, sizeof(bind));

  if (mysql_stmt_execute(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_EXECUTE;
  }

  bind[0].buffer = (char*)buffer;
  bind[0].buffer_type = buffer_type;

  if (mysql_stmt_bind_and_store_result(mariadb_statement, bind) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  if (mysql_stmt_fetch(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_STORE_RESULT;
  }

  return RC_OK;
}

static inline retcode_t execute_statement_exist(MYSQL_STMT* const mariadb_statement, bool* const exist) {
  return execute_statement_one_result(mariadb_statement, exist, MYSQL_TYPE_TINY);
}

static inline retcode_t execute_statement_count(MYSQL_STMT* const mariadb_statement, uint64_t* const count) {
  return execute_statement_one_result(mariadb_statement, count, MYSQL_TYPE_LONGLONG);
}

static void storage_transaction_load_bind_essence(MYSQL_BIND* const bind, iota_transaction_t* const transaction,
                                                  size_t* const index) {
  bind[*index].buffer = (char*)transaction->essence.address;
  bind[*index].buffer_type = MYSQL_TYPE_BLOB;
  bind[*index].buffer_length = FLEX_TRIT_SIZE_243;
  transaction->loaded_columns_mask.essence |= MASK_ESSENCE_ADDRESS;
  (*index)++;

  bind[*index].buffer = (char*)&transaction->essence.value;
  bind[*index].buffer_type = MYSQL_TYPE_LONGLONG;
  transaction->loaded_columns_mask.essence |= MASK_ESSENCE_VALUE;
  (*index)++;

  bind[*index].buffer = (char*)transaction->essence.obsolete_tag;
  bind[*index].buffer_type = MYSQL_TYPE_BLOB;
  bind[*index].buffer_length = FLEX_TRIT_SIZE_81;
  transaction->loaded_columns_mask.essence |= MASK_ESSENCE_OBSOLETE_TAG;
  (*index)++;

  bind[*index].buffer = (char*)&transaction->essence.timestamp;
  bind[*index].buffer_type = MYSQL_TYPE_LONGLONG;
  transaction->loaded_columns_mask.essence |= MASK_ESSENCE_TIMESTAMP;
  (*index)++;

  bind[*index].buffer = (char*)&transaction->essence.current_index;
  bind[*index].buffer_type = MYSQL_TYPE_LONGLONG;
  transaction->loaded_columns_mask.essence |= MASK_ESSENCE_CURRENT_INDEX;
  (*index)++;

  bind[*index].buffer = (char*)&transaction->essence.last_index;
  bind[*index].buffer_type = MYSQL_TYPE_LONGLONG;
  transaction->loaded_columns_mask.essence |= MASK_ESSENCE_LAST_INDEX;
  (*index)++;

  bind[*index].buffer = (char*)transaction->essence.bundle;
  bind[*index].buffer_type = MYSQL_TYPE_BLOB;
  bind[*index].buffer_length = FLEX_TRIT_SIZE_243;
  transaction->loaded_columns_mask.essence |= MASK_ESSENCE_BUNDLE;
  (*index)++;
}

static void storage_transaction_load_bind_attachment(MYSQL_BIND* const bind, iota_transaction_t* const transaction,
                                                     size_t* const index) {
  bind[*index].buffer = (char*)transaction->attachment.trunk;
  bind[*index].buffer_type = MYSQL_TYPE_BLOB;
  bind[*index].buffer_length = FLEX_TRIT_SIZE_243;
  transaction->loaded_columns_mask.attachment |= MASK_ATTACHMENT_TRUNK;
  (*index)++;

  bind[*index].buffer = (char*)transaction->attachment.branch;
  bind[*index].buffer_type = MYSQL_TYPE_BLOB;
  bind[*index].buffer_length = FLEX_TRIT_SIZE_243;
  transaction->loaded_columns_mask.attachment |= MASK_ATTACHMENT_BRANCH;
  (*index)++;

  bind[*index].buffer = (char*)&transaction->attachment.attachment_timestamp;
  bind[*index].buffer_type = MYSQL_TYPE_LONGLONG;
  transaction->loaded_columns_mask.attachment |= MASK_ATTACHMENT_TIMESTAMP;
  (*index)++;

  bind[*index].buffer = (char*)&transaction->attachment.attachment_timestamp_lower;
  bind[*index].buffer_type = MYSQL_TYPE_LONGLONG;
  transaction->loaded_columns_mask.attachment |= MASK_ATTACHMENT_TIMESTAMP_LOWER;
  (*index)++;

  bind[*index].buffer = (char*)&transaction->attachment.attachment_timestamp_upper;
  bind[*index].buffer_type = MYSQL_TYPE_LONGLONG;
  transaction->loaded_columns_mask.attachment |= MASK_ATTACHMENT_TIMESTAMP_UPPER;
  (*index)++;

  bind[*index].buffer = (char*)transaction->attachment.nonce;
  bind[*index].buffer_type = MYSQL_TYPE_BLOB;
  bind[*index].buffer_length = FLEX_TRIT_SIZE_81;
  transaction->loaded_columns_mask.attachment |= MASK_ATTACHMENT_NONCE;
  (*index)++;

  bind[*index].buffer = (char*)transaction->attachment.tag;
  bind[*index].buffer_type = MYSQL_TYPE_BLOB;
  bind[*index].buffer_length = FLEX_TRIT_SIZE_81;
  transaction->loaded_columns_mask.attachment |= MASK_ATTACHMENT_TAG;
  (*index)++;
}

static void storage_transaction_load_bind_consensus(MYSQL_BIND* const bind, iota_transaction_t* const transaction,
                                                    size_t* const index) {
  bind[*index].buffer = (char*)transaction->consensus.hash;
  bind[*index].buffer_type = MYSQL_TYPE_BLOB;
  bind[*index].buffer_length = FLEX_TRIT_SIZE_243;
  transaction->loaded_columns_mask.consensus |= MASK_CONSENSUS_HASH;
  (*index)++;
}

static void storage_transaction_load_bind_data(MYSQL_BIND* const bind, iota_transaction_t* const transaction,
                                               size_t* const index) {
  bind[*index].buffer = (char*)transaction->data.signature_or_message;
  bind[*index].buffer_type = MYSQL_TYPE_BLOB;
  bind[*index].buffer_length = FLEX_TRIT_SIZE_6561;
  transaction->loaded_columns_mask.data |= MASK_DATA_SIG_OR_MSG;
  (*index)++;
}

static void storage_transaction_load_bind_metadata(MYSQL_BIND* const bind, iota_transaction_t* const transaction,
                                                   size_t* const index) {
  bind[*index].buffer = (char*)&transaction->metadata.snapshot_index;
  bind[*index].buffer_type = MYSQL_TYPE_LONGLONG;
  transaction->loaded_columns_mask.metadata |= MASK_METADATA_SNAPSHOT_INDEX;
  (*index)++;

  bind[*index].buffer = (char*)&transaction->metadata.solid;
  bind[*index].buffer_type = MYSQL_TYPE_TINY;
  transaction->loaded_columns_mask.metadata |= MASK_METADATA_SOLID;
  (*index)++;

  bind[*index].buffer = (char*)&transaction->metadata.validity;
  bind[*index].buffer_type = MYSQL_TYPE_TINY;
  transaction->loaded_columns_mask.metadata |= MASK_METADATA_VALIDITY;
  (*index)++;

  bind[*index].buffer = (char*)&transaction->metadata.arrival_timestamp;
  bind[*index].buffer_type = MYSQL_TYPE_LONGLONG;
  transaction->loaded_columns_mask.metadata |= MASK_METADATA_ARRIVAL_TIMESTAMP;
  (*index)++;
}

static retcode_t storage_transaction_load_generic(MYSQL_STMT* const mariadb_statement, MYSQL_BIND* const bind_out,
                                                  storage_load_model_t const model, flex_trit_t const* const hash,
                                                  iota_stor_pack_t* const pack) {
  size_t index = 0;
  iota_transaction_t* transaction = *((iota_transaction_t**)pack->models);
  MYSQL_BIND bind_in[1];

  memset(bind_in, 0, sizeof(bind_in));

  column_compress_bind(bind_in, 0, hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);

  if (mysql_stmt_bind_param_and_execute(mariadb_statement, bind_in) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  transaction_reset(transaction);

  if (model == MODEL_TRANSACTION) {
    storage_transaction_load_bind_essence(bind_out, transaction, &index);
    storage_transaction_load_bind_attachment(bind_out, transaction, &index);
    storage_transaction_load_bind_consensus(bind_out, transaction, &index);
    storage_transaction_load_bind_data(bind_out, transaction, &index);
  } else if (model == MODEL_TRANSACTION_ESSENCE_METADATA) {
    storage_transaction_load_bind_essence(bind_out, transaction, &index);
    storage_transaction_load_bind_metadata(bind_out, transaction, &index);
  } else if (model == MODEL_TRANSACTION_ESSENCE_ATTACHMENT_METADATA) {
    storage_transaction_load_bind_essence(bind_out, transaction, &index);
    storage_transaction_load_bind_attachment(bind_out, transaction, &index);
    storage_transaction_load_bind_metadata(bind_out, transaction, &index);
  } else if (model == MODEL_TRANSACTION_ESSENCE_CONSENSUS) {
    storage_transaction_load_bind_essence(bind_out, transaction, &index);
    storage_transaction_load_bind_consensus(bind_out, transaction, &index);
  } else if (model == MODEL_TRANSACTION_METADATA) {
    storage_transaction_load_bind_metadata(bind_out, transaction, &index);
  } else {
    return RC_STORAGE_FAILED_NOT_IMPLEMENTED;
  }

  if (mysql_stmt_bind_and_store_result(mariadb_statement, bind_out) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  pack->num_loaded = mysql_stmt_num_rows(mariadb_statement);
  pack->insufficient_capacity = pack->num_loaded > pack->capacity;

  if (pack->num_loaded == 0 || pack->insufficient_capacity == true) {
    return RC_OK;
  }

  if (mysql_stmt_fetch(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_STORE_RESULT;
  }

  return RC_OK;
}

static retcode_t storage_milestone_load_generic(MYSQL_STMT* const mariadb_statement, MYSQL_BIND* const bind_out,
                                                iota_stor_pack_t* const pack) {
  iota_milestone_t* milestone = *((iota_milestone_t**)pack->models);

  if (mysql_stmt_execute(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_EXECUTE;
  }

  milestone_reset(milestone);

  bind_out[0].buffer = (char*)&milestone->index;
  bind_out[0].buffer_type = MYSQL_TYPE_LONGLONG;

  bind_out[1].buffer = (char*)milestone->hash;
  bind_out[1].buffer_type = MYSQL_TYPE_BLOB;
  bind_out[1].buffer_length = FLEX_TRIT_SIZE_243;

  if (mysql_stmt_bind_and_store_result(mariadb_statement, bind_out) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  pack->num_loaded = mysql_stmt_num_rows(mariadb_statement);
  pack->insufficient_capacity = pack->num_loaded > pack->capacity;

  if (pack->num_loaded == 0 || pack->insufficient_capacity == true) {
    return RC_OK;
  }

  if (mysql_stmt_fetch(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_STORE_RESULT;
  }

  return RC_OK;
}

static retcode_t storage_transaction_update_generic(MYSQL_STMT* const mariadb_statement, MYSQL_BIND* const bind,
                                                    flex_trit_t const* const hash) {
  column_compress_bind(bind, 1, hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);

  if (mysql_stmt_bind_param_and_execute(mariadb_statement, bind) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  return RC_OK;
}

static retcode_t storage_hashes_load_generic(MYSQL_STMT* const mariadb_statement, iota_stor_pack_t* const pack) {
  size_t i = 0;
  size_t length = 0;
  MYSQL_BIND bind[1];
  flex_trit_t hash[FLEX_TRIT_SIZE_243];

  memset(hash, FLEX_TRIT_NULL_VALUE, FLEX_TRIT_SIZE_243);

  if (mysql_stmt_execute(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_EXECUTE;
  }

  memset(bind, 0, sizeof(bind));

  bind[0].buffer = (char*)hash;
  bind[0].buffer_type = MYSQL_TYPE_BLOB;
  bind[0].buffer_length = FLEX_TRIT_SIZE_243;
  bind[0].length = &length;

  if (mysql_stmt_bind_and_store_result(mariadb_statement, bind) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  pack->num_loaded = mysql_stmt_num_rows(mariadb_statement);
  pack->insufficient_capacity = pack->num_loaded > pack->capacity;

  if (pack->num_loaded == 0 || pack->insufficient_capacity == true) {
    return RC_OK;
  }

  while (mysql_stmt_fetch(mariadb_statement) == 0) {
    memcpy(pack->models[i], hash, FLEX_TRIT_SIZE_243);
    memset(hash, FLEX_TRIT_NULL_VALUE, FLEX_TRIT_SIZE_243);
    i++;
  }

  return RC_OK;
}

/**
 * Public functions
 */

retcode_t storage_init() {
  logger_id = logger_helper_enable(MARIADB_LOGGER_ID, LOGGER_DEBUG, true);

  if (mysql_library_init(0, NULL, NULL) != 0) {
    return RC_STORAGE_FAILED_INIT;
  }

  return RC_OK;
}

retcode_t storage_destroy() {
  logger_helper_release(logger_id);

  mysql_library_end();

  return RC_OK;
}

retcode_t storage_transaction_count(storage_connection_t const* const connection, uint64_t* const count) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_count;

  return execute_statement_count(mariadb_statement, count);
}

retcode_t storage_transaction_store(storage_connection_t const* const connection,
                                    iota_transaction_t const* const transaction) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_insert;
  MYSQL_BIND bind[17];
  uint64_t ts = current_timestamp_ms();

  memset(bind, 0, sizeof(bind));

  column_compress_bind(bind, 0, transaction->data.signature_or_message, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_6561);
  column_compress_bind(bind, 1, transaction->essence.address, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
  column_compress_bind(bind, 2, &transaction->essence.value, MYSQL_TYPE_LONGLONG, -1);
  column_compress_bind(bind, 3, transaction->essence.obsolete_tag, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_81);
  column_compress_bind(bind, 4, &transaction->essence.timestamp, MYSQL_TYPE_LONGLONG, -1);
  column_compress_bind(bind, 5, &transaction->essence.current_index, MYSQL_TYPE_LONGLONG, -1);
  column_compress_bind(bind, 6, &transaction->essence.last_index, MYSQL_TYPE_LONGLONG, -1);
  column_compress_bind(bind, 7, transaction->essence.bundle, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
  column_compress_bind(bind, 8, transaction->attachment.trunk, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
  column_compress_bind(bind, 9, transaction->attachment.branch, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
  column_compress_bind(bind, 10, transaction->attachment.tag, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_81);
  column_compress_bind(bind, 11, &transaction->attachment.attachment_timestamp, MYSQL_TYPE_LONGLONG, -1);
  column_compress_bind(bind, 12, &transaction->attachment.attachment_timestamp_upper, MYSQL_TYPE_LONGLONG, -1);
  column_compress_bind(bind, 13, &transaction->attachment.attachment_timestamp_lower, MYSQL_TYPE_LONGLONG, -1);
  column_compress_bind(bind, 14, transaction->attachment.nonce, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_81);
  column_compress_bind(bind, 15, transaction->consensus.hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
  column_compress_bind(bind, 16, &ts, MYSQL_TYPE_LONGLONG, -1);

  if (mysql_stmt_bind_param_and_execute(mariadb_statement, bind) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  return RC_OK;
}

retcode_t storage_transaction_load(storage_connection_t const* const connection,
                                   storage_transaction_field_t const field, flex_trit_t const* const key,
                                   iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_select_by_hash;
  MYSQL_BIND bind_out[16];
  UNUSED(field);

  memset(bind_out, 0, sizeof(bind_out));

  return storage_transaction_load_generic(mariadb_statement, bind_out, MODEL_TRANSACTION, key, pack);
}

retcode_t storage_transaction_load_essence_metadata(storage_connection_t const* const connection,
                                                    flex_trit_t const* const hash, iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_select_essence_metadata;
  MYSQL_BIND bind_out[11];

  memset(bind_out, 0, sizeof(bind_out));

  return storage_transaction_load_generic(mariadb_statement, bind_out, MODEL_TRANSACTION_ESSENCE_METADATA, hash, pack);
}

retcode_t storage_transaction_load_essence_attachment_metadata(storage_connection_t const* const connection,
                                                               flex_trit_t const* const hash,
                                                               iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_select_essence_attachment_metadata;
  MYSQL_BIND bind_out[18];

  memset(bind_out, 0, sizeof(bind_out));

  return storage_transaction_load_generic(mariadb_statement, bind_out, MODEL_TRANSACTION_ESSENCE_ATTACHMENT_METADATA,
                                          hash, pack);
}

retcode_t storage_transaction_load_essence_consensus(storage_connection_t const* const connection,
                                                     flex_trit_t const* const hash, iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_select_essence_consensus;
  MYSQL_BIND bind_out[8];

  memset(bind_out, 0, sizeof(bind_out));

  return storage_transaction_load_generic(mariadb_statement, bind_out, MODEL_TRANSACTION_ESSENCE_CONSENSUS, hash, pack);
}

retcode_t storage_transaction_load_metadata(storage_connection_t const* const connection, flex_trit_t const* const hash,
                                            iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_select_metadata;
  MYSQL_BIND bind_out[4];

  memset(bind_out, 0, sizeof(bind_out));

  return storage_transaction_load_generic(mariadb_statement, bind_out, MODEL_TRANSACTION_METADATA, hash, pack);
}

retcode_t storage_transaction_exist(storage_connection_t const* const connection,
                                    storage_transaction_field_t const field, flex_trit_t const* const key,
                                    bool* const exist) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = NULL;
  MYSQL_BIND bind[1];
  size_t num_bytes_key;

  switch (field) {
    case TRANSACTION_FIELD_NONE:
      mariadb_statement = mariadb_connection->statements.transaction_exist;
      break;
    case TRANSACTION_FIELD_HASH:
      mariadb_statement = mariadb_connection->statements.transaction_exist_by_hash;
      num_bytes_key = FLEX_TRIT_SIZE_243;
      break;
    default:
      return RC_STORAGE_FAILED_NOT_IMPLEMENTED;
  }

  if (field != TRANSACTION_FIELD_NONE && key) {
    memset(bind, 0, sizeof(bind));
    column_compress_bind(bind, 0, key, MYSQL_TYPE_BLOB, num_bytes_key);
    if (mysql_stmt_bind_param(mariadb_statement, bind) != 0) {
      log_statement_error(mariadb_statement);
      return RC_STORAGE_FAILED_BINDING;
    }
  }

  return execute_statement_exist(mariadb_statement, exist);
}

retcode_t storage_transaction_update_snapshot_index(storage_connection_t const* const connection,
                                                    flex_trit_t const* const hash, uint64_t const snapshot_index) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_update_snapshot_index;
  MYSQL_BIND bind[2];

  memset(bind, 0, sizeof(bind));

  column_compress_bind(bind, 0, &snapshot_index, MYSQL_TYPE_LONGLONG, -1);

  return storage_transaction_update_generic(mariadb_statement, bind, hash);
}

retcode_t storage_transaction_update_solidity(storage_connection_t const* const connection,
                                              flex_trit_t const* const hash, bool const is_solid) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_update_solidity;
  MYSQL_BIND bind[2];

  memset(bind, 0, sizeof(bind));

  column_compress_bind(bind, 0, &is_solid, MYSQL_TYPE_TINY, -1);

  return storage_transaction_update_generic(mariadb_statement, bind, hash);
}

retcode_t storage_transaction_update_validity(storage_connection_t const* const connection,
                                              flex_trit_t const* const hash, bundle_status_t const validity) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_update_validity;
  MYSQL_BIND bind[2];

  memset(bind, 0, sizeof(bind));

  column_compress_bind(bind, 0, &validity, MYSQL_TYPE_TINY, -1);

  return storage_transaction_update_generic(mariadb_statement, bind, hash);
}

retcode_t storage_transaction_load_hashes(storage_connection_t const* const connection,
                                          storage_transaction_field_t const field, flex_trit_t const* const key,
                                          iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = NULL;
  size_t num_bytes_key;
  MYSQL_BIND bind[1];

  memset(bind, 0, sizeof(bind));

  switch (field) {
    case TRANSACTION_FIELD_ADDRESS:
      mariadb_statement = mariadb_connection->statements.transaction_select_hashes_by_address;
      num_bytes_key = FLEX_TRIT_SIZE_243;
      break;
    default:
      return RC_STORAGE_FAILED_NOT_IMPLEMENTED;
  }

  column_compress_bind(bind, 0, key, MYSQL_TYPE_BLOB, num_bytes_key);

  if (mysql_stmt_bind_param(mariadb_statement, bind) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  return storage_hashes_load_generic(mariadb_statement, pack);
}

retcode_t storage_transaction_load_hashes_of_approvers(storage_connection_t const* const connection,
                                                       flex_trit_t const* const approvee_hash,
                                                       iota_stor_pack_t* const pack, uint64_t before_timestamp) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement =
      before_timestamp != 0 ? mariadb_connection->statements.transaction_select_hashes_of_approvers_before_date
                            : mariadb_connection->statements.transaction_select_hashes_of_approvers;
  MYSQL_BIND bind[3];

  memset(bind, 0, sizeof(bind));

  column_compress_bind(bind, 0, approvee_hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
  column_compress_bind(bind, 1, approvee_hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);

  if (before_timestamp != 0) {
    column_compress_bind(bind, 2, &before_timestamp, MYSQL_TYPE_LONGLONG, -1);
  }

  if (mysql_stmt_bind_param(mariadb_statement, bind) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  return storage_hashes_load_generic(mariadb_statement, pack);
}

retcode_t storage_transaction_load_hashes_of_milestone_candidates(storage_connection_t const* const connection,
                                                                  flex_trit_t const* const coordinator,
                                                                  iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_select_hashes_of_milestone_candidates;
  MYSQL_BIND bind[1];

  memset(bind, 0, sizeof(bind));

  column_compress_bind(bind, 0, coordinator, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);

  if (mysql_stmt_bind_param(mariadb_statement, bind) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  return storage_hashes_load_generic(mariadb_statement, pack);
}

retcode_t storage_transaction_approvers_count(storage_connection_t const* const connection,
                                              flex_trit_t const* const hash, uint64_t* const count) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_approvers_count;
  MYSQL_BIND bind[2];

  memset(bind, 0, sizeof(bind));

  column_compress_bind(bind, 0, hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
  column_compress_bind(bind, 1, hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);

  if (mysql_stmt_bind_param(mariadb_statement, bind) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  return execute_statement_count(mariadb_statement, count);
}

retcode_t storage_transaction_find(storage_connection_t const* const connection, hash243_queue_t const bundles,
                                   hash243_queue_t const addresses, hash81_queue_t const tags,
                                   hash243_queue_t const approvees, iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  retcode_t ret = RC_OK;
  MYSQL_STMT* mariadb_statement = NULL;
  size_t bundles_count = hash243_queue_count(bundles);
  size_t addresses_count = hash243_queue_count(addresses);
  size_t tags_count = hash81_queue_count(tags);
  size_t approvees_count = hash243_queue_count(approvees);
  hash243_queue_entry_t* iter243 = NULL;
  hash81_queue_entry_t* iter81 = NULL;
  size_t column = 0;
  char* statement =
      storage_statement_transaction_find_build(bundles_count, addresses_count, tags_count, approvees_count);
  MYSQL_BIND bind[50];

  memset(bind, 0, sizeof(bind));

  if ((ret = prepare_statement((MYSQL*)&mariadb_connection->db, &mariadb_statement, statement)) != RC_OK) {
    goto done;
  }

  bool if_bundles = !bundles_count;
  column_compress_bind(bind, column++, &if_bundles, MYSQL_TYPE_TINY, -1);

  CDL_FOREACH(bundles, iter243) {
    column_compress_bind(bind, column++, iter243->hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
  }

  bool if_addresses = !addresses_count;
  column_compress_bind(bind, column++, &if_addresses, MYSQL_TYPE_TINY, -1);

  CDL_FOREACH(addresses, iter243) {
    column_compress_bind(bind, column++, iter243->hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
  }

  bool if_tags = !tags_count;
  column_compress_bind(bind, column++, &if_tags, MYSQL_TYPE_TINY, -1);

  CDL_FOREACH(tags, iter81) { column_compress_bind(bind, column++, iter81->hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_81); }

  bool if_approvees = !approvees_count;
  column_compress_bind(bind, column++, &if_approvees, MYSQL_TYPE_TINY, -1);

  CDL_FOREACH(approvees, iter243) {
    column_compress_bind(bind, column, iter243->hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
    column_compress_bind(bind, column + approvees_count, iter243->hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
    column++;
  }

  if (mysql_stmt_bind_param(mariadb_statement, bind) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  ret = storage_hashes_load_generic(mariadb_statement, pack);

done:
  finalize_statement(mariadb_statement);
  free(statement);

  return ret;
}

retcode_t storage_transaction_delete(storage_connection_t const* const connection, flex_trit_t const* const hash) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_delete;
  MYSQL_BIND bind[1];

  memset(bind, 0, sizeof(bind));

  column_compress_bind(bind, 0, hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);

  if (mysql_stmt_bind_param_and_execute(mariadb_statement, bind) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  return RC_OK;
}

retcode_t storage_transactions_metadata_clear(storage_connection_t const* const connection) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.transaction_metadata_clear;

  if (mysql_stmt_execute(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_EXECUTE;
  }

  return RC_OK;
}

retcode_t storage_transactions_update_snapshot_index(storage_connection_t const* const connection,
                                                     hash243_set_t const hashes, uint64_t const snapshot_index) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  hash243_set_entry_t *iter = NULL, *tmp = NULL;
  retcode_t ret = RC_OK;

  if ((ret = start_transaction((MYSQL*)&mariadb_connection->db)) != RC_OK) {
    return ret;
  }

  HASH_SET_ITER(hashes, iter, tmp) {
    if ((ret = storage_transaction_update_snapshot_index(connection, iter->hash, snapshot_index)) != RC_OK) {
      break;
    }
  }

  return end_transaction((MYSQL*)&mariadb_connection->db, ret);
}

retcode_t storage_transactions_update_solidity(storage_connection_t const* const connection, hash243_set_t const hashes,
                                               bool const is_solid) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  hash243_set_entry_t *iter = NULL, *tmp = NULL;
  retcode_t ret = RC_OK;

  if ((ret = start_transaction((MYSQL*)&mariadb_connection->db)) != RC_OK) {
    return ret;
  }

  HASH_SET_ITER(hashes, iter, tmp) {
    if ((ret = storage_transaction_update_solidity(connection, iter->hash, is_solid)) != RC_OK) {
      break;
    }
  }

  return end_transaction((MYSQL*)&mariadb_connection->db, ret);
}

retcode_t storage_transactions_delete(storage_connection_t const* const connection, hash243_set_t const hashes) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  hash243_set_entry_t *iter = NULL, *tmp = NULL;
  retcode_t ret = RC_OK;

  if ((ret = start_transaction((MYSQL*)&mariadb_connection->db)) != RC_OK) {
    return ret;
  }

  HASH_SET_ITER(hashes, iter, tmp) {
    if ((ret = storage_transaction_delete(connection, iter->hash)) != RC_OK) {
      break;
    }
  }

  return end_transaction((MYSQL*)&mariadb_connection->db, ret);
}

retcode_t storage_bundle_update_validity(storage_connection_t const* const connection,
                                         bundle_transactions_t const* const bundle, bundle_status_t const status) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  retcode_t ret = RC_OK;
  iota_transaction_t* tx = NULL;

  if ((ret = start_transaction((MYSQL*)&mariadb_connection->db)) != RC_OK) {
    return ret;
  }

  BUNDLE_FOREACH(bundle, tx) {
    if ((ret = storage_transaction_update_validity(connection, transaction_hash(tx), status)) != RC_OK) {
      break;
    }
  }

  return end_transaction((MYSQL*)&mariadb_connection->db, ret);
}

retcode_t storage_milestone_clear(storage_connection_t const* const connection) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.milestone_clear;

  if (mysql_stmt_execute(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_EXECUTE;
  }

  return RC_OK;
}

retcode_t storage_milestone_store(storage_connection_t const* const connection,
                                  iota_milestone_t const* const milestone) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.milestone_insert;
  MYSQL_BIND bind[2];

  memset(bind, 0, sizeof(bind));

  column_compress_bind(bind, 0, &milestone->index, MYSQL_TYPE_LONGLONG, -1);
  column_compress_bind(bind, 1, milestone->hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);

  if (mysql_stmt_bind_param_and_execute(mariadb_statement, bind) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  return RC_OK;
}

retcode_t storage_milestone_load(storage_connection_t const* const connection, flex_trit_t const* const hash,
                                 iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.milestone_select_by_hash;
  MYSQL_BIND bind_in[1];
  MYSQL_BIND bind_out[2];

  memset(bind_in, 0, sizeof(bind_in));
  memset(bind_out, 0, sizeof(bind_out));

  column_compress_bind(bind_in, 0, hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);

  if (mysql_stmt_bind_param(mariadb_statement, bind_in) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  return storage_milestone_load_generic(mariadb_statement, bind_out, pack);
}

retcode_t storage_milestone_load_last(storage_connection_t const* const connection, iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.milestone_select_last;
  MYSQL_BIND bind_out[2];

  memset(bind_out, 0, sizeof(bind_out));

  return storage_milestone_load_generic(mariadb_statement, bind_out, pack);
}

retcode_t storage_milestone_load_first(storage_connection_t const* const connection, iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.milestone_select_first;
  MYSQL_BIND bind_out[2];

  memset(bind_out, 0, sizeof(bind_out));

  return storage_milestone_load_generic(mariadb_statement, bind_out, pack);
}

retcode_t storage_milestone_load_by_index(storage_connection_t const* const connection, uint64_t const index,
                                          iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.milestone_select_by_index;
  MYSQL_BIND bind_in[1];
  MYSQL_BIND bind_out[2];

  memset(bind_in, 0, sizeof(bind_in));
  memset(bind_out, 0, sizeof(bind_out));

  column_compress_bind(bind_in, 0, &index, MYSQL_TYPE_LONGLONG, -1);

  if (mysql_stmt_bind_param(mariadb_statement, bind_in) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  return storage_milestone_load_generic(mariadb_statement, bind_out, pack);
}

retcode_t storage_milestone_load_next(storage_connection_t const* const connection, uint64_t const index,
                                      iota_stor_pack_t* const pack) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.milestone_select_next;
  MYSQL_BIND bind_in[1];
  MYSQL_BIND bind_out[2];

  memset(bind_in, 0, sizeof(bind_in));
  memset(bind_out, 0, sizeof(bind_out));

  column_compress_bind(bind_in, 0, &index, MYSQL_TYPE_LONGLONG, -1);

  if (mysql_stmt_bind_param(mariadb_statement, bind_in) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  return storage_milestone_load_generic(mariadb_statement, bind_out, pack);
}

retcode_t storage_milestone_exist(storage_connection_t const* const connection, flex_trit_t const* const hash,
                                  bool* const exist) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = NULL;
  MYSQL_BIND bind[1];

  if (hash) {
    memset(bind, 0, sizeof(bind));
    mariadb_statement = mariadb_connection->statements.milestone_exist_by_hash;
    column_compress_bind(bind, 0, hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);
    if (mysql_stmt_bind_param(mariadb_statement, bind) != 0) {
      log_statement_error(mariadb_statement);
      return RC_STORAGE_FAILED_BINDING;
    }
  } else {
    mariadb_statement = mariadb_connection->statements.milestone_exist;
  }

  return execute_statement_exist(mariadb_statement, exist);
}

retcode_t storage_milestone_delete(storage_connection_t const* const connection, flex_trit_t const* const hash) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.milestone_delete_by_hash;
  MYSQL_BIND bind[1];

  memset(bind, 0, sizeof(bind));

  column_compress_bind(bind, 0, hash, MYSQL_TYPE_BLOB, FLEX_TRIT_SIZE_243);

  if (mysql_stmt_bind_param_and_execute(mariadb_statement, bind) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  return RC_OK;
}

retcode_t storage_state_delta_store(storage_connection_t const* const connection, uint64_t const index,
                                    state_delta_t const* const delta) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.state_delta_store;
  MYSQL_BIND bind[2];
  size_t size = 0;
  byte_t* bytes = NULL;
  retcode_t ret = RC_OK;

  memset(bind, 0, sizeof(bind));

  size = state_delta_serialized_size(delta);

  if ((bytes = (byte_t*)calloc(size, sizeof(byte_t))) == NULL) {
    ret = RC_OOM;
    goto done;
  }

  if ((ret = state_delta_serialize(delta, bytes)) != RC_OK) {
    goto done;
  }

  bind[0].buffer = (void*)bytes;
  bind[0].buffer_type = MYSQL_TYPE_LONG_BLOB;
  bind[0].buffer_length = size;
  bind[0].is_null = 0;

  bind[1].buffer = (void*)&index;
  bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
  bind[1].is_null = 0;

  if (mysql_stmt_bind_param_and_execute(mariadb_statement, bind) != 0) {
    ret = RC_STORAGE_FAILED_BINDING;
    goto done;
  }

done:
  if (bytes) {
    free(bytes);
  }

  return ret;
}

retcode_t storage_state_delta_load(storage_connection_t const* const connection, uint64_t const index,
                                   state_delta_t* const delta) {
  mariadb_tangle_connection_t const* mariadb_connection = (mariadb_tangle_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.state_delta_load;
  MYSQL_BIND bind_in[1];
  MYSQL_BIND bind_out[1];
  byte_t* bytes = NULL;
  size_t size = 0;
  retcode_t ret = RC_OK;
  MYSQL_RES* metadata = NULL;
  my_bool set = 1;

  *delta = NULL;

  memset(bind_in, 0, sizeof(bind_in));
  memset(bind_out, 0, sizeof(bind_out));

  column_compress_bind(bind_in, 0, &index, MYSQL_TYPE_LONGLONG, -1);

  if (mysql_stmt_bind_param_and_execute(mariadb_statement, bind_in) != 0) {
    ret = RC_STORAGE_FAILED_BINDING;
    goto done;
  }

  if ((metadata = mysql_stmt_result_metadata(mariadb_statement)) == NULL) {
    ret = RC_STORAGE_FAILED_GET_RESULT;
    goto done;
  }

  if (mysql_stmt_attr_set(mariadb_statement, STMT_ATTR_UPDATE_MAX_LENGTH, &set) != 0) {
    log_statement_error(mariadb_statement);
    ret = RC_STORAGE_FAILED_SET_ATTR;
    goto done;
  }

  if (mysql_stmt_store_result(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    ret = RC_STORAGE_FAILED_STORE_RESULT;
    goto done;
  }

  if (mysql_stmt_num_rows(mariadb_statement) == 0) {
    goto done;
  }

  size = metadata->fields[0].max_length;

  if ((bytes = (byte_t*)calloc(size, sizeof(byte_t))) == NULL) {
    ret = RC_OOM;
    goto done;
  }

  bind_out[0].buffer = (char*)bytes;
  bind_out[0].buffer_type = MYSQL_TYPE_LONG_BLOB;
  bind_out[0].buffer_length = size;

  if (mysql_stmt_bind_result(mariadb_statement, bind_out) != 0) {
    log_statement_error(mariadb_statement);
    ret = RC_STORAGE_FAILED_BINDING;
    goto done;
  }

  if (mysql_stmt_fetch(mariadb_statement) != 0) {
    log_statement_error(mariadb_statement);
    ret = RC_STORAGE_FAILED_STORE_RESULT;
    goto done;
  }

  if ((ret = state_delta_deserialize(bytes, size, delta)) != RC_OK) {
    goto done;
  }

done:
  if (bytes) {
    free(bytes);
  }
  if (metadata) {
    mysql_free_result(metadata);
  }

  return ret;
}

retcode_t storage_spent_address_store(storage_connection_t const* const connection, flex_trit_t const* const address) {
  mariadb_spent_addresses_connection_t const* mariadb_connection =
      (mariadb_spent_addresses_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.spent_address_insert;
  MYSQL_BIND bind[1];

  memset(bind, 0, sizeof(bind));

  bind[0].buffer = (void*)address;
  bind[0].buffer_type = MYSQL_TYPE_BLOB;
  bind[0].buffer_length = FLEX_TRIT_SIZE_243;
  bind[0].is_null = 0;

  if (mysql_stmt_bind_param_and_execute(mariadb_statement, bind) != 0) {
    return RC_STORAGE_FAILED_BINDING;
  }

  return RC_OK;
}

retcode_t storage_spent_address_exist(storage_connection_t const* const connection, flex_trit_t const* const address,
                                      bool* const exist) {
  mariadb_spent_addresses_connection_t const* mariadb_connection =
      (mariadb_spent_addresses_connection_t*)connection->actual;
  MYSQL_STMT* mariadb_statement = mariadb_connection->statements.spent_address_exist;
  MYSQL_BIND bind[1];

  memset(bind, 0, sizeof(bind));

  bind[0].buffer = (void*)address;
  bind[0].buffer_type = MYSQL_TYPE_BLOB;
  bind[0].buffer_length = FLEX_TRIT_SIZE_243;
  bind[0].is_null = 0;

  if (mysql_stmt_bind_param(mariadb_statement, bind) != 0) {
    log_statement_error(mariadb_statement);
    return RC_STORAGE_FAILED_BINDING;
  }

  return execute_statement_exist(mariadb_statement, exist);
}

retcode_t storage_spent_addresses_store(storage_connection_t const* const connection, hash243_set_t const addresses) {
  mariadb_spent_addresses_connection_t const* mariadb_connection =
      (mariadb_spent_addresses_connection_t*)connection->actual;
  hash243_set_entry_t *iter = NULL, *tmp = NULL;
  retcode_t ret = RC_OK;

  if ((ret = start_transaction((MYSQL*)&mariadb_connection->db)) != RC_OK) {
    return ret;
  }

  HASH_SET_ITER(addresses, iter, tmp) {
    if ((ret = storage_spent_address_store(connection, iter->hash)) != RC_OK) {
      break;
    }
  }

  return end_transaction((MYSQL*)&mariadb_connection->db, ret);
}
