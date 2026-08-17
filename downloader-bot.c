#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <time.h>
// put youre tkoen
#define TOKEN "YOUR_BOT_TOKEN_HERE"
#define MAX_FILE_SIZE 50000000  // 50MB (Telegram bot limit)

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) {
        printf("Not enough memory!\n");
        return 0;
    }
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

void send_message(long long chat_id, const char *text)
{
    CURL *curl;
    CURLcode res;
    
    curl = curl_easy_init();
    
    if(curl) {
        char url[512];
        char data[2048];
        
        snprintf(url, sizeof(url), 
                 "https://api.telegram.org/bot%s/sendMessage", TOKEN);
        
        char *encoded_text = curl_easy_escape(curl, text, 0);
        snprintf(data, sizeof(data),
                 "chat_id=%lld&text=%s", chat_id, encoded_text);
        curl_free(encoded_text);
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
}

void send_video(long long chat_id, const char *filepath, const char *caption)
{
    CURL *curl;
    CURLcode res;
    curl_mime *mime = NULL;
    curl_mimepart *part = NULL;
    char chat_id_str[32];
    
    snprintf(chat_id_str, sizeof(chat_id_str), "%lld", chat_id);
    
    curl = curl_easy_init();
    
    if(curl) {
        char url[512];
        snprintf(url, sizeof(url), 
                 "https://api.telegram.org/bot%s/sendVideo", TOKEN);
        
        mime = curl_mime_init(curl);
        
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "chat_id");
        curl_mime_data(part, chat_id_str, CURL_ZERO_TERMINATED);
        
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "video");
        curl_mime_filedata(part, filepath);
        
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "caption");
        curl_mime_data(part, caption, CURL_ZERO_TERMINATED);
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        
        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            printf("Failed to send video: %s\n", curl_easy_strerror(res));
        }
        
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
    }
}

int detect_platform(const char *url)
{
    if(strstr(url, "youtube.com") != NULL || strstr(url, "youtu.be") != NULL) {
        return 1;
    }
    else if(strstr(url, "instagram.com") != NULL) {
        return 2;
    }
    else if(strstr(url, "tiktok.com") != NULL) {
        return 3;
    }
    return 0;
}

int download_video(const char *url, char *output_path, size_t path_size)
{
    char command[2048];
    char filename[256];
    time_t now = time(NULL);
    
    snprintf(filename, sizeof(filename), "video_%ld.mp4", now);
    snprintf(output_path, path_size, "/tmp/%s", filename);
    
    snprintf(command, sizeof(command),
             "yt-dlp -f 'best[ext=mp4]/best' -o '%s' '%s' 2>&1", 
             output_path, url);
    
    printf("📥 Downloading: %s\n", url);
    
    int result = system(command);
    
    if(result == 0 && access(output_path, F_OK) == 0) {
        return 1;
    }
    
    return 0;
}

long get_file_size(const char *filepath)
{
    FILE *file = fopen(filepath, "rb");
    if(file == NULL) {
        return -1;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    
    return size;
}

void process_messages(long long *offset)
{
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl = curl_easy_init();
    
    if(curl) {
        char url[512];
        snprintf(url, sizeof(url), 
                 "https://api.telegram.org/bot%s/getUpdates?timeout=30&offset=%lld",
                 TOKEN, *offset);
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        
        res = curl_easy_perform(curl);
        
        if(res == CURLE_OK) {
            json_object *root = json_tokener_parse(chunk.memory);
            
            if(root != NULL) {
                json_object *result = json_object_object_get(root, "result");
                
                if(result != NULL && json_object_is_type(result, json_type_array)) {
                    int array_len = json_object_array_length(result);
                    
                    for(int i = 0; i < array_len; i++) {
                        json_object *update = json_object_array_get_idx(result, i);
                        json_object *update_id = json_object_object_get(update, "update_id");
                        json_object *message = json_object_object_get(update, "message");
                        
                        if(message != NULL) {
                            json_object *chat = json_object_object_get(message, "chat");
                            json_object *chat_id = json_object_object_get(chat, "id");
                            json_object *text = json_object_object_get(message, "text");
                            
                            long long cid = json_object_get_int64(chat_id);
                            
                            if(text != NULL) {
                                const char *msg_text = json_object_get_string(text);
                                
                                if(strcmp(msg_text, "/start") == 0) {
                                    send_message(cid, "🤖 Welcome to Media Downloader Bot!\n\nSend me a link from:\n🎥 YouTube\n📸 Instagram\n🎵 TikTok\n\nOr type /help for more info.");
                                }
                                else if(strcmp(msg_text, "/help") == 0) {
                                    send_message(cid, "📚 How to use:\n\n1️⃣ Copy a video link from YouTube, Instagram, or TikTok\n2️⃣ Paste it here\n3️⃣ Wait for the bot to download and send it to you!");
                                }
                                else if(strstr(msg_text, "http") != NULL) {
                                    int platform = detect_platform(msg_text);
                                    
                                    if(platform == 0) {
                                        send_message(cid, "❌ Unsupported link!");
                                        continue;
                                    }
                                    
                                    if(platform == 1) {
                                        send_message(cid, "🎥 YouTube link detected!\n⏳ Downloading...");
                                    }
                                    else if(platform == 2) {
                                        send_message(cid, "📸 Instagram link detected!\n⏳ Downloading...");
                                    }
                                    else if(platform == 3) {
                                        send_message(cid, "🎵 TikTok link detected!\n⏳ Downloading...");
                                    }
                                    
                                    char filepath[512];
                                    if(download_video(msg_text, filepath, sizeof(filepath))) {
                                        long file_size = get_file_size(filepath);
                                        
                                        if(file_size > MAX_FILE_SIZE) {
                                            char size_msg[512];
                                            snprintf(size_msg, sizeof(size_msg), 
                                                     "❌ File too large! (%.1f MB)\nMax: 50MB", 
                                                     (float)file_size / 1000000);
                                            send_message(cid, size_msg);
                                        }
                                        else {
                                            send_message(cid, "📤 Uploading video...");
                                            send_video(cid, filepath, "✅ Download complete!");
                                            send_message(cid, "🎉 Video sent successfully!");
                                        }
                                        
                                        remove(filepath);
                                    }
                                    else {
                                        send_message(cid, "❌ Download failed! Check the link.");
                                    }
                                }
                                else {
                                    send_message(cid, "Send me a valid link! 🎥📸🎵");
                                }
                            }
                        }
                        
                        *offset = json_object_get_int64(update_id) + 1;
                    }
                }
                
                json_object_put(root);
            }
        }
        
        curl_easy_cleanup(curl);
    }
    
    free(chunk.memory);
}

int main()
{
    long long offset = 0;
    
    curl_global_init(CURL_GLOBAL_ALL);    
    printf("🤖 Bot started! Press Ctrl+C to stop.\n");
    
    while(1) {
        process_messages(&offset);
        usleep(1000000);
    }
    
    curl_global_cleanup();
    return 0;
}
