#include <stdio.h>
#include <curl/curl.h>

// Callback function to write received data to a file
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

int main(void) {
    CURL *curl_handle;
    FILE *fp;
    CURLcode res;
    const char *url = "https://example.com/path/to/your/file.txt"; // Replace with your URL
    const char *output_filename = "downloaded_file.txt"; // Replace with your desired filename

    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Get a curl handle
    curl_handle = curl_easy_init();
    if (curl_handle) {
        // Open the file for writing
        fp = fopen(output_filename, "wb");
        if (fp == NULL) {
            fprintf(stderr, "Error opening file %s\n", output_filename);
            return 1;
        }

        // Set the URL to download
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);

        // Set the callback function to write data to the file
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);

        // Pass the file pointer to the callback function
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, fp);

        // Perform the request
        res = curl_easy_perform(curl_handle);

        // Check for errors
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("File downloaded successfully to %s\n", output_filename);
        }

        // Clean up
        fclose(fp);
        curl_easy_cleanup(curl_handle);
    } else {
        fprintf(stderr, "Error initializing curl\n");
        return 1;
    }

    // Clean up libcurl global resources
    curl_global_cleanup();

    return 0;
}
