// Copyright 2018 IOTA Foundation

#ifndef CCLIENT_REQUEST_FIND_TRANSACTIONS_H
#define CCLIENT_REQUEST_FIND_TRANSACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types/types.h"

typedef struct {
  /**
   * List of addresses.
   * Transactions with any of these addresses as input/output will be returned
   */
  UT_array* addresses;

  /**
   * List of transaction tags.
   * Transactions with any of these tags will be returned
   */
  UT_array* tags;

  /**
   * List of approvees of a transaction.
   * Transactions which directly approve any of approvees will be returned
   */
  UT_array* approvees;
  /**
   * List of bundle hashes.
   * Transactions belonging to bundles will be returned
   */
  UT_array* bundles;
  // size_t numBundles;

} find_transactions_req_t;

find_transactions_req_t* find_transactions_req_new();
void find_transactions_req_free(find_transactions_req_t* req);

void find_transactions_req_add_address(find_transactions_req_t* req,
                                       char* addr);
void find_transactions_req_add_tag(find_transactions_req_t* req, char* tag);
void find_transactions_req_add_approvee(find_transactions_req_t* req,
                                        char* approvee);
void find_transactions_req_add_bundle(find_transactions_req_t* req,
                                      char* bundle);

#ifdef __cplusplus
}
#endif

#endif  // CCLIENT_REQUEST_FIND_TRANSACTIONS_H
