/*
 * SMS Queue - Asynchronous SMS processing
 *
 * This module provides a background thread for processing SMS database
 * operations to prevent blocking the main monitor loop during calls.
 *
 * Copyright (C) 2026
 */

#include "ast_config.h"

#include <pthread.h>

#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"

#include "chan_quectel.h"
#include "channel.h"
#include "manager.h"
#include "sms_queue.h"
#include "smsdb.h"

/* Queue head definition */
static AST_LIST_HEAD_STATIC(sms_queue, sms_task);

/* Worker thread */
static pthread_t sms_worker_thread = AST_PTHREADT_NULL;
static volatile int sms_queue_shutdown_flag = 0;

/* Condition variable for worker thread wake-up */
static ast_cond_t sms_queue_cond;

/* Forward declarations */
static void *sms_worker_func(void *arg);
static void process_sms_task(sms_task_t *task);
static void delete_from_modem(struct pvt *pvt, const sms_task_t *task);

/*!
 * \brief Initialize the SMS queue system
 */
int sms_queue_init(void) {
  int res;

  sms_queue_shutdown_flag = 0;

  res = ast_cond_init(&sms_queue_cond, NULL);
  if (res != 0) {
    ast_log(LOG_ERROR, "Failed to initialize SMS queue condition variable\n");
    return -1;
  }

  res = ast_pthread_create_background(&sms_worker_thread, NULL, sms_worker_func,
                                      NULL);
  if (res != 0) {
    ast_log(LOG_ERROR, "Failed to create SMS worker thread\n");
    ast_cond_destroy(&sms_queue_cond);
    return -1;
  }

  ast_log(LOG_NOTICE, "SMS queue system initialized\n");
  return 0;
}

/*!
 * \brief Shutdown the SMS queue system
 */
void sms_queue_shutdown(void) {
  sms_task_t *task;

  /* Signal shutdown */
  sms_queue_shutdown_flag = 1;

  /* Wake up worker thread */
  AST_LIST_LOCK(&sms_queue);
  ast_cond_signal(&sms_queue_cond);
  AST_LIST_UNLOCK(&sms_queue);

  /* Wait for worker thread to finish */
  if (sms_worker_thread != AST_PTHREADT_NULL) {
    pthread_join(sms_worker_thread, NULL);
    sms_worker_thread = AST_PTHREADT_NULL;
  }

  /* Drop remaining tasks. Their messages have not been deleted from the
   * modem yet, so they are read again when the module is loaded. */
  AST_LIST_LOCK(&sms_queue);
  while ((task = AST_LIST_REMOVE_HEAD(&sms_queue, entry))) {
    ast_free(task);
  }
  AST_LIST_UNLOCK(&sms_queue);

  ast_cond_destroy(&sms_queue_cond);

  ast_log(LOG_NOTICE, "SMS queue system shutdown\n");
}

/*!
 * \brief Queue an SMS part for background processing
 */
int sms_queue_put(const char *imsi, const char *device_id, const char *sender,
                  const char *scts, int ref, int parts, int order,
                  const char *message, unsigned int delete_index) {
  sms_task_t *task;

  if (sms_queue_shutdown_flag) {
    ast_log(LOG_WARNING, "SMS queue is shutting down, rejecting task\n");
    return -1;
  }

  task = ast_calloc(1, sizeof(*task));
  if (!task) {
    ast_log(LOG_ERROR, "Failed to allocate SMS task\n");
    return -1;
  }

  task->type = SMS_TASK_PUT_PART;
  ast_copy_string(task->imsi, imsi, sizeof(task->imsi));
  ast_copy_string(task->device_id, device_id, sizeof(task->device_id));
  ast_copy_string(task->sender, sender, sizeof(task->sender));
  ast_copy_string(task->scts, scts ? scts : "", sizeof(task->scts));
  task->ref = ref;
  task->parts = parts;
  task->order = order;
  ast_copy_string(task->message, message, sizeof(task->message));
  task->delete_index = delete_index;

  AST_LIST_LOCK(&sms_queue);
  AST_LIST_INSERT_TAIL(&sms_queue, task, entry);
  ast_cond_signal(&sms_queue_cond);
  AST_LIST_UNLOCK(&sms_queue);

  ast_debug(3, "SMS task queued: imsi=%s sender=%s ref=%d part=%d/%d\n", imsi,
            sender, ref, order, parts);

  return 0;
}

/*!
 * \brief Worker thread function
 */
static void *sms_worker_func(void *arg) {
  sms_task_t *task;

  (void)arg;

  ast_debug(1, "SMS worker thread started\n");

  while (!sms_queue_shutdown_flag) {
    AST_LIST_LOCK(&sms_queue);

    /* Wait for tasks if queue is empty */
    while (AST_LIST_EMPTY(&sms_queue) && !sms_queue_shutdown_flag) {
      ast_cond_wait(&sms_queue_cond, &sms_queue.lock);
    }

    if (sms_queue_shutdown_flag) {
      AST_LIST_UNLOCK(&sms_queue);
      break;
    }

    /* Get next task */
    task = AST_LIST_REMOVE_HEAD(&sms_queue, entry);
    AST_LIST_UNLOCK(&sms_queue);

    if (task) {
      process_sms_task(task);
      ast_free(task);
    }
  }

  ast_debug(1, "SMS worker thread exiting\n");
  return NULL;
}

/*!
 * \brief Process a single SMS task
 */
static void process_sms_task(sms_task_t *task) {
  char fullmsg[SMS_QUEUE_FULLMSG_SIZE];
  char text_base64[40800];
  int csms_cnt;
  int fullmsg_len;
  struct pvt *pvt;

  ast_debug(2, "Processing SMS task: device=%s sender=%s ref=%d part=%d/%d\n",
            task->device_id, task->sender, task->ref, task->order, task->parts);

  switch (task->type) {
  case SMS_TASK_PUT_PART:
    if (task->parts > 1) {
      /* Multi-part message: store in database */
      csms_cnt = smsdb_put(task->imsi, task->sender, task->ref, task->parts,
                           task->order, task->message, fullmsg);

      if (csms_cnt <= 0) {
        ast_log(LOG_ERROR, "[%s] Error putting SMS to SMSDB\n",
                task->device_id);
        /* Still deliver the single part as-is */
        ast_copy_string(fullmsg, task->message, sizeof(fullmsg));
        fullmsg_len = strlen(fullmsg);
        goto deliver_message;
      }

      if (csms_cnt < task->parts) {
        /* Still waiting for more parts */
        ast_verb(1, "[%s] SMS part %d/%d stored, waiting for remaining parts\n",
                 task->device_id, task->order, task->parts);
        /* The part is safe in SMSDB now */
        pvt = find_device_ext(task->device_id);
        if (pvt) {
          delete_from_modem(pvt, task);
          ast_mutex_unlock(&pvt->lock);
        }
        return;
      }

      /* All parts received, fullmsg now contains the complete message */
      fullmsg_len = strlen(fullmsg);
      ast_verb(1,
               "[%s] All %d SMS parts received, delivering complete message\n",
               task->device_id, task->parts);
    } else {
      /* Single part message */
      ast_copy_string(fullmsg, task->message, sizeof(fullmsg));
      fullmsg_len = strlen(fullmsg);
    }

  deliver_message:
    ast_verb(1, "[%s] Got full SMS from %s: '%s'\n", task->device_id,
             task->sender, fullmsg);

    /* Base64 encode for manager event */
    ast_base64encode(text_base64, (unsigned char *)fullmsg, fullmsg_len,
                     sizeof(text_base64));

    /* Send manager events */
    manager_event_new_sms(task->device_id, task->sender, fullmsg);
    manager_event_new_sms_base64(task->device_id, task->sender, text_base64);

    /* Look up the device to get context and start local channel */
    pvt = find_device_ext(task->device_id);
    if (pvt) {
      /* pvt is returned locked by find_device_ext */
      channel_var_t vars[] = {
          {"SMS", fullmsg},
          {"SMS_BASE64", text_base64},
          {"SMS_TS", task->scts},
          {NULL, NULL},
      };
      start_local_channel(pvt, "sms", task->sender, vars);
      delete_from_modem(pvt, task);
      ast_mutex_unlock(&pvt->lock);
    } else {
      ast_log(LOG_WARNING, "[%s] Device not found for SMS delivery\n",
              task->device_id);
    }
    break;
  }
}

/*!
 * \brief Delete a handled SMS from modem storage
 * \param pvt Device, locked by the caller
 */
static void delete_from_modem(struct pvt *pvt, const sms_task_t *task) {
  if (task->delete_index == -1U) {
    return;
  }
  if (at_enqueue_delete_sms(&pvt->sys_chan, task->delete_index)) {
    ast_log(LOG_ERROR, "[%s] Error enqueueing delete of SMS at index %u\n",
            task->device_id, task->delete_index);
  }
}
