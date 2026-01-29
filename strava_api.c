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

// Helper to find a key at object top-level (not inside nested objects)
static const char *json_find_key(const char *json, const char *key) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = json;

    while ((pos = strstr(pos, search)) != NULL) {
        // Count braces from start to check nesting level
        int depth = 0;
        for (const char *p = json; p < pos; p++) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
        }
        // depth == 1 means top level of the object
        if (depth <= 1) {
            return pos;
        }
        pos++;
    }
    return NULL;
}

// Simple JSON value extraction (not a full parser)
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

static bool json_get_bool(const char *json, const char *key, bool *out) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;

    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
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
        // Find end of this object, skipping string contents
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

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->access_token);

    // Step 1: Fetch activity details
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char url[512];
    snprintf(url, sizeof(url), "%s/activities/%lld",
             STRAVA_API_URL, (long long)activity_id);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);

    CurlBuffer activity_buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &activity_buf);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_buffer_free(&activity_buf);
        return false;
    }

    // Extract activity metadata
    char name[256] = "";
    char type[64] = "";
    char start_date[64] = "";
    float distance = 0;
    int moving_time = 0;

    json_get_string(activity_buf.data, "name", name, sizeof(name));
    json_get_string(activity_buf.data, "type", type, sizeof(type));
    json_get_string(activity_buf.data, "start_date", start_date, sizeof(start_date));
    json_get_float(activity_buf.data, "distance", &distance);
    json_get_int(activity_buf.data, "moving_time", &moving_time);

    curl_buffer_free(&activity_buf);

    // Step 2: Fetch all available streams
    curl = curl_easy_init();
    if (!curl) {
        curl_slist_free_all(headers);
        return false;
    }

    snprintf(url, sizeof(url), "%s/activities/%lld/streams?keys=time,watts,latlng,heartrate,cadence,altitude,distance&key_by_type=true",
             STRAVA_API_URL, (long long)activity_id);

    CurlBuffer streams_buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &streams_buf);

    res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        curl_buffer_free(&streams_buf);
        return false;
    }

    // Step 3: Build structured JSON output
    FILE *f = fopen(output_path, "w");
    if (!f) {
        curl_buffer_free(&streams_buf);
        return false;
    }

    // Escape name for JSON (handle quotes and backslashes)
    char escaped_name[512];
    size_t j = 0;
    for (size_t i = 0; name[i] && j < sizeof(escaped_name) - 2; i++) {
        if (name[i] == '"' || name[i] == '\\') {
            escaped_name[j++] = '\\';
        }
        escaped_name[j++] = name[i];
    }
    escaped_name[j] = '\0';

    fprintf(f, "{\n");
    fprintf(f, "  \"source\": \"strava\",\n");
    fprintf(f, "  \"activity_id\": %lld,\n", (long long)activity_id);
    fprintf(f, "  \"name\": \"%s\",\n", escaped_name);
    fprintf(f, "  \"type\": \"%s\",\n", type);
    fprintf(f, "  \"start_date\": \"%s\",\n", start_date);
    fprintf(f, "  \"distance\": %.1f,\n", distance);
    fprintf(f, "  \"moving_time\": %d,\n", moving_time);

    // Parse and reformat streams from Strava's key_by_type format
    // Strava returns: {"time": {"data": [...]}, "watts": {"data": [...]}, ...}
    fprintf(f, "  \"streams\": {\n");

    const char *stream_keys[] = {"time", "watts", "latlng", "heartrate", "cadence", "altitude", "distance", NULL};
    bool first_stream = true;

    for (int k = 0; stream_keys[k]; k++) {
        // Find the stream key in the response
        char search[64];
        snprintf(search, sizeof(search), "\"%s\"", stream_keys[k]);
        const char *stream_pos = strstr(streams_buf.data, search);
        if (!stream_pos) continue;

        // Find the "data" array within this stream
        const char *data_pos = strstr(stream_pos, "\"data\"");
        if (!data_pos) continue;

        // Find the opening bracket of the data array
        const char *arr_start = strchr(data_pos, '[');
        if (!arr_start) continue;

        // Find the matching closing bracket
        int depth = 1;
        const char *arr_end = arr_start + 1;
        while (*arr_end && depth > 0) {
            if (*arr_end == '[') depth++;
            else if (*arr_end == ']') depth--;
            arr_end++;
        }

        if (depth != 0) continue;

        // Extract the array content
        size_t arr_len = arr_end - arr_start;
        char *arr_content = malloc(arr_len + 1);
        if (!arr_content) continue;
        strncpy(arr_content, arr_start, arr_len);
        arr_content[arr_len] = '\0';

        if (!first_stream) fprintf(f, ",\n");
        first_stream = false;
        fprintf(f, "    \"%s\": %s", stream_keys[k], arr_content);

        free(arr_content);
    }

    fprintf(f, "\n  }\n");
    fprintf(f, "}\n");
    fclose(f);

    curl_buffer_free(&streams_buf);

    printf("Downloaded activity to: %s\n", output_path);
    fflush(stdout);

    return true;
}

void strava_activity_list_free(StravaActivityList *list) {
    free(list->activities);
    list->activities = NULL;
    list->count = 0;
    list->capacity = 0;
}
