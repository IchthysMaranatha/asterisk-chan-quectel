/*
 * SMS Queue - Asynchronous SMS processing
 *
 * This module provides a background thread for processing SMS database
 * operations to prevent blocking the main monitor loop during calls.
 *
 * Copyright (C) 2026
 */

#ifndef SMS_QUEUE_H
#define SMS_QUEUE_H

#include "ast_config.h"

#include <asterisk/linkedlists.h>

/* Forward declaration */
struct pvt;

/* Maximum sizes for SMS data */
#define SMS_QUEUE_IMSI_SIZE 32
#define SMS_QUEUE_ADDR_SIZE 256
#define SMS_QUEUE_MSG_SIZE 4096
#define SMS_QUEUE_FULLMSG_SIZE (160 * 255)
#define SMS_QUEUE_SCTS_SIZE 64

/* SMS task types */
typedef enum {
  SMS_TASK_PUT_PART, /* Store an SMS part, check for completion */
} sms_task_type_t;

/* SMS task structure */
typedef struct sms_task {
  sms_task_type_t type;

  /* Device identification */
  char imsi[SMS_QUEUE_IMSI_SIZE];
  char device_id[64]; /* For manager events */

  /* SMS metadata */
  char sender[SMS_QUEUE_ADDR_SIZE];
  char scts[SMS_QUEUE_SCTS_SIZE]; /* Service center timestamp */
  int ref;                        /* Reference ID for multi-part */
  int parts;                      /* Total number of parts */
  int order;                      /* Current part order */

  /* Message content */
  char message[SMS_QUEUE_MSG_SIZE];

  /* Linked list entry */
  AST_LIST_ENTRY(sms_task) entry;
} sms_task_t;

/*!
 * \brief Initialize the SMS queue system
 * \retval 0 success
 * \retval -1 error
 */
int sms_queue_init(void);

/*!
 * \brief Shutdown the SMS queue system
 */
void sms_queue_shutdown(void);

/*!
 * \brief Queue an SMS part for background processing
 * \param imsi Device IMSI
 * \param device_id Device ID for events
 * \param sender Sender address
 * \param scts Service center timestamp
 * \param ref Multi-part reference ID
 * \param parts Total number of parts
 * \param order Current part order (1-based)
 * \param message The message content
 * \retval 0 success
 * \retval -1 error
 */
int sms_queue_put(const char *imsi, const char *device_id, const char *sender,
                  const char *scts, int ref, int parts, int order,
                  const char *message);

#endif /* SMS_QUEUE_H */
