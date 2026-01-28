#include "strava_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <curl/curl.h>

#define CONFIG_PATH "/.config/fitpower/config"
#define STRAVA_AUTH_URL "https://www.strava.com/oauth/authorize"
#define STRAVA_TOKEN_URL "https://www.strava.com/oauth/token"
#define STRAVA_API_URL "https://www.strava.com/api/v3"
#define CALLBACK_PORT 8089
#define REDIRECT_URI "http://localhost:8089/callback"

// Simple JSON value extraction (not a full parser)
static bool json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;

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
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;

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
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;

    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;

    char *end;
    *out = strtof(pos, &end);
    return end != pos;
}

static bool json_get_bool(const char *json, const char *key, bool *out) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;

    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;

    if (strncmp(pos, "true", 4) == 0) {
        *out = true;
        return true;
    } else if (strncmp(pos, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
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

bool strava_load_config(StravaConfig *config) {
    memset(config, 0, sizeof(StravaConfig));

    const char *home = getenv("HOME");
    if (!home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, CONFIG_PATH);

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

bool strava_save_config(const StravaConfig *config) {
    const char *home = getenv("HOME");
    if (!home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, CONFIG_PATH);

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

bool strava_is_authenticated(const StravaConfig *config) {
    if (!config->access_token[0]) return false;
    if (!config->refresh_token[0]) return false;
    return true;
}

// Parse tokens from JSON response
static bool parse_token_response(const char *json, StravaConfig *config) {
    if (!json_get_string(json, "access_token", config->access_token, sizeof(config->access_token))) {
        return false;
    }
    if (!json_get_string(json, "refresh_token", config->refresh_token, sizeof(config->refresh_token))) {
        return false;
    }

    int64_t expires;
    if (json_get_int64(json, "expires_at", &expires)) {
        config->token_expires_at = (time_t)expires;
    }

    return true;
}

bool strava_authenticate(StravaConfig *config) {
    // Create authorization URL
    char auth_url[1024];
    snprintf(auth_url, sizeof(auth_url),
             "%s?client_id=%s&response_type=code&redirect_uri=%s&approval_prompt=auto&scope=activity:read_all",
             STRAVA_AUTH_URL, config->client_id, REDIRECT_URI);

    printf("Opening browser for Strava authorization...\n");
    printf("If browser doesn't open, visit:\n%s\n\n", auth_url);
    fflush(stdout);

    // Open browser
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "open '%s'", auth_url);
    system(cmd);

    // Start local server to receive callback
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
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
        return false;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        close(server_fd);
        return false;
    }

    printf("Waiting for authorization callback on port %d...\n", CALLBACK_PORT);
    fflush(stdout);

    // Accept connection
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        return false;
    }

    // Read request
    char request[4096] = {0};
    read(client_fd, request, sizeof(request) - 1);

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

    // Send response to browser
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>Authorization successful!</h1>"
        "<p>You can close this window and return to fitpower.</p></body></html>";
    write(client_fd, response, strlen(response));

    close(client_fd);
    close(server_fd);

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
             "client_id=%s&client_secret=%s&code=%s&grant_type=authorization_code",
             config->client_id, config->client_secret, code);

    CurlBuffer buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, STRAVA_TOKEN_URL);
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
        strava_save_config(config);
        printf("Authentication successful!\n");
        fflush(stdout);
    }

    return success;
}

bool strava_refresh_token(StravaConfig *config) {
    if (!config->refresh_token[0]) return false;

    // Check if token is still valid (with 5 minute buffer)
    if (config->token_expires_at > time(NULL) + 300) {
        return true;
    }

    printf("Refreshing access token...\n");
    fflush(stdout);

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char post_data[1024];
    snprintf(post_data, sizeof(post_data),
             "client_id=%s&client_secret=%s&refresh_token=%s&grant_type=refresh_token",
             config->client_id, config->client_secret, config->refresh_token);

    CurlBuffer buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, STRAVA_TOKEN_URL);
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
        strava_save_config(config);
    }

    return success;
}

bool strava_fetch_activities(StravaConfig *config, StravaActivityList *list, int page, int per_page) {
    if (!strava_refresh_token(config)) {
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char url[512];
    snprintf(url, sizeof(url), "%s/athlete/activities?page=%d&per_page=%d",
             STRAVA_API_URL, page, per_page);

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->access_token);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);

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

    // Initialize list if needed
    if (!list->activities) {
        list->capacity = 64;
        list->activities = malloc(list->capacity * sizeof(StravaActivity));
        list->count = 0;
    }

    // Parse JSON array - find each activity object
    const char *pos = buf.data;
    while ((pos = strchr(pos, '{')) != NULL) {
        // Find end of this object
        int depth = 1;
        const char *start = pos;
        pos++;
        while (*pos && depth > 0) {
            if (*pos == '{') depth++;
            else if (*pos == '}') depth--;
            pos++;
        }

        if (depth != 0) break;

        // Extract this object
        size_t obj_len = pos - start;
        char *obj = malloc(obj_len + 1);
        strncpy(obj, start, obj_len);
        obj[obj_len] = '\0';

        // Parse activity
        if (list->count >= list->capacity) {
            list->capacity *= 2;
            list->activities = realloc(list->activities, list->capacity * sizeof(StravaActivity));
        }

        StravaActivity *act = &list->activities[list->count];
        memset(act, 0, sizeof(StravaActivity));

        if (json_get_int64(obj, "id", &act->id)) {
            json_get_string(obj, "name", act->name, sizeof(act->name));
            json_get_string(obj, "type", act->type, sizeof(act->type));
            json_get_string(obj, "start_date_local", act->start_date, sizeof(act->start_date));
            json_get_int(obj, "moving_time", &act->moving_time);
            json_get_float(obj, "distance", &act->distance);
            json_get_float(obj, "average_watts", &act->average_watts);
            json_get_bool(obj, "device_watts", &act->has_power);

            list->count++;
        }

        free(obj);
    }

    curl_buffer_free(&buf);
    return true;
}

bool strava_download_activity(StravaConfig *config, int64_t activity_id, const char *output_path) {
    if (!strava_refresh_token(config)) {
        return false;
    }

    // First, get the activity streams or export
    // Strava doesn't provide direct FIT download via API, so we get streams
    // and would need to convert - OR we can try to get the original file
    // through an undocumented endpoint

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    // Try to export as FIT (this requires the activity to have been uploaded as FIT originally)
    char url[512];
    snprintf(url, sizeof(url), "%s/activities/%lld/streams?keys=watts,time&key_by_type=true",
             STRAVA_API_URL, (long long)activity_id);

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->access_token);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);

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

    // For now, save the stream data as JSON
    // A proper implementation would convert this to FIT format
    // or use a different approach to get the original file

    FILE *f = fopen(output_path, "w");
    if (!f) {
        curl_buffer_free(&buf);
        return false;
    }

    fwrite(buf.data, 1, buf.size, f);
    fclose(f);

    curl_buffer_free(&buf);

    printf("Note: Strava API provides streams, not FIT files.\n");
    printf("Saved stream data to: %s\n", output_path);
    fflush(stdout);

    return true;
}

void strava_activity_list_free(StravaActivityList *list) {
    free(list->activities);
    list->activities = NULL;
    list->count = 0;
    list->capacity = 0;
}
