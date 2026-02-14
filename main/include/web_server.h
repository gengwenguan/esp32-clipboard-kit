#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"

/**
 * @brief Start the web server
 * @return httpd_handle_t Handle to the started server, or NULL on error
 */
httpd_handle_t start_webserver(void);

#endif // WEB_SERVER_H
