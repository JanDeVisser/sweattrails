#include "wahoo_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <curl/curl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/evp.h>

#define WAHOO_CONFIG_PATH "/.config/sweattrails/wahoo_config"
#define WAHOO_AUTH_URL "https://api.wahooligan.com/oauth/authorize"
#define WAHOO_TOKEN_URL "https://api.wahooligan.com/oauth/token"
#define WAHOO_API_URL "https://api.wahooligan.com/v1"
#define CALLBACK_PORT 8090
#define REDIRECT_URI "https://localhost:8090/callback"

// Simple JSON parsing helpers (same pattern as strava_api.c)
static const char *json_find_key(const char *json, const char *key) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = json;

    while ((pos = strstr(pos, search)) != NULL) {
        int depth = 0;
        for (const char *p = json; p < pos; p++) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
        }
        if (depth <= 1) {
            return pos;
        }
        pos++;
    }
    return NULL;
}

static bool json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;

    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;
    if (*pos != '"') return false;
    pos++;

    size_t i = 0;
    while (*pos && *pos != '"' && i < out_size - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
        }
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return true;
}

static bool json_get_int64(const char *json, const char *key, int64_t *out) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;

    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;

    char *end;
    *out = strtoll(pos, &end, 10);
    return end != pos;
}

static bool json_get_int(const char *json, const char *key, int *out) {
    int64_t val;
    if (json_get_int64(json, key, &val)) {
        *out = (int)val;
        return true;
    }
    return false;
}

static bool json_get_float(const char *json, const char *key, float *out) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;

    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;

    char *end;
    *out = strtof(pos, &end);
    return end != pos;
}

// Curl write callback
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} CurlBuffer;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    CurlBuffer *buf = (CurlBuffer *)userp;

    if (buf->size + realsize + 1 > buf->capacity) {
        size_t new_capacity = buf->capacity == 0 ? 4096 : buf->capacity * 2;
        while (new_capacity < buf->size + realsize + 1) {
            new_capacity *= 2;
        }
        char *new_data = realloc(buf->data, new_capacity);
        if (!new_data) return 0;
        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';

    return realsize;
}

static void curl_buffer_free(CurlBuffer *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

bool wahoo_load_config(WahooConfig *config) {
    memset(config, 0, sizeof(WahooConfig));

    const char *home = getenv("HOME");
    if (!home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, WAHOO_CONFIG_PATH);

    FILE *f = fopen(path, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json = malloc(size + 1);
    if (!json) {
        fclose(f);
        return false;
    }

    fread(json, 1, size, f);
    json[size] = '\0';
    fclose(f);

    json_get_string(json, "client_id", config->client_id, sizeof(config->client_id));
    json_get_string(json, "client_secret", config->client_secret, sizeof(config->client_secret));
    json_get_string(json, "access_token", config->access_token, sizeof(config->access_token));
    json_get_string(json, "refresh_token", config->refresh_token, sizeof(config->refresh_token));

    int64_t expires;
    if (json_get_int64(json, "token_expires_at", &expires)) {
        config->token_expires_at = (time_t)expires;
    }

    free(json);

    return config->client_id[0] && config->client_secret[0];
}

bool wahoo_save_config(const WahooConfig *config) {
    const char *home = getenv("HOME");
    if (!home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, WAHOO_CONFIG_PATH);

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "{\n");
    fprintf(f, "  \"client_id\": \"%s\",\n", config->client_id);
    fprintf(f, "  \"client_secret\": \"%s\",\n", config->client_secret);
    fprintf(f, "  \"access_token\": \"%s\",\n", config->access_token);
    fprintf(f, "  \"refresh_token\": \"%s\",\n", config->refresh_token);
    fprintf(f, "  \"token_expires_at\": %ld\n", (long)config->token_expires_at);
    fprintf(f, "}\n");

    fclose(f);
    return true;
}

bool wahoo_is_authenticated(const WahooConfig *config) {
    if (!config->access_token[0]) return false;
    if (!config->refresh_token[0]) return false;
    return true;
}

static bool parse_token_response(const char *json, WahooConfig *config) {
    if (!json_get_string(json, "access_token", config->access_token, sizeof(config->access_token))) {
        return false;
    }
    if (!json_get_string(json, "refresh_token", config->refresh_token, sizeof(config->refresh_token))) {
        return false;
    }

    int expires_in = 0;
    if (json_get_int(json, "expires_in", &expires_in)) {
        config->token_expires_at = time(NULL) + expires_in;
    }

    return true;
}

// Create SSL context using mkcert-generated certificates
static SSL_CTX *create_ssl_context_with_cert(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        fprintf(stderr, "Failed to create SSL context\n");
        return NULL;
    }

    // Load mkcert certificate and key from ~/.config/sweattrails/certs/
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "HOME not set\n");
        SSL_CTX_free(ctx);
        return NULL;
    }

    char cert_path[512], key_path[512];
    snprintf(cert_path, sizeof(cert_path), "%s/.config/sweattrails/certs/localhost+1.pem", home);
    snprintf(key_path, sizeof(key_path), "%s/.config/sweattrails/certs/localhost+1-key.pem", home);

    if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "Failed to load certificate from %s\n", cert_path);
        fprintf(stderr, "Run: mkcert -install && mkdir -p ~/.config/sweattrails/certs && cd ~/.config/sweattrails/certs && mkcert localhost 127.0.0.1\n");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "Failed to load private key from %s\n", key_path);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    return ctx;
}

bool wahoo_authenticate(WahooConfig *config) {
    // Create authorization URL
    char auth_url[1024];
    snprintf(auth_url, sizeof(auth_url),
             "%s?client_id=%s&redirect_uri=%s&response_type=code&scope=workouts_read",
             WAHOO_AUTH_URL, config->client_id, REDIRECT_URI);

    printf("Opening browser for Wahoo authorization...\n");
    printf("If browser doesn't open, visit:\n%s\n\n", auth_url);
    fflush(stdout);

    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();

    // Create SSL context with self-signed cert
    SSL_CTX *ssl_ctx = create_ssl_context_with_cert();
    if (!ssl_ctx) {
        return false;
    }

    // Open browser
    char cmd[1200];
#ifdef __APPLE__
    snprintf(cmd, sizeof(cmd), "open '%s'", auth_url);
#else
    snprintf(cmd, sizeof(cmd), "xdg-open '%s'", auth_url);
#endif
    system(cmd);

    // Start local server to receive callback
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        SSL_CTX_free(ssl_ctx);
        return false;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(CALLBACK_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        SSL_CTX_free(ssl_ctx);
        return false;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        close(server_fd);
        SSL_CTX_free(ssl_ctx);
        return false;
    }

    printf("Waiting for authorization callback on port %d (HTTPS)...\n", CALLBACK_PORT);
    fflush(stdout);

    // Accept connection
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        SSL_CTX_free(ssl_ctx);
        return false;
    }

    // Wrap with SSL
    SSL *ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, client_fd);

    if (SSL_accept(ssl) <= 0) {
        fprintf(stderr, "SSL handshake failed\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(client_fd);
        close(server_fd);
        SSL_CTX_free(ssl_ctx);
        return false;
    }

    // Read request over SSL
    char request[4096] = {0};
    SSL_read(ssl, request, sizeof(request) - 1);

    // Extract authorization code from URL
    char *code_start = strstr(request, "code=");
    char code[256] = {0};
    if (code_start) {
        code_start += 5;
        char *code_end = code_start;
        while (*code_end && *code_end != '&' && *code_end != ' ' && *code_end != '\r' && *code_end != '\n') {
            code_end++;
        }
        size_t code_len = code_end - code_start;
        if (code_len < sizeof(code)) {
            strncpy(code, code_start, code_len);
        }
    }

    // Send response to browser over SSL
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>Wahoo Authorization successful!</h1>"
        "<p>You can close this window and return to Sweattrails.</p></body></html>";
    SSL_write(ssl, response, strlen(response));

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client_fd);
    close(server_fd);
    SSL_CTX_free(ssl_ctx);

    if (!code[0]) {
        fprintf(stderr, "No authorization code received\n");
        return false;
    }

    printf("Got authorization code, exchanging for tokens...\n");
    fflush(stdout);

    // Exchange code for tokens
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char post_data[1024];
    snprintf(post_data, sizeof(post_data),
             "client_id=%s&client_secret=%s&code=%s&grant_type=authorization_code&redirect_uri=%s",
             config->client_id, config->client_secret, code, REDIRECT_URI);

    CurlBuffer buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, WAHOO_TOKEN_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "Token exchange failed: %s\n", curl_easy_strerror(res));
        curl_buffer_free(&buf);
        return false;
    }

    bool success = parse_token_response(buf.data, config);
    curl_buffer_free(&buf);

    if (success) {
        wahoo_save_config(config);
        printf("Wahoo authentication successful!\n");
        fflush(stdout);
    }

    return success;
}

bool wahoo_refresh_token(WahooConfig *config) {
    if (!config->refresh_token[0]) return false;

    // Check if token is still valid (with 5 minute buffer)
    if (config->token_expires_at > time(NULL) + 300) {
        return true;
    }

    printf("Refreshing Wahoo access token...\n");
    fflush(stdout);

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char post_data[1024];
    snprintf(post_data, sizeof(post_data),
             "client_id=%s&client_secret=%s&refresh_token=%s&grant_type=refresh_token",
             config->client_id, config->client_secret, config->refresh_token);

    CurlBuffer buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, WAHOO_TOKEN_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        curl_buffer_free(&buf);
        return false;
    }

    bool success = parse_token_response(buf.data, config);
    curl_buffer_free(&buf);

    if (success) {
        wahoo_save_config(config);
    }

    return success;
}

// Helper to find nested JSON object and extract a field
static bool json_get_nested_string(const char *json, const char *obj_key, const char *field_key, char *out, size_t out_size) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", obj_key);
    const char *obj_pos = strstr(json, search);
    if (!obj_pos) return false;

    const char *brace = strchr(obj_pos, '{');
    if (!brace) return false;

    // Find the field within this object
    snprintf(search, sizeof(search), "\"%s\"", field_key);
    const char *field_pos = strstr(brace, search);
    if (!field_pos) return false;

    field_pos = strchr(field_pos + strlen(search), ':');
    if (!field_pos) return false;

    while (*field_pos && (*field_pos == ':' || *field_pos == ' ' || *field_pos == '\t')) field_pos++;
    if (*field_pos != '"') return false;
    field_pos++;

    size_t i = 0;
    while (*field_pos && *field_pos != '"' && i < out_size - 1) {
        if (*field_pos == '\\' && *(field_pos + 1)) {
            field_pos++;
        }
        out[i++] = *field_pos++;
    }
    out[i] = '\0';
    return true;
}

static bool json_get_nested_float(const char *json, const char *obj_key, const char *field_key, float *out) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", obj_key);
    const char *obj_pos = strstr(json, search);
    if (!obj_pos) return false;

    const char *brace = strchr(obj_pos, '{');
    if (!brace) return false;

    snprintf(search, sizeof(search), "\"%s\"", field_key);
    const char *field_pos = strstr(brace, search);
    if (!field_pos) return false;

    field_pos = strchr(field_pos + strlen(search), ':');
    if (!field_pos) return false;

    while (*field_pos && (*field_pos == ':' || *field_pos == ' ' || *field_pos == '\t')) field_pos++;

    char *end;
    *out = strtof(field_pos, &end);
    return end != field_pos;
}

static bool json_get_nested_int(const char *json, const char *obj_key, const char *field_key, int *out) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", obj_key);
    const char *obj_pos = strstr(json, search);
    if (!obj_pos) return false;

    const char *brace = strchr(obj_pos, '{');
    if (!brace) return false;

    snprintf(search, sizeof(search), "\"%s\"", field_key);
    const char *field_pos = strstr(brace, search);
    if (!field_pos) return false;

    field_pos = strchr(field_pos + strlen(search), ':');
    if (!field_pos) return false;

    while (*field_pos && (*field_pos == ':' || *field_pos == ' ' || *field_pos == '\t')) field_pos++;

    char *end;
    *out = (int)strtol(field_pos, &end, 10);
    return end != field_pos;
}

bool wahoo_fetch_workouts(WahooConfig *config, WahooWorkoutList *list, int page, int per_page) {
    if (!wahoo_refresh_token(config)) {
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char url[512];
    snprintf(url, sizeof(url), "%s/workouts?page=%d&per_page=%d",
             WAHOO_API_URL, page, per_page);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->access_token);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Accept: application/json");

    CurlBuffer buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        curl_buffer_free(&buf);
        return false;
    }

    // Check for empty response
    if (!buf.data || buf.size == 0) {
        curl_buffer_free(&buf);
        return false;
    }

    // Initialize list if needed
    if (!list->workouts) {
        list->capacity = 64;
        list->workouts = malloc(list->capacity * sizeof(WahooWorkout));
        list->count = 0;
    }

    // Wahoo returns {"workouts": [...]}
    const char *workouts_start = strstr(buf.data, "\"workouts\"");
    if (!workouts_start) {
        curl_buffer_free(&buf);
        return true;  // No workouts, but not an error
    }

    // Parse JSON array - find each workout object
    const char *pos = strchr(workouts_start, '[');
    if (!pos) {
        curl_buffer_free(&buf);
        return true;
    }

    pos++;  // Skip '['
    while ((pos = strchr(pos, '{')) != NULL) {
        // Find end of this object
        int depth = 1;
        const char *start = pos;
        pos++;
        bool in_string = false;
        while (*pos && depth > 0) {
            if (*pos == '"' && *(pos - 1) != '\\') {
                in_string = !in_string;
            } else if (!in_string) {
                if (*pos == '{') depth++;
                else if (*pos == '}') depth--;
            }
            pos++;
        }

        if (depth != 0) break;

        // Extract this object
        size_t obj_len = pos - start;
        char *obj = malloc(obj_len + 1);
        strncpy(obj, start, obj_len);
        obj[obj_len] = '\0';

        // Parse workout
        if (list->count >= list->capacity) {
            list->capacity *= 2;
            list->workouts = realloc(list->workouts, list->capacity * sizeof(WahooWorkout));
        }

        WahooWorkout *workout = &list->workouts[list->count];
        memset(workout, 0, sizeof(WahooWorkout));

        if (json_get_int64(obj, "id", &workout->id)) {
            json_get_string(obj, "name", workout->name, sizeof(workout->name));
            json_get_string(obj, "starts", workout->starts, sizeof(workout->starts));
            json_get_int(obj, "minutes", &workout->minutes);

            // Get data from workout_summary
            json_get_nested_float(obj, "workout_summary", "distance_accum", &workout->distance_meters);
            json_get_nested_float(obj, "workout_summary", "ascent_accum", &workout->ascent_meters);
            json_get_nested_int(obj, "workout_summary", "heart_rate_avg", &workout->avg_heart_rate);
            json_get_nested_int(obj, "workout_summary", "power_avg", &workout->avg_power);

            // Get FIT file URL from file object within workout_summary
            json_get_nested_string(obj, "file", "url", workout->fit_file_url, sizeof(workout->fit_file_url));

            list->count++;
        }

        free(obj);
    }

    curl_buffer_free(&buf);
    return true;
}

bool wahoo_download_fit(WahooConfig *config, const char *fit_url, const char *output_path) {
    (void)config;  // FIT downloads from CDN don't need auth

    if (!fit_url || !fit_url[0]) {
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    FILE *f = fopen(output_path, "wb");
    if (!f) {
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, fit_url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(f);

    if (res != CURLE_OK) {
        unlink(output_path);  // Remove partial file
        return false;
    }

    printf("Downloaded Wahoo workout to: %s\n", output_path);
    fflush(stdout);

    return true;
}

void wahoo_workout_list_free(WahooWorkoutList *list) {
    free(list->workouts);
    list->workouts = NULL;
    list->count = 0;
    list->capacity = 0;
}
