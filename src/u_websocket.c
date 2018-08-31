/**
 * 
 * Ulfius Framework
 * 
 * REST framework library
 * 
 * u_websocket.c: websocket implementation
 * 
 * Copyright 2017-2018 Nicolas Mora <mail@babelouest.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation;
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU GENERAL PUBLIC LICENSE for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include "u_private.h"
#include "../include/ulfius.h"

#ifndef U_DISABLE_WEBSOCKET
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <gnutls/gnutls.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

/**
 * Set a websocket in the response
 * You must set at least websocket_manager_callback or websocket_incoming_message_callback
 * @Parameters
 * response: struct _u_response to send back the websocket initialization, mandatory
 * websocket_protocol: list of protocols, separated by a comma, or NULL if all protocols are accepted
 * websocket_extensions: list of extensions, separated by a comma, or NULL if all extensions are accepted
 * websocket_manager_callback: callback function called right after the handshake acceptance, optional
 * websocket_manager_user_data: any data that will be given to the websocket_manager_callback, optional
 * websocket_incoming_message_callback: callback function called on each incoming complete message, optional
 * websocket_incoming_user_data: any data that will be given to the websocket_incoming_message_callback, optional
 * websocket_onclose_callback: callback function called right before closing the websocket, must be complete for the websocket to close
 * websocket_onclose_user_data: any data that will be given to the websocket_onclose_callback, optional
 * @Return value: U_OK on success
 */
int ulfius_set_websocket_response(struct _u_response * response,
                                   const char * websocket_protocol,
                                   const char * websocket_extensions, 
                                   void (* websocket_manager_callback) (const struct _u_request * request,
                                                                       struct _websocket_manager * websocket_manager,
                                                                       void * websocket_manager_user_data),
                                   void * websocket_manager_user_data,
                                   void (* websocket_incoming_message_callback) (const struct _u_request * request,
                                                                                struct _websocket_manager * websocket_manager,
                                                                                const struct _websocket_message * message,
                                                                                void * websocket_incoming_user_data),
                                   void * websocket_incoming_user_data,
                                   void (* websocket_onclose_callback) (const struct _u_request * request,
                                                                       struct _websocket_manager * websocket_manager,
                                                                       void * websocket_onclose_user_data),
                                   void * websocket_onclose_user_data) {
  if (response != NULL && (websocket_manager_callback != NULL || websocket_incoming_message_callback)) {
    if (((struct _websocket_handle *)response->websocket_handle)->websocket_protocol != NULL) {
      o_free(((struct _websocket_handle *)response->websocket_handle)->websocket_protocol);
    }
    ((struct _websocket_handle *)response->websocket_handle)->websocket_protocol = o_strdup(websocket_protocol);
    if (((struct _websocket_handle *)response->websocket_handle)->websocket_protocol == NULL && websocket_protocol != NULL) {
      y_log_message(Y_LOG_LEVEL_ERROR, "Error allocating resources for response->websocket_protocol");
      return U_ERROR_MEMORY;
    }
    if (((struct _websocket_handle *)response->websocket_handle)->websocket_extensions != NULL) {
      o_free(((struct _websocket_handle *)response->websocket_handle)->websocket_extensions);
    }
    ((struct _websocket_handle *)response->websocket_handle)->websocket_extensions = o_strdup(websocket_extensions);
    if (((struct _websocket_handle *)response->websocket_handle)->websocket_extensions == NULL && websocket_extensions != NULL) {
      y_log_message(Y_LOG_LEVEL_ERROR, "Error allocating resources for response->websocket_extensions");
      o_free(((struct _websocket_handle *)response->websocket_handle)->websocket_protocol);
      return U_ERROR_MEMORY;
    }
    ((struct _websocket_handle *)response->websocket_handle)->websocket_manager_callback = websocket_manager_callback;
    ((struct _websocket_handle *)response->websocket_handle)->websocket_manager_user_data = websocket_manager_user_data;
    ((struct _websocket_handle *)response->websocket_handle)->websocket_incoming_message_callback = websocket_incoming_message_callback;
    ((struct _websocket_handle *)response->websocket_handle)->websocket_incoming_user_data = websocket_incoming_user_data;
    ((struct _websocket_handle *)response->websocket_handle)->websocket_onclose_callback = websocket_onclose_callback;
    ((struct _websocket_handle *)response->websocket_handle)->websocket_onclose_user_data = websocket_onclose_user_data;
    return U_OK;
  } else {
    return U_ERROR_PARAMS;
  }
}

/**
 * Run websocket in a separate thread
 * then sets a listening message loop
 * Complete the callback when the websocket is closed
 * The websocket can be closed by the client, the manager, the program, or on network disconnect
 */
void * ulfius_thread_websocket(void * data) {
  struct _websocket * websocket = (struct _websocket*)data;
  struct _websocket_message * message = NULL;
  pthread_t thread_websocket_manager;
  pthread_mutexattr_t mutexattr;
  int thread_ret_websocket_manager = 0, poll_ret;
  int error = 0;
  socklen_t len = sizeof (error);
  
  if (websocket != NULL && websocket->websocket_manager != NULL) {
    pthread_mutexattr_init ( &mutexattr );
    pthread_mutexattr_settype( &mutexattr, PTHREAD_MUTEX_RECURSIVE );
    if (pthread_mutex_init(&(websocket->websocket_manager->read_lock), &mutexattr) != 0 || pthread_mutex_init(&(websocket->websocket_manager->write_lock), &mutexattr) != 0) {
      y_log_message(Y_LOG_LEVEL_ERROR, "Impossible to initialize Mutex Lock for websocket");
      websocket->websocket_manager->connected = 0;
    }
    pthread_mutexattr_destroy( &mutexattr );
    if (websocket->websocket_manager_callback != NULL && websocket->websocket_manager->connected) {
      websocket->websocket_manager->manager_closed = 0;
      thread_ret_websocket_manager = pthread_create(&thread_websocket_manager, NULL, ulfius_thread_websocket_manager_run, (void *)websocket);
      if (thread_ret_websocket_manager) {
        y_log_message(Y_LOG_LEVEL_ERROR, "Error creating websocket manager thread, return code: %d", thread_ret_websocket_manager);
        websocket->websocket_manager->connected = 0;
      }
    } else {
      websocket->websocket_manager->manager_closed = 1;
    }
    while (websocket->websocket_manager->connected && !websocket->websocket_manager->closing) {
      message = NULL;
      if (pthread_mutex_lock(&websocket->websocket_manager->read_lock)) {
        websocket->websocket_manager->connected = 0;
      }
      error = 0;
      poll_ret = getsockopt(websocket->websocket_manager->mhd_sock, SOL_SOCKET, SO_ERROR, &error, &len);
      if (poll_ret != 0) {
        y_log_message(Y_LOG_LEVEL_ERROR, "Error getsockopt");
        websocket->websocket_manager->connected = 0;
      } else {
        if (error != 0) {
          y_log_message(Y_LOG_LEVEL_ERROR, "Socket not good enough");
          websocket->websocket_manager->connected = 0;
        } else {
          if (ulfius_read_incoming_message(websocket->websocket_manager, &message) == U_OK) {
            if (message->opcode == U_WEBSOCKET_OPCODE_CLOSE) {
              // Send close command back, then close the socket
              if (ulfius_websocket_send_message(websocket->websocket_manager, U_WEBSOCKET_OPCODE_CLOSE, 0, NULL) != U_OK) {
                y_log_message(Y_LOG_LEVEL_ERROR, "Error sending close command");
              }
              websocket->websocket_manager->closing = 1;
            } else if (message->opcode == U_WEBSOCKET_OPCODE_PING) {
              // Send pong command
              if (ulfius_websocket_send_message(websocket->websocket_manager, U_WEBSOCKET_OPCODE_PONG, 0, NULL) != U_OK) {
                y_log_message(Y_LOG_LEVEL_ERROR, "Error sending pong command");
              }
            } else if (message->opcode != U_WEBSOCKET_OPCODE_NONE && message != NULL) {
              if (websocket->websocket_incoming_message_callback != NULL) {
                y_log_message(Y_LOG_LEVEL_DEBUG, "Dispatch message %p of size %zu", message, message->data_len);
                websocket->websocket_incoming_message_callback(websocket->request, websocket->websocket_manager, message, websocket->websocket_incoming_user_data);
              }
            }
            if (message != NULL) {
              if (ulfius_push_websocket_message(websocket->websocket_manager->message_list_incoming, message) != U_OK) {
                y_log_message(Y_LOG_LEVEL_ERROR, "Error pushing new websocket message in list");
              }
            }
          }
        }
      }
      pthread_mutex_unlock(&websocket->websocket_manager->read_lock);
    }
    if (ulfius_close_websocket(websocket) != U_OK) {
      y_log_message(Y_LOG_LEVEL_ERROR, "Error closing websocket");
    }
    // Wait for thread manager to close
    pthread_join(thread_websocket_manager, NULL);
  } else {
    y_log_message(Y_LOG_LEVEL_ERROR, "Error websocket parameters");
  }
  ulfius_clear_websocket(websocket);
  return NULL;
}

/**
 * Websocket callback function for MHD
 * Starts the websocket manager if set,
 */
void ulfius_start_websocket_cb (void * cls,
                                struct MHD_Connection * connection,
                                void * con_cls,
                                const char * extra_in,
                                size_t extra_in_size,
                                MHD_socket sock,
                                struct MHD_UpgradeResponseHandle * urh) {
  struct _websocket * websocket = (struct _websocket *)cls;
  pthread_t thread_websocket;
  int thread_ret_websocket = 0, thread_detach_websocket = 0;
  UNUSED(connection);
  UNUSED(con_cls);
  UNUSED(extra_in);
  UNUSED(extra_in_size);
  
  if (websocket != NULL) {
    websocket->urh = urh;
    websocket->websocket_manager = o_malloc(sizeof(struct _websocket_manager));
    // Run websocket manager in a thread if set
    if (websocket->websocket_manager != NULL) {
      websocket->websocket_manager->message_list_incoming = o_malloc(sizeof(struct _websocket_message_list));
      websocket->websocket_manager->message_list_outcoming = o_malloc(sizeof(struct _websocket_message_list));
      ulfius_init_websocket_message_list(websocket->websocket_manager->message_list_incoming);
      ulfius_init_websocket_message_list(websocket->websocket_manager->message_list_outcoming);
      websocket->websocket_manager->mhd_sock = sock;
      websocket->websocket_manager->fds.fd = sock;
      websocket->websocket_manager->fds.events = POLLIN | POLLRDHUP;
      websocket->websocket_manager->connected = 1;
      websocket->websocket_manager->closing = 0;
      thread_ret_websocket = pthread_create(&thread_websocket, NULL, ulfius_thread_websocket, (void *)websocket);
      thread_detach_websocket = pthread_detach(thread_websocket);
      if (thread_ret_websocket || thread_detach_websocket) {
        y_log_message(Y_LOG_LEVEL_ERROR, "Error creating or detaching websocket manager thread, return code: %d, detach code: %d",
                      thread_ret_websocket, thread_detach_websocket);
        ulfius_clear_websocket(websocket);
      }
    } else {
      y_log_message(Y_LOG_LEVEL_ERROR, "Error allocating resources for websocket_manager");
      ulfius_clear_websocket(websocket);
    }
  } else {
    y_log_message(Y_LOG_LEVEL_ERROR, "Error websocket is NULL");
    ulfius_clear_websocket(websocket);
  }
  return;
}

static int is_websocket_data_available(struct _websocket_manager * websocket_manager) {
  int ret = -1, poll_ret;
  
  do {
    poll_ret = poll(&websocket_manager->fds, 1, U_WEBSOCKET_USEC_WAIT);
    if (poll_ret == -1) {
      y_log_message(Y_LOG_LEVEL_ERROR, "Error poll websocket read for close signal");
      ret = 0;
    } else if (websocket_manager->fds.revents & (POLLRDHUP|POLLERR|POLLHUP|POLLNVAL)) {
      ret = 0;
    } else if (poll_ret > 0) {
      ret = 1;
    }
  } while (ret == -1);
  return ret;
}

static size_t read_data_from_socket(struct _websocket_manager * websocket_manager, uint8_t * data, size_t len) {
  size_t ret = 0;
  ssize_t data_len;
  int data_available;
  
  if (len > 0) {
    do {
      data_available = is_websocket_data_available(websocket_manager);
      if (data_available) {
        data_len = read(websocket_manager->mhd_sock, data, (len - ret));
        if (data_len > 0) {
          ret += data_len;
        }
      }
    } while (data_available && ret < len);
  }
  return ret;
}

/**
 * Read and parse a new message from the websocket
 * Return the opcode of the new websocket, U_WEBSOCKET_OPCODE_NONE if no message arrived, or U_WEBSOCKET_OPCODE_ERROR on error
 * Sets the new message in the message variable
 */
int ulfius_read_incoming_message(struct _websocket_manager * websocket_manager, struct _websocket_message ** message) {
  int ret = U_OK, fin = 0, i;
  int message_error = 0;
  uint8_t header[2], payload_len[8], masking_key[4];
  uint8_t * payload_data;
  size_t msg_len = 0, len;
  
  *message = o_malloc(sizeof(struct _websocket_message));
  if (*message != NULL) {
    (*message)->data_len = 0;
    (*message)->has_mask = 0;
    (*message)->data = NULL;
    time(&(*message)->datestamp);
    
    do {
      if (!message_error) {
        // Read header
        if (read_data_from_socket(websocket_manager, header, 2) == 2) {
          (*message)->opcode = header[0] & 0x0F;
          fin = (header[0] & U_WEBSOCKET_BIT_FIN);
          if (!fin) {
            y_log_message(Y_LOG_LEVEL_DEBUG, "message fragmented");
          }
          if ((header[1] & U_WEBSOCKET_LEN_MASK) <= 125) {
            msg_len = (header[1] & U_WEBSOCKET_LEN_MASK);
          } else if ((header[1] & U_WEBSOCKET_LEN_MASK) == 126) {
            len = read_data_from_socket(websocket_manager, payload_len, 2);
            if (len == 2) {
              msg_len = payload_len[1] | ((uint64_t)payload_len[0] << 8);
            } else {
              message_error = 1;
              ret = U_ERROR;
              y_log_message(Y_LOG_LEVEL_ERROR, "Error reading websocket message length");
            }
          } else if ((header[1] & U_WEBSOCKET_LEN_MASK) == 127) {
            len = read_data_from_socket(websocket_manager, payload_len, 8);
            if (len == 8) {
              msg_len = payload_len[7] |
                        ((uint64_t)payload_len[6] << 8) |
                        ((uint64_t)payload_len[5] << 16) |
                        ((uint64_t)payload_len[4] << 24) |
                        ((uint64_t)payload_len[3] << 32) |
                        ((uint64_t)payload_len[2] << 40) |
                        ((uint64_t)payload_len[1] << 48) |
                        ((uint64_t)payload_len[0] << 54);
            } else {
              message_error = 1;
              ret = U_ERROR;
              y_log_message(Y_LOG_LEVEL_ERROR, "Error reading websocket message length");
            }
          }
        } else {
          y_log_message(Y_LOG_LEVEL_ERROR, "Error getting websocket header");
          ret = U_ERROR;
        }
        
        // Read mask
        if (header[1] & U_WEBSOCKET_HAS_MASK) {
          (*message)->has_mask = 1;
          len = read_data_from_socket(websocket_manager, masking_key, 4);
          if (len != 4) {
            message_error = 1;
            ret = U_ERROR;
            y_log_message(Y_LOG_LEVEL_ERROR, "Error reading websocket for mask");
          }
          if (!message_error) {
            if (msg_len > 0) {
              payload_data = o_malloc(msg_len*sizeof(uint8_t));
              y_log_message(Y_LOG_LEVEL_DEBUG, "msg_len is %zu", msg_len);
              len = read_data_from_socket(websocket_manager, payload_data, msg_len);
              if ((unsigned int)len == msg_len) {
                y_log_message(Y_LOG_LEVEL_DEBUG, "Decode and store message");
                // Decode message
                (*message)->data = o_realloc((*message)->data, (msg_len+(*message)->data_len)*sizeof(uint8_t));
                for (i = (*message)->data_len; ((*message)->data_len + i) < msg_len; i++) {
                  (*message)->data[i] = payload_data[i-(*message)->data_len] ^ masking_key[(i-(*message)->data_len)%4];
                }
                (*message)->data_len += msg_len;
                y_log_message(Y_LOG_LEVEL_DEBUG, "data_len is now %zu", (*message)->data_len);
              } else {
                message_error = 1;
                ret = U_ERROR;
                y_log_message(Y_LOG_LEVEL_ERROR, "Error reading websocket for payload_data: %zu", len);
              }
              o_free(payload_data);
            }
          }
        } else {
          message_error = 1;
          ret = U_ERROR;
          y_log_message(Y_LOG_LEVEL_ERROR, "Incoming message has no MASK flag, exiting");
        }
      }
    } while (!message_error && !fin);
  } else {
    y_log_message(Y_LOG_LEVEL_ERROR, "Error allocating resources for *message");
  }
  return ret;
}

/**
 * Clear all data related to the websocket
 */
int ulfius_clear_websocket(struct _websocket * websocket) {
  if (websocket != NULL) {
    if (MHD_upgrade_action (websocket->urh, MHD_UPGRADE_ACTION_CLOSE) != MHD_YES) {
      y_log_message(Y_LOG_LEVEL_ERROR, "Error sending MHD_UPGRADE_ACTION_CLOSE frame to urh");
    }
    ulfius_instance_remove_websocket_active(websocket->instance, websocket);
    ulfius_clear_websocket_manager(websocket->websocket_manager);
    ulfius_clean_request_full(websocket->request);
    o_free(websocket->websocket_manager);
    websocket->websocket_manager = NULL;
    o_free(websocket->websocket_protocol_selected);
    o_free(websocket->websocket_extensions_selected);
    o_free(websocket);
    return U_OK;
  } else {
    return U_ERROR_PARAMS;
  }
}

/**
 * Run the websocket manager in a separated detached thread
 */
void * ulfius_thread_websocket_manager_run(void * args) {
  struct _websocket * websocket = (struct _websocket *)args;
  if (websocket != NULL && websocket->websocket_manager_callback != NULL && websocket->websocket_manager != NULL) {
    websocket->websocket_manager_callback(websocket->request, websocket->websocket_manager, websocket->websocket_manager_user_data);
    // Websocket manager callback complete, set close signal
    websocket->websocket_manager->manager_closed = 1;
    websocket->websocket_manager->closing = 1;
  }
  pthread_exit(NULL);
}

/**
 * Generates a handhshake answer from the key given in parameter
 */
int ulfius_generate_handshake_answer(const char * key, char * out_digest) {
  gnutls_datum_t key_data;
  unsigned char encoded_key[32] = {0};
  size_t encoded_key_size = 32, encoded_key_size_base64;
  int res, to_return = 0;
  
  key_data.data = (unsigned char*)msprintf("%s%s", key, U_WEBSOCKET_MAGIC_STRING);
  key_data.size = strlen((const char *)key_data.data);
  
  if (key_data.data != NULL && out_digest != NULL && (res = gnutls_fingerprint(GNUTLS_DIG_SHA1, &key_data, encoded_key, &encoded_key_size)) == GNUTLS_E_SUCCESS) {
    if (o_base64_encode(encoded_key, encoded_key_size, (unsigned char *)out_digest, &encoded_key_size_base64)) {
      to_return = 1;
    } else {
      y_log_message(Y_LOG_LEVEL_ERROR, "Error base64 encoding hashed key");
    }
  } else {
    y_log_message(Y_LOG_LEVEL_ERROR, "Error getting sha1 signature for key");
  }
  o_free(key_data.data);
  return to_return;
}

/**
 * Initialize a websocket message list
 * Return U_OK on success
 */
int ulfius_init_websocket_message_list(struct _websocket_message_list * message_list) {
  if (message_list != NULL) {
    message_list->len = 0;
    message_list->list = NULL;
    return U_OK;
  } else {
    return U_ERROR_PARAMS;
  }
}

/**
 * Clear data of a websocket message list
 */
void ulfius_clear_websocket_message_list(struct _websocket_message_list * message_list) {
  size_t i;
  if (message_list != NULL) {
    for (i=0; i < message_list->len; i++) {
      ulfius_clear_websocket_message(message_list->list[i]);
    }
    o_free(message_list->list);
  }
}

/**
 * Clear data of a websocket message
 */
void ulfius_clear_websocket_message(struct _websocket_message * message) {
  if (message != NULL) {
    o_free(message->data);
    o_free(message);
  }
}

/**
 * Append a message in a message list
 * Return U_OK on success
 */
int ulfius_push_websocket_message(struct _websocket_message_list * message_list, struct _websocket_message * message) {
  if (message_list != NULL && message != NULL) {
    message_list->list = o_realloc(message_list->list, (message_list->len+1)*sizeof(struct _websocket_message *));
    if (message_list->list != NULL) {
      message_list->list[message_list->len] = message;
      message_list->len++;
      return U_OK;
    } else {
      return U_ERROR_MEMORY;
    }
  } else {
    return U_ERROR_PARAMS;
  }
}

/**
 * Return the first message of the message list
 * Return NULL if message_list has no message
 * Returned value must be cleared after use
 */
struct _websocket_message * ulfius_websocket_pop_first_message(struct _websocket_message_list * message_list) {
  size_t len;
  struct _websocket_message * message = NULL;
  if (message_list != NULL && message_list->len > 0) {
    message = message_list->list[0];
    for (len=0; len < message_list->len-1; len++) {
      message_list->list[len] = message_list->list[len+1];
    }
    message_list->list = o_realloc(message_list->list, (message_list->len-1));
    message_list->len--;
  }
  return message;
}

/**
 * Send a message in the websocket
 * Return U_OK on success
 */
int ulfius_websocket_send_message(struct _websocket_manager * websocket_manager,
                                  const uint8_t opcode,
                                  const uint64_t data_len,
                                  const char * data) {
  int ret, count = WEBSOCKET_MAX_CLOSE_TRY, poll_ret;
  struct _websocket_message * message;
  if (websocket_manager != NULL && websocket_manager->connected) {
    if (pthread_mutex_lock(&websocket_manager->write_lock)) {
      return U_ERROR;
    }
    if (opcode == U_WEBSOCKET_OPCODE_CLOSE) {
      // If message sent is U_WEBSOCKET_OPCODE_CLOSE, wait for the response for 2 s max, then close the connection
      if (pthread_mutex_lock(&websocket_manager->read_lock)) {
        pthread_mutex_unlock(&websocket_manager->write_lock);
        return U_ERROR;
      }
      ret = ulfius_websocket_send_message_nolock(websocket_manager, opcode, 1, data_len, data);
      message = NULL;
      poll_ret = poll(&websocket_manager->fds, 1, U_WEBSOCKET_USEC_WAIT);
      if (poll_ret == -1) {
        y_log_message(Y_LOG_LEVEL_ERROR, "Error poll websocket read for close signal");
      } else if (!(websocket_manager->fds.revents & (POLLRDHUP|POLLERR|POLLHUP|POLLNVAL)) && poll_ret > 0) {
        do {
          if (ulfius_read_incoming_message(websocket_manager, &message) == U_OK) {
            while (count-- > 0) {
              if (ulfius_push_websocket_message(websocket_manager->message_list_incoming, message) != U_OK) {
                y_log_message(Y_LOG_LEVEL_ERROR, "Error pushing new websocket message in list");
              }
            }
          }
        } while (message->opcode != U_WEBSOCKET_OPCODE_CLOSE);
      }
      websocket_manager->closing = 1;
      pthread_mutex_unlock(&websocket_manager->read_lock);
    } else {
      ret = ulfius_websocket_send_message_nolock(websocket_manager, opcode, 1, data_len, data);
    }
    pthread_mutex_unlock(&websocket_manager->write_lock);
    return ret;
  } else {
    return U_ERROR_PARAMS;
  }
}

/**
 * Send a fragmented message in the websocket
 * each fragment size will be at most fragment_len
 * Return U_OK on success
 */
int ulfius_websocket_send_fragmented_message(struct _websocket_manager * websocket_manager,
                                  const uint8_t opcode,
                                  const uint64_t data_len,
                                  const char * data,
                  const size_t fragment_len) {
  size_t i = 0, cur_len;
  int ret = U_OK, count = WEBSOCKET_MAX_CLOSE_TRY, poll_ret;
  struct _websocket_message * message;
  
  if (websocket_manager != NULL && websocket_manager->connected && fragment_len > 0) {
    if (pthread_mutex_lock(&websocket_manager->write_lock)) {
      return U_ERROR;
    }
    while (i < data_len) {
      cur_len = fragment_len<(data_len - i)?fragment_len:(data_len - i);
      if (opcode == U_WEBSOCKET_OPCODE_CLOSE) {
        // If message sent is U_WEBSOCKET_OPCODE_CLOSE, wait for the response for 2 s max, then close the connection
        if (pthread_mutex_lock(&websocket_manager->read_lock)) {
          pthread_mutex_unlock(&websocket_manager->write_lock);
          return U_ERROR;
        }
        ret = ulfius_websocket_send_message_nolock(websocket_manager, opcode, 1, cur_len, (data + i));
        message = NULL;
        poll_ret = poll(&websocket_manager->fds, 1, U_WEBSOCKET_USEC_WAIT);
        if (poll_ret == -1) {
          y_log_message(Y_LOG_LEVEL_ERROR, "Error poll websocket read for close signal");
        } else if (!(websocket_manager->fds.revents & (POLLRDHUP|POLLERR|POLLHUP|POLLNVAL)) && poll_ret > 0) {
          do {
            if (ulfius_read_incoming_message(websocket_manager, &message) == U_OK) {
              while (count-- > 0) {
                if (ulfius_push_websocket_message(websocket_manager->message_list_incoming, message) != U_OK) {
                  y_log_message(Y_LOG_LEVEL_ERROR, "Error pushing new websocket message in list");
                }
              }
            }
          } while (message->opcode != U_WEBSOCKET_OPCODE_CLOSE);
        }
        websocket_manager->closing = 1;
        pthread_mutex_unlock(&websocket_manager->read_lock);
      } else {
        ret = ulfius_websocket_send_message_nolock(websocket_manager, opcode, ((i+fragment_len)>=data_len), data_len, data);
      }
      i += cur_len;
    }
    pthread_mutex_unlock(&websocket_manager->write_lock);
    return ret;
  }
  return ret;
}

/**
 * Send a message in the websocket without lock
 * Return U_OK on success
 */
int ulfius_websocket_send_message_nolock(struct _websocket_manager * websocket_manager,
                                         const uint8_t opcode,
                                         const short int fin,
                                         const uint64_t data_len,
                                         const char * data) {
  size_t frame_data_len;
  uint8_t * sent_data;
  int off;
  struct _websocket_message * my_message;
  if (websocket_manager != NULL &&
      websocket_manager->connected &&
     (
       opcode == U_WEBSOCKET_OPCODE_TEXT ||
       opcode == U_WEBSOCKET_OPCODE_BINARY ||
       opcode == U_WEBSOCKET_OPCODE_CLOSE ||
       opcode == U_WEBSOCKET_OPCODE_PING ||
       opcode == U_WEBSOCKET_OPCODE_PONG
     ) &&
     (data_len == 0 || data != NULL)) {
    frame_data_len = 2 + data_len;
    if (data_len > 65536) {
      frame_data_len += 8;
    } else if (data_len > 128) {
      frame_data_len += 2;
    }
    sent_data = o_malloc(frame_data_len + 1);
    my_message = o_malloc(sizeof(struct _websocket_message));
    if (sent_data != NULL && my_message != NULL) {
      if (data_len > 0) {
        my_message->data = o_malloc(data_len*sizeof(char));
        if (my_message->data == NULL) {
          o_free(sent_data);
          o_free(my_message);
          return U_ERROR_MEMORY;
        }
      }
      if (fin) {
        sent_data[0] = opcode|U_WEBSOCKET_BIT_FIN;
      }
      my_message->opcode = opcode;
      my_message->has_mask = 0;
      memset(my_message->mask, 0, 4);
      my_message->data_len = data_len;
      if (data_len > 65536) {
        sent_data[1] = 127;
        sent_data[2] = (uint8_t)(data_len >> 54);
        sent_data[3] = (uint8_t)(data_len >> 48);
        sent_data[4] = (uint8_t)(data_len >> 40);
        sent_data[5] = (uint8_t)(data_len >> 32);
        sent_data[6] = (uint8_t)(data_len >> 24);
        sent_data[7] = (uint8_t)(data_len >> 16);
        sent_data[8] = (uint8_t)(data_len >> 8);
        sent_data[9] = (uint8_t)(data_len);
        off = 10;
      } else if (data_len > 125) {
        sent_data[1] = 126;
        sent_data[2] = (uint8_t)(data_len >> 8);
        sent_data[3] = (uint8_t)(data_len);
        off = 4;
      } else {
        sent_data[1] = (uint8_t)data_len;
        off = 2;
      }
      if (data_len > 0) {
        memcpy(sent_data + off, data, data_len);
        memcpy(my_message->data, data, data_len);
      } else {
        my_message->data = NULL;
      }
      time(&my_message->datestamp);
      ulfius_websocket_send_all(websocket_manager->mhd_sock, sent_data, frame_data_len);
      if (ulfius_push_websocket_message(websocket_manager->message_list_outcoming, my_message) != U_OK) {
        y_log_message(Y_LOG_LEVEL_ERROR, "Error pushing new websocket message in list");
      }
      o_free(sent_data);
      return U_OK;
    } else {
      return U_ERROR_MEMORY;
    }
  } else {
    return U_ERROR_PARAMS;
  }
}

/**
 * Workaround to make sure a message, as long as it can be is complete sent
 */
void ulfius_websocket_send_all(MHD_socket sock, const uint8_t * data, size_t len) {
  ssize_t ret = 0, off;
  if (data != NULL && len > 0) {
    for (off = 0; (size_t)off < len; off += ret) {
      ret = send(sock, &data[off], len - off, MSG_NOSIGNAL);
      if (ret < 0) {
        break;
      }
    }
  }
}

/**
 * Return a match list between two list of items
 * If match is NULL, then return source duplicate
 * Returned value must be u_free'd after use
 */
char * ulfius_check_list_match(const char * source, const char * match, const char * separator) {
  char ** source_list = NULL, ** match_list = NULL;
  char * to_return = NULL;
  int i;
  if (match == NULL) {
    to_return = o_strdup(source);
  } else {
    if (source != NULL) {
      if (split_string(source, separator, &source_list) > 0 && split_string(match, separator, &match_list) > 0) {
        for (i=0; source_list[i] != NULL; i++) {
          if (string_array_has_trimmed_value((const char **)match_list, source_list[i])) {
            if (to_return == NULL) {
              to_return = o_strdup(trimwhitespace(source_list[i]));
            } else {
              char * tmp = msprintf("%s%s %s", to_return, separator, trimwhitespace(source_list[i]));
              o_free(to_return);
              to_return = tmp;
            }
          }
        }
        free_string_array(source_list);
        free_string_array(match_list);
      }
    }
  }
  return to_return;
}

/**
 * Return the first match between two list of items
 * If match is NULL, then return the first element of source
 * Returned value must be u_free'd after use
 */
char * ulfius_check_first_match(const char * source, const char * match, const char * separator) {
  char ** source_list = NULL, ** match_list = NULL;
  char * to_return = NULL;
  int i;
  if (match == NULL) {
    if (source != NULL) {
      if (split_string(source, separator, &source_list) > 0) {
        to_return = o_strdup(trimwhitespace(source_list[0]));
      }
      free_string_array(source_list);
    }
  } else {
    if (source != NULL) {
      if (split_string(source, separator, &source_list) > 0 && split_string(match, separator, &match_list) > 0) {
        for (i=0; source_list[i] != NULL && to_return == NULL; i++) {
          if (string_array_has_trimmed_value((const char **)match_list, source_list[i])) {
            if (to_return == NULL) {
              to_return = o_strdup(trimwhitespace(source_list[i]));
            }
          }
        }
        free_string_array(source_list);
        free_string_array(match_list);
      }
    }
  }
  return to_return;
}

/**
 * Close the websocket
 */
int ulfius_close_websocket(struct _websocket * websocket) {
  if (websocket != NULL && websocket->websocket_manager != NULL) {
    if (websocket->websocket_onclose_callback != NULL) {
      // Call websocket_onclose_callback if set
      websocket->websocket_onclose_callback(websocket->request, websocket->websocket_manager, websocket->websocket_onclose_user_data);
    }
    // If websocket is still open, send opcode 0x08 (close)
    if (websocket->websocket_manager->connected) {
      if (ulfius_websocket_send_message(websocket->websocket_manager, U_WEBSOCKET_OPCODE_CLOSE, 0, NULL) != U_OK) {
        y_log_message(Y_LOG_LEVEL_ERROR, "Error sending close frame to websocket");
      }
    }
    websocket->websocket_manager->connected = 0;
    return U_OK;
  } else {
    return U_ERROR_PARAMS;
  }
}

/**
 * Clear data of a websocket_manager
 */
void ulfius_clear_websocket_manager(struct _websocket_manager * websocket_manager) {
  if (websocket_manager != NULL) {
    pthread_mutex_destroy(&websocket_manager->read_lock);
    pthread_mutex_destroy(&websocket_manager->write_lock);
    ulfius_clear_websocket_message_list(websocket_manager->message_list_incoming);
    o_free(websocket_manager->message_list_incoming);
    websocket_manager->message_list_incoming = NULL;
    ulfius_clear_websocket_message_list(websocket_manager->message_list_outcoming);
    o_free(websocket_manager->message_list_outcoming);
    websocket_manager->message_list_outcoming = NULL;
  }
}

 /**
 * Add a websocket in the list of active websockets of the instance
 */
int ulfius_instance_add_websocket_active(struct _u_instance * instance, struct _websocket * websocket) {
  if (instance != NULL && websocket != NULL) {
    ((struct _websocket_handler *)instance->websocket_handler)->websocket_active = o_realloc(((struct _websocket_handler *)instance->websocket_handler)->websocket_active, (((struct _websocket_handler *)instance->websocket_handler)->nb_websocket_active+1)*sizeof(struct _websocket *));
    if (((struct _websocket_handler *)instance->websocket_handler)->websocket_active != NULL) {
      ((struct _websocket_handler *)instance->websocket_handler)->websocket_active[((struct _websocket_handler *)instance->websocket_handler)->nb_websocket_active] = websocket;
      ((struct _websocket_handler *)instance->websocket_handler)->nb_websocket_active++;
      return U_OK;
    } else {
      y_log_message(Y_LOG_LEVEL_ERROR, "Error allocating resources for instance->websocket_handler->websocket_active");
      return U_ERROR_MEMORY;
    }
  } else {
    return U_ERROR_PARAMS;
  }
}

/**
 * Remove a websocket from the list of active websockets of the instance
 */
int ulfius_instance_remove_websocket_active(struct _u_instance * instance, struct _websocket * websocket) {
  size_t i, j;
  if (instance != NULL && ((struct _websocket_handler *)instance->websocket_handler)->websocket_active != NULL && websocket != NULL) {
    for (i=0; i<((struct _websocket_handler *)instance->websocket_handler)->nb_websocket_active; i++) {
      if (((struct _websocket_handler *)instance->websocket_handler)->websocket_active[i] == websocket) {
        if (((struct _websocket_handler *)instance->websocket_handler)->nb_websocket_active > 1) {
          for (j=i; j<((struct _websocket_handler *)instance->websocket_handler)->nb_websocket_active-1; j++) {
            ((struct _websocket_handler *)instance->websocket_handler)->websocket_active[j] = ((struct _websocket_handler *)instance->websocket_handler)->websocket_active[j+1];
          }
          ((struct _websocket_handler *)instance->websocket_handler)->websocket_active = o_realloc(((struct _websocket_handler *)instance->websocket_handler)->websocket_active, (((struct _websocket_handler *)instance->websocket_handler)->nb_websocket_active-1)*sizeof(struct _websocket *));
          if (((struct _websocket_handler *)instance->websocket_handler)->websocket_active == NULL) {
            y_log_message(Y_LOG_LEVEL_ERROR, "Error allocating resources for instance->websocket_active");
            return U_ERROR_MEMORY;
          }
        } else {
          o_free(((struct _websocket_handler *)instance->websocket_handler)->websocket_active);
          ((struct _websocket_handler *)instance->websocket_handler)->websocket_active = NULL;
        }
        ((struct _websocket_handler *)instance->websocket_handler)->nb_websocket_active--;
        pthread_mutex_lock(&((struct _websocket_handler *)instance->websocket_handler)->websocket_close_lock);
        pthread_cond_broadcast(&((struct _websocket_handler *)instance->websocket_handler)->websocket_close_cond);
        pthread_mutex_unlock(&((struct _websocket_handler *)instance->websocket_handler)->websocket_close_lock);
        return U_OK;
      }
    }
    return U_ERROR_NOT_FOUND;
  } else {
    return U_ERROR_PARAMS;
  }
}

#define WEBSOCKET_RESPONSE_HTTP       0x0001
#define WEBSOCKET_RESPONSE_UPGRADE    0x0002
#define WEBSOCKET_RESPONSE_CONNECTION 0x0004
#define WEBSOCKET_RESPONSE_ACCEPT     0x0008
#define WEBSOCKET_RESPONSE_PROTCOL    0x0010
#define WEBSOCKET_RESPONSE_EXTENSION  0x0020

/**
 * Search for the length of the current reponse http line in buffer, starting at buffer_offset
 * If no end of line found, read the socket until an end of line is found
 */
static int ulfius_get_next_line_from_http_response(int sock, char ** buffer, size_t * buffer_len, size_t buffer_offset, size_t * line_len) {
  char read_buffer[512] = {0}, * end_line = NULL;
  int ret;
  size_t read_buffer_len = 0;
  
  if (*buffer != NULL && (end_line = o_strstr(*buffer+buffer_offset, "\r\n")) != NULL) {
    *line_len = (end_line-(*buffer+buffer_offset)) + 2;
    ret = U_OK;
  } else {
    // Read sock
    if ((read_buffer_len = recv(sock, read_buffer, 512, 0)) >= 0) {
      *buffer = o_realloc((*buffer), ((*buffer_len) + 512));
      if (*buffer != NULL) {
        memcpy((*buffer)+(*buffer_len), read_buffer, 512);
        *buffer_len += read_buffer_len;
        return ulfius_get_next_line_from_http_response(sock, buffer, buffer_len, buffer_offset, line_len);
      } else {
        y_log_message(Y_LOG_LEVEL_ERROR, "ulfius_get_next_line_from_http_response - Error allocating resources for *buffer");
        ret = U_ERROR;
      }
    } else {
      y_log_message(Y_LOG_LEVEL_ERROR, "ulfius_get_next_line_from_http_response - Error recv socket");
      ret = U_ERROR;
    }
  }
  
  return ret;
}

static int ulfius_open_websocket(struct _u_request * request, struct yuarel * y_url, struct _websocket * websocket) {
  int websocket_response_http = 0, i, ret;
  unsigned int websocket_response = 0;
  struct sockaddr_in server;
  struct hostent * he;
  char * http_line, * response = NULL;
  const char ** keys;
  size_t response_len = 0, response_offset = 0, line_len;
  
  websocket->websocket_manager->tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (websocket->websocket_manager->tcp_sock != -1) {
    if ((he = gethostbyname(y_url->host)) != NULL) {
      memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);
      server.sin_family = AF_INET;
      server.sin_port = htons(y_url->port);
      y_log_message(Y_LOG_LEVEL_DEBUG, "connect to %s:%d", y_url->host, y_url->port);
      
      if (connect(websocket->websocket_manager->tcp_sock, (struct sockaddr *)&server , sizeof(server)) >= 0) {
        // Send HTTP Request
        do {
          http_line = msprintf("%s %s HTTP/%s\r\n", request->http_verb, y_url->path, request->http_protocol);
          if (send(websocket->websocket_manager->tcp_sock, http_line, o_strlen(http_line), 0) < 0) {
            y_log_message(Y_LOG_LEVEL_ERROR, "Error send (1)");
            close(websocket->websocket_manager->tcp_sock);
            websocket->websocket_manager->tcp_sock = -1;
            ret = U_ERROR;
            break;
          }
          o_free(http_line);
          
          http_line = msprintf("Host: %s\r\n", y_url->host);
          if (send(websocket->websocket_manager->tcp_sock, http_line, o_strlen(http_line), 0) < 0) {
            y_log_message(Y_LOG_LEVEL_ERROR, "Error send (Host)");
            close(websocket->websocket_manager->tcp_sock);
            websocket->websocket_manager->tcp_sock = -1;
            ret = U_ERROR;
            break;
          }
          o_free(http_line);
          
          http_line = msprintf("Upgrade: websocket\r\n");
          if (send(websocket->websocket_manager->tcp_sock, http_line, o_strlen(http_line), 0) < 0) {
            y_log_message(Y_LOG_LEVEL_ERROR, "Error send (Upgrade)");
            close(websocket->websocket_manager->tcp_sock);
            websocket->websocket_manager->tcp_sock = -1;
            ret = U_ERROR;
            break;
          }
          o_free(http_line);
          
          http_line = msprintf("Connection: Upgrade\r\n");
          if (send(websocket->websocket_manager->tcp_sock, http_line, o_strlen(http_line), 0) < 0) {
            y_log_message(Y_LOG_LEVEL_ERROR, "Error send (Connection)");
            close(websocket->websocket_manager->tcp_sock);
            websocket->websocket_manager->tcp_sock = -1;
            ret = U_ERROR;
            break;
          }
          o_free(http_line);
          
          http_line = msprintf("Origin: %s://%s\r\n", y_url->scheme, y_url->host);
          if (send(websocket->websocket_manager->tcp_sock, http_line, o_strlen(http_line), 0) < 0) {
            y_log_message(Y_LOG_LEVEL_ERROR, "Error send (Origin)");
            close(websocket->websocket_manager->tcp_sock);
            websocket->websocket_manager->tcp_sock = -1;
            ret = U_ERROR;
            break;
          }
          o_free(http_line);
          
          keys = u_map_enum_keys(request->map_header);
          for (i=0; keys[i] != NULL; i++) {
            http_line = msprintf("%s: %s\r\n", keys[i], u_map_get(request->map_header, keys[i]));
            if (send(websocket->websocket_manager->tcp_sock , http_line , o_strlen(http_line) , 0) < 0) {
              y_log_message(Y_LOG_LEVEL_ERROR, "Error send header (%s)", keys[i]);
              close(websocket->websocket_manager->tcp_sock);
              websocket->websocket_manager->tcp_sock = -1;
              ret = U_ERROR;
              break;
            }
            o_free(http_line);
          }
          
          if (websocket->websocket_manager->tcp_sock >= 0) {
            // Send empty line
            const char * empty = "\r\n";
            if (send(websocket->websocket_manager->tcp_sock , empty , o_strlen(empty) , 0) < 0) {
              y_log_message(Y_LOG_LEVEL_ERROR, "Error send empty line");
              close(websocket->websocket_manager->tcp_sock);
              websocket->websocket_manager->tcp_sock = -1;
              ret = U_ERROR;
              break;
            }
          }
        } while (0);
        y_log_message(Y_LOG_LEVEL_DEBUG, "Send http request complete");
        
        // Read and parse response
        do {
          if (ulfius_get_next_line_from_http_response(websocket->websocket_manager->tcp_sock, &response, &response_len, response_offset, &line_len) == U_OK) {
            if (!websocket_response_http) {
              if (0 == o_strcmp((response + response_offset), "HTTP/1.1 101 Switching Protocols")) {
                websocket_response_http = 1;
                response_offset += line_len;
              } else {
                y_log_message(Y_LOG_LEVEL_DEBUG, "HTTP Response error: %.*s", line_len, (response + response_offset));
                break;
              }
            } else if (websocket_response_http) {
              if (0 == o_strcmp((response + response_offset), "Upgrade: websocket\r\n")) {
                websocket_response |= WEBSOCKET_RESPONSE_UPGRADE;
                response_offset += line_len;
              } else if (0 == o_strcmp((response + response_offset), "Connection: Upgrade\r\n")) {
                websocket_response |= WEBSOCKET_RESPONSE_CONNECTION;
                response_offset += line_len;
              } else if (0 == o_strcmp((response + response_offset), "Sec-WebSocket-Protocol")) {
                response_offset += line_len;
                websocket->websocket_manager->protocol = o_strndup(response + response_offset, o_strstr(response + response_offset, "\r\n") - (response + response_offset));
                websocket_response |= WEBSOCKET_RESPONSE_PROTCOL;
              } else if (0 == o_strcmp((response + response_offset), "Sec-WebSocket-Extension")) {
                response_offset += line_len;
                websocket->websocket_manager->extension = o_strndup(response + response_offset, o_strstr(response + response_offset, "\r\n") - (response + response_offset));
                websocket_response |= WEBSOCKET_RESPONSE_EXTENSION;
              } else if (0 == o_strcmp((response + response_offset), "Sec-WebSocket-Accept")) {
                response_offset += line_len;
                // TODO: Check handshake
                websocket_response |= WEBSOCKET_RESPONSE_ACCEPT;
              } else if (0 == o_strcmp((response + response_offset), "\r\n")) {
                // Websocket HTTP response complete
                break;
              }
            } else if (!line_len) {
              y_log_message(Y_LOG_LEVEL_ERROR, "Error reading line");
              close(websocket->websocket_manager->tcp_sock);
              websocket->websocket_manager->tcp_sock = -1;
              ret = U_ERROR;
              break;
            }
          } else {
            y_log_message(Y_LOG_LEVEL_ERROR, "Error ulfius_get_next_line_from_http_response, abort parsing response");
            close(websocket->websocket_manager->tcp_sock);
            websocket->websocket_manager->tcp_sock = -1;
            ret = U_ERROR;
            break;
          }
        } while (0);
        
        if (websocket->websocket_manager->tcp_sock > -1 && !(websocket_response & (WEBSOCKET_RESPONSE_UPGRADE|WEBSOCKET_RESPONSE_CONNECTION|WEBSOCKET_RESPONSE_PROTCOL|WEBSOCKET_RESPONSE_EXTENSION|WEBSOCKET_RESPONSE_ACCEPT))) {
          y_log_message(Y_LOG_LEVEL_ERROR, "Websocket HTTP response incomplete or incorrect, aborting");
          close(websocket->websocket_manager->tcp_sock);
          websocket->websocket_manager->tcp_sock = -1;
          ret = U_ERROR;
        } else if (websocket->websocket_manager->tcp_sock == -1) {
          y_log_message(Y_LOG_LEVEL_ERROR, "Socket closed");
          ret = U_ERROR;
        } else {
          ret = U_OK;
        }
      } else {
        y_log_message(Y_LOG_LEVEL_ERROR, "Error connecting socket");
        close(websocket->websocket_manager->tcp_sock);
        websocket->websocket_manager->tcp_sock = -1;
        ret = U_ERROR;
      }
    } else {
      y_log_message(Y_LOG_LEVEL_ERROR, "Error gethostbyname");
      ret = U_ERROR;
    }
  } else {
    y_log_message(Y_LOG_LEVEL_ERROR, "Error opening socket");
    ret = U_ERROR;
  }
  return ret;
}

/**
 * Initialize values for a struct _u_request to open a websocket
 * request must be previously initialized
 * Return U_OK on success
 */
int ulfius_init_websocket_request(struct _u_request * request,
                                  const char * url,
                                  const char * websocket_protocol,
                                  const char * websocket_extensions) {
  int ret;
  if (request != NULL && url != NULL) {
    o_free(request->http_protocol);
    o_free(request->http_verb);
    o_free(request->http_url);
    request->http_protocol = o_strdup("1.1");
    request->http_verb = o_strdup("GET");
    request->http_url = o_strdup(url);
    if (websocket_protocol != NULL) {
      u_map_put(request->map_header, "Sec-WebSocket-Protocol", websocket_protocol);
    }
    if (websocket_extensions != NULL) {
      u_map_put(request->map_header, "Sec-WebSocket-Extensions", websocket_extensions);
    }
    u_map_put(request->map_header, "Sec-WebSocket-Version", "13");
    u_map_put(request->map_header, "Upgrade", "websocket");
    u_map_put(request->map_header, "Connection", "Upgrade");
    u_map_put(request->map_header, "Sec-WebSocket-Key", "x3JJHMbDL1EzLkh9GBhXDw=="); // TODO init key with a more random value
    ret = U_OK;
  } else {
    y_log_message(Y_LOG_LEVEL_ERROR, "ulfius_init_websocket_request error input parameters");
    ret = U_ERROR;
  }
  return ret;
}

/**
 * Open a websocket client connection
 * Return U_OK on success
 */
int ulfius_open_websocket_client_connection(struct _u_request * request,
                                            void (* websocket_manager_callback) (const struct _u_request * request,
                                                                                 struct _websocket_manager * websocket_manager,
                                                                                 void * websocket_manager_user_data),
                                            void * websocket_manager_user_data,
                                            void (* websocket_incoming_message_callback) (const struct _u_request * request,
                                                                                          struct _websocket_manager * websocket_manager,
                                                                                          const struct _websocket_message * message,
                                                                                          void * websocket_incoming_user_data),
                                            void * websocket_incoming_user_data,
                                            void (* websocket_onclose_callback) (const struct _u_request * request,
                                                                                 struct _websocket_manager * websocket_manager,
                                                                                 void * websocket_onclose_user_data),
                                            void * websocket_onclose_user_data) {
  int ret;
  struct yuarel y_url;
  char * url, * basic_auth_encoded_header, * basic_auth, * basic_auth_encoded;
  size_t basic_auth_encoded_len;
  struct _websocket_manager * websocket_manager;
  struct _websocket * websocket;
  pthread_t thread_websocket;
  int thread_ret_websocket = 0, thread_detach_websocket = 0;
  
  if (request != NULL && (websocket_manager_callback != NULL || websocket_incoming_message_callback != NULL)) {
    url = o_strdup(request->http_url);
    if (!yuarel_parse(&y_url, url)) {
      if (0 == o_strcasecmp("http", y_url.scheme) || 0 == o_strcasecmp("https", y_url.scheme) || 0 == o_strcasecmp("ws", y_url.scheme) || 0 == o_strcasecmp("wss", y_url.scheme)) {
        if (!y_url.port) {
          if (0 == o_strcasecmp("http", y_url.scheme) || 0 == o_strcasecmp("ws", y_url.scheme)) {
            y_url.port = 80;
          } else {
            y_url.port = 443;
          }
        }
        if (y_url.username != NULL && y_url.password != NULL) {
          basic_auth = msprintf("%s:%s", y_url.username, y_url.password);
          basic_auth_encoded = o_malloc((o_strlen(basic_auth)*4/3)+1);
          memset(basic_auth_encoded, 0, (o_strlen(basic_auth)*4/3)+1);
          if (o_base64_encode((const unsigned char *)basic_auth, o_strlen(basic_auth), (unsigned char *)basic_auth_encoded, &basic_auth_encoded_len)) {
            basic_auth_encoded_header = msprintf("Basic: %s", basic_auth_encoded);
            u_map_remove_from_key(request->map_header, "Authorization");
            u_map_put(request->map_header, "Authorization", basic_auth_encoded_header);
            o_free(basic_auth_encoded_header);
            o_free(basic_auth_encoded);
          } else {
            y_log_message(Y_LOG_LEVEL_ERROR, "Error o_base64_encode");
          }
          o_free(basic_auth);
        }
        
        websocket_manager = o_malloc(sizeof(struct _websocket_manager));
        websocket = o_malloc(sizeof(struct _websocket));
        if (websocket_manager != NULL && websocket != NULL) {
          websocket->websocket_manager = websocket_manager;
          websocket->websocket_manager->type = U_WEBSOCKET_CLIENT;
          websocket->websocket_manager_callback = websocket_manager_callback;
          websocket->websocket_manager_user_data = websocket_manager_user_data;
          websocket->websocket_incoming_message_callback = websocket_incoming_message_callback;
          websocket->websocket_incoming_user_data = websocket_incoming_user_data;
          websocket->websocket_onclose_callback = websocket_onclose_callback;
          websocket->websocket_onclose_user_data = websocket_onclose_user_data;
          // Open connection
          if (0 == o_strcasecmp("http", y_url.scheme) || 0 == o_strcasecmp("ws", y_url.scheme)) {
            websocket->tls = 0;
            if (ulfius_open_websocket(request, &y_url, websocket) != U_OK) {
              y_log_message(Y_LOG_LEVEL_ERROR, "Error ulfius_open_websocket");
              ret = U_ERROR;
            }
          } else {
            // TODO: Use gnutls secure connection
            ret = U_ERROR;
          }
          thread_ret_websocket = pthread_create(&thread_websocket, NULL, ulfius_thread_websocket, (void *)websocket);
          thread_detach_websocket = pthread_detach(thread_websocket);
          if (thread_ret_websocket || thread_detach_websocket) {
            y_log_message(Y_LOG_LEVEL_ERROR, "Error creating or detaching websocket manager thread, return code: %d, detach code: %d",
                          thread_ret_websocket, thread_detach_websocket);
            ulfius_clear_websocket(websocket);
          }
        } else {
          y_log_message(Y_LOG_LEVEL_ERROR, "Error allocating resources for websocket_manager or websocket");
          ret = U_ERROR_PARAMS;
        }
      } else {
        y_log_message(Y_LOG_LEVEL_ERROR, "unknown scheme, please use one of the following: 'http', 'https', 'ws', 'wss'");
        ret = U_ERROR_PARAMS;
      }
      y_log_message(Y_LOG_LEVEL_DEBUG, "That's cool");
      ret = U_OK;
    } else {
      y_log_message(Y_LOG_LEVEL_ERROR, "Error parsing url");
      ret = U_ERROR_PARAMS;
    }
    o_free(url);
  } else {
    y_log_message(Y_LOG_LEVEL_DEBUG, "Error param");
    ret = U_ERROR_PARAMS;
  }
  return ret;
}

#endif
