/**
 * 
 * Ulfius Framework
 * 
 * REST framework library
 * 
 * ulfius.h: structures and functions declarations
 * 
 * Copyright 2015 Nicolas Mora <mail@babelouest.org>
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

#ifndef __ULFIUS_H__
#define __ULFIUS_H__

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <microhttpd.h>
#include <jansson.h>
#include <curl/curl.h>

/** Angharad libraries **/
#include <yder.h>
#include <orcania.h>

#define ULFIUS_URL_SEPARATOR       "/"
#define ULFIUS_HTTP_ENCODING_JSON  "application/json"
#define ULFIUS_HTTP_HEADER_CONTENT "Content-Type"
#define ULFIUS_HTTP_NOT_FOUND_BODY "Resource not found"
#define ULFIUS_HTTP_ERROR_BODY     "Server Error"

#define ULFIUS_CALLBACK_RESPONSE_OK    0
#define ULFIUS_CALLBACK_RESPONSE_ERROR 1

#define ULFIUS_COOKIE_ATTRIBUTE_EXPIRES  "Expires"
#define ULFIUS_COOKIE_ATTRIBUTE_MAX_AGE  "Max-Age"
#define ULFIUS_COOKIE_ATTRIBUTE_DOMAIN   "Domain"
#define ULFIUS_COOKIE_ATTRIBUTE_PATH     "Path"
#define ULFIUS_COOKIE_ATTRIBUTE_SECURE   "Secure"
#define ULFIUS_COOKIE_ATTRIBUTE_HTTPONLY "HttpOnly"

#define ULFIUS_POSTBUFFERSIZE 1024

#define U_OK              0 // No error
#define U_ERROR           1 // Error
#define U_ERROR_MEMORY    2 // Error in memory allocation
#define U_ERROR_PARAMS    3 // Error in input parameters
#define U_ERROR_LIBMHD    4 // Error in libmicrohttpd execution
#define U_ERROR_LIBCURL   5 // Error in libcurl execution
#define U_ERROR_NOT_FOUND 6 // Something was not found

#define ULFIUS_VERSION 0.9.8

/*************
 * Structures
 *************/

/**
 * struct _u_map
 */
struct _u_map {
  int nb_values;
  char ** keys;
  char ** values;
};

/**
 * struct _u_cookie
 * the structure containing the response cookie parameters
 */
struct _u_cookie {
  char * key;
  char * value;
  char * expires;
  uint max_age;
  char * domain;
  char * path;
  int secure;
  int http_only;
};

/**
 * 
 * Structure of an instance
 * 
 * Contains the needed data for an ulfius instance to work
 * 
 * mhd_daemon:   pointer to the libmicrohttpd daemon
 * port:         port number to listen to
 * bind_address: ip address to listen to (if needed)
 * 
 */
struct _u_instance {
  struct MHD_Daemon * mhd_daemon;
  int port;
  struct sockaddr_in * bind_address;
};

/**
 * 
 * Structure of request parameters
 * 
 * Contains request data
 * http_verb:          http method (GET, POST, PUT, DELETE, etc.), use '*' to match all http methods
 * http_url:           url used to call this callback function or full url to call when used in a ulfius_send_http_request
 * client_address:     IP address of the client
 * map_url:            map containing the url variables, both from the route and the ?key=value variables
 * map_header:         map containing the header variables
 * map_cookie:         map containing the cookie variables
 * map_post_body:      map containing the post body variables (if available)
 * json_body:          json_t * object containing the json body (if available)
 * json_error:            stack allocated json_error_t if json body was not parsed (if available)
 * json_has_error:     true if the json body was not parsed by jansson (if available)
 * binary_body:        pointer to raw body
 * binary_body_length: length of raw body
 * 
 */
struct _u_request {
  char *               http_verb;
  char *               http_url;
  struct sockaddr *    client_address;
  struct _u_map *      map_url;
  struct _u_map *      map_header;
  struct _u_map *      map_cookie;
  struct _u_map *      map_post_body;
  json_t *             json_body;
  json_error_t *       json_error;
  int                  json_has_error;
  void *               binary_body;
  size_t               binary_body_length;
};

/**
 * 
 * Structure of response parameters
 * 
 * Contains response data that must be set by the user
 * status:             HTTP status code (200, 404, 500, etc)
 * protocol:           HTTP Protocol sent
 * map_header:         map containing the header variables
 * nb_cookies:         number of cookies sent
 * map_cookie:         array of cookies sent
 * string_body:        a char * containing the raw body response
 * json_body:          a json_t * object containing the json response
 * binary_body:        a void * containing a raw binary content
 * binary_body_length: the length of the binary_body
 * 
 */
struct _u_response {
  long               status;
  char             * protocol;
  struct _u_map    * map_header;
  unsigned int       nb_cookies;
  struct _u_cookie * map_cookie;
  char             * string_body;
  json_t           * json_body;
  void             * binary_body;
  unsigned int       binary_body_length;
};

/**
 * 
 * Structure of an endpoint
 * 
 * Contains all informations needed for an endpoint
 * http_method:       http verb (GET, POST, PUT, etc.) in upper case
 * url_prefix:        prefix for the url (optional)
 * url_format:        string used to define the endpoint format
 *                    separate words with /
 *                    to define a variable in the url, prefix it with @ or :
 *                    example: /test/resource/:name/elements
 *                    on an url_format that ends with '*', the rest of the url will not be tested
 * callback_function: a pointer to a function that will be executed each time the endpoint is called
 *                    you must declare the function as described.
 * user_data:         a pointer to a data or a structure that will be available in the callback function
 * 
 */
struct _u_endpoint {
  char * http_method;
  char * url_prefix;
  char * url_format;
  int (* callback_function)(const struct _u_request * request, // Input parameters (set by the framework)
                            struct _u_response * response,     // Output parameters (set by the user)
                            void * user_data);
  void * user_data;
};

/**
 * Structures used to facilitate data manipulations (internal)
 */
struct connection_info_struct {
  struct MHD_PostProcessor * post_processor;
  int                        has_post_processor;
  int                        callback_first_iteration;
  struct _u_request *        request;
};

/********************************
 * Public functions declarations
 ********************************/

/**
 * ulfius_init_framework
 * Initializes the framework and run the webservice based on the parameters given
 * return truze if no error
 * 
 * u_instance:    pointer to a struct _u_instance that describe its port and bind address
 * endpoint_list: array of struct _u_endpoint that will describe endpoints used for the application
 *                the array MUST have an empty struct _u_endpoint at the end of it
 *                {NULL, NULL, NULL, NULL, NULL}
 * return U_OK on success
 */
int ulfius_init_framework(struct _u_instance * u_instance, struct _u_endpoint * endpoint_list);

/**
 * ulfius_stop_framework
 * 
 * Stop the webservice
 * u_instance:    pointer to a struct _u_instance that describe its port and bind address
 * return U_OK on success
 */
int ulfius_stop_framework(struct _u_instance * u_instance);

/**
 * ulfius_add_cookie_to_header
 * add a cookie to the cookie map
 * return U_OK on success
 */
int ulfius_add_cookie_to_response(struct _u_response * response, const char * key, const char * value, const char * expires, const uint max_age, 
                      const char * domain, const char * path, const int secure, const int http_only);

/**
 * ulfius_send_http_request
 * Send a HTTP request and store the result into a _u_response
 * return U_OK on success
 */
int ulfius_send_http_request(const struct _u_request * request, struct _u_response * response);

/**
 * ulfius_init_request
 * Initialize a request structure by allocating inner elements
 * return U_OK on success
 */
int ulfius_init_request(struct _u_request * request);

/**
 * ulfius_clean_request
 * clean the specified request's inner elements
 * user must free the parent pointer if needed after clean
 * or use ulfius_clean_request_full
 * return U_OK on success
 */
int ulfius_clean_request(struct _u_request * request);

/**
 * ulfius_clean_request_full
 * clean the specified request and all its elements
 * return U_OK on success
 */
int ulfius_clean_request_full(struct _u_request * request);

/**
 * ulfius_init_response
 * Initialize a response structure by allocating inner elements
 * return U_OK on success
 */
int ulfius_init_response(struct _u_response * response);

/**
 * ulfius_clean_response
 * clean the specified response's inner elements
 * user must free the parent pointer if needed after clean
 * or use ulfius_clean_response_full
 * return U_OK on success
 */
int ulfius_clean_response(struct _u_response * response);

/**
 * ulfius_clean_response_full
 * clean the specified response and all its elements
 * return U_OK on success
 */
int ulfius_clean_response_full(struct _u_response * response);

/**
 * ulfius_copy_response
 * Copy the source response elements into the des response
 * return U_OK on success
 */
int ulfius_copy_response(struct _u_response * dest, const struct _u_response * source);

/**
 * ulfius_clean_cookie
 * clean the cookie's elements
 * return U_OK on success
 */
int ulfius_clean_cookie(struct _u_cookie * cookie);

/**
 * Copy the cookie source elements into dest elements
 * return U_OK on success
 */
int ulfius_copy_cookie(struct _u_cookie * dest, const struct _u_cookie * source);

/**
 * create a new request based on the source elements
 * returned value must be cleaned after use
 */
struct _u_request * ulfius_duplicate_request(const struct _u_request * request);

/**
 * create a new response based on the source elements
 * return value must be cleaned after use
 */
struct _u_response * ulfius_duplicate_response(const struct _u_response * response);

/**
 * Send an email using libcurl
 * email is plain/text and UTF8 charset
 * host: smtp server host name
 * port: tcp port number (optional, 0 for default)
 * use_tls: true if the connection is tls secured
 * verify_certificate: true if you want to disable the certificate verification on a tls server
 * user: connection user name (optional, NULL: no user name)
 * password: connection password (optional, NULL: no password)
 * from: from address (mandatory)
 * to: to recipient address (mandatory)
 * cc: cc recipient address (optional, NULL: no cc)
 * bcc: bcc recipient address (optional, NULL: no bcc)
 * subject: email subject (mandatory)
 * mail_body: email body (mandatory)
 * return U_OK on success
 */
int ulfius_send_smtp_email(const char * host, 
                            const int port, 
                            const int use_tls, 
                            const int verify_certificate, 
                            const char * user, 
                            const char * password, 
                            const char * from, 
                            const char * to, 
                            const char * cc, 
                            const char * bcc, 
                            const char * subject, 
                            const char * mail_body);

/**
 * generate_endpoint
 * return a pointer to an allocated endpoint
 * returned value must be free'd after use
 */
int generate_endpoint(struct _u_endpoint * endpoint, const char * http_method, const char * url_prefix, const char * url_format, int (* callback_function)(const struct _u_request * request, struct _u_response * response, void * user_data), void * user_data);

/**
 * copy_endpoint
 * return a copy of an endpoint with duplicate values
 * returned value must be free'd after use
 */
int copy_endpoint(struct _u_endpoint * source, struct _u_endpoint * dest);

/**
 * copy_endpoint_list
 * return a copy of an endpoint list with duplicate values
 * returned value must be free'd after use
 */
struct _u_endpoint * duplicate_endpoint_list(struct _u_endpoint * endpoint_list);

/**
 * clean_endpoint
 * free allocated memory by an endpoint
 */
void clean_endpoint(struct _u_endpoint * endpoint);

/**
 * clean_endpoint_list
 * free allocated memory by an endpoint list
 */
void clean_endpoint_list(struct _u_endpoint * endpoint_list);

/**
 * umap declarations
 * umap is a simple map structure that handles sets of key/value maps
 * 
 * Be careful, umap is VERY memory unfriendly, every pointer returned by the functions must be freed after use
 * 
 */

/**
 * initialize a struct _u_map
 * this function MUST be called after a declaration or allocation
 * return U_OK on success
 */
int u_map_init(struct _u_map * map);

/**
 * free the struct _u_map's inner components
 * return U_OK on success
 */
int u_map_clean(struct _u_map * u_map);

/**
 * free the struct _u_map and its components
 * return U_OK on success
 */
int u_map_clean_full(struct _u_map * u_map);

/**
 * free an enum return by functions u_map_enum_keys or u_map_enum_values
 * return U_OK on success
 */
int u_map_clean_enum(char ** array);

/**
 * returns an array containing all the keys in the struct _u_map
 * return an array of char * ending with a NULL element
 */
const char ** u_map_enum_keys(const struct _u_map * u_map);

/**
 * returns an array containing all the values in the struct _u_map
 * return an array of char * ending with a NULL element
 */
const char ** u_map_enum_values(const struct _u_map * u_map);

/**
 * return true if the sprcified u_map contains the specified key
 * false otherwise
 * search is case sensitive
 */
int u_map_has_key(const struct _u_map * u_map, const char * key);

/**
 * return true if the sprcified u_map contains the specified value
 * false otherwise
 * search is case sensitive
 */
int u_map_has_value(const struct _u_map * u_map, const char * value);

/**
 * return true if the sprcified u_map contains the specified key
 * false otherwise
 * search is case insensitive
 */
int u_map_has_key_case(const struct _u_map * u_map, const char * key);

/**
 * return true if the sprcified u_map contains the specified value
 * false otherwise
 * search is case insensitive
 */
int u_map_has_value_case(const struct _u_map * u_map, const char * value);

/**
 * add the specified key/value pair into the specified u_map
 * if the u_map already contains a pair with the same key, replace the value
 * return U_OK on success
 */
int u_map_put(struct _u_map * u_map, const char * key, const char * value);

/**
 * get the value corresponding to the specified key in the u_map
 * return NULL if no match found
 * search is case sensitive
 */
const char * u_map_get(const struct _u_map * u_map, const const char * key);

/**
 * get the value corresponding to the specified key in the u_map
 * return NULL if no match found
 * search is case insensitive
 */
const char * u_map_get_case(const struct _u_map * u_map, const char * key);

/**
 * remove an pair key/value that has the specified key
 * return U_OK on success, U_NOT_FOUND if key was not found, error otherwise
 */
int u_map_remove_from_key(struct _u_map * u_map, const char * key);

/**
 * remove all pairs key/value that has the specified key (case insensitive search)
 * return U_OK on success, U_NOT_FOUND if key was not found, error otherwise
 */
int u_map_remove_from_key_case(struct _u_map * u_map, const char * key);

/**
 * remove all pairs key/value that has the specified value
 * return U_OK on success, U_NOT_FOUND if key was not found, error otherwise
 */
int u_map_remove_from_value(struct _u_map * u_map, const char * key);

/**
 * remove all pairs key/value that has the specified value (case insensitive search)
 * return U_OK on success, U_NOT_FOUND if key was not found, error otherwise
 */
int u_map_remove_from_value_case(struct _u_map * u_map, const char * key);

/**
 * remove the pair key/value at the specified index
 * return U_OK on success, U_NOT_FOUND if index is out of bound, error otherwise
 */
int u_map_remove_at(struct _u_map * u_map, const int index);

/**
 * Create an exact copy of the specified struct _u_map
 * return a reference to the copy, NULL otherwise
 * returned value must be free'd after use
 */
struct _u_map * u_map_copy(const struct _u_map * source);

/**
 * Return the number of key/values pair in the specified struct _u_map
 * Return -1 on error
 */
int u_map_count(const struct _u_map * source);

/**********************************
 * Internal functions declarations
 **********************************/

/**
 * validate_instance
 * return true if u_instance has valid parameters, false otherwise
 */
int validate_instance(const struct _u_instance * u_instance);

/**
 * validate_endpoint_list
 * return true if endpoint_list has valid parameters, false otherwise
 */
int validate_endpoint_list(const struct _u_endpoint * endpoint_list);

/**
 * ulfius_webservice_dispatcher
 * function executed by libmicrohttpd every time an HTTP call is made
 * return MHD_NO on error
 */
int ulfius_webservice_dispatcher (void *cls, struct MHD_Connection *connection,
                                  const char *url, const char *method,
                                  const char *version, const char *upload_data,
                                  size_t *upload_data_size, void **con_cls);
/**
 * iterate_post_data
 * function used to iterate post parameters
 * return MHD_NO on error
 */
int iterate_post_data (void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
                      const char *filename, const char *content_type,
                      const char *transfer_encoding, const char *data, uint64_t off,
                      size_t size);

/**
 * request_completed
 * function used to clean data allocated after a web call is complete
 */
void request_completed (void *cls, struct MHD_Connection *connection,
                        void **con_cls, enum MHD_RequestTerminationCode toe);

/**
 * split_url
 * return an array of char based on the url words
 * returned value must be free'd after use
 */
char ** split_url(const char * prefix, const char * url);

/**
 * endpoint_match
 * return the endpoint matching the url called with the proper http method
 * return NULL if no endpoint is found
 */
struct _u_endpoint * endpoint_match(const char * method, const char * url, struct _u_endpoint * endpoint_list);

/**
 * url_format_match
 * return true if splitted_url matches splitted_url_format
 * false otherwise
 */
int url_format_match(const char ** splitted_url, const char ** splitted_url_format);

/**
 * parse_url
 * fills map with the keys/values defined in the url that are described in the endpoint format url
 * return U_OK on success
 */
int parse_url(const char * url, const struct _u_endpoint * endpoint, struct _u_map * map);

/**
 * set_response_header
 * adds headers defined in the response_map_header to the response
 * return the number of added headers, -1 on error
 */
int set_response_header(struct MHD_Response * response, const struct _u_map * response_map_header);

/**
 * set_response_cookie
 * adds cookies defined in the response_map_cookie
 * return the number of added headers, -1 on error
 */
int set_response_cookie(struct MHD_Response * mhd_response, const struct _u_response * response);

/**
 * Add a cookie in the cookie map as defined in the RFC 6265
 * Returned value must be free'd after use
 */
char * get_cookie_header(const struct _u_cookie * cookie);

#endif // __ULFIUS_H__
