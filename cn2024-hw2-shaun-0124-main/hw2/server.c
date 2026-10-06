#include "format.h"

// 2.4 Endpoints
#define ENDPOINT_COUNT 10
#define ENDPOINT_ROOT 0
#define ENDPOINT_UPLOAD_FILE 1
#define ENDPOINT_UPLOAD_VIDEO 2
#define ENDPOINT_FILE 3
#define ENDPOINT_VIDEO 4
#define ENDPOINT_VIDEO_DIR 5
#define ENDPOINT_API_FILE 6
#define ENDPOINT_API_FILE_DIR 7
#define ENDPOINT_API_VIDEO 8
#define ENDPOINT_API_VIDEO_DIR 9
const bool ENDPOINT_METHOD_IS_GET[ENDPOINT_COUNT] = {1, 1, 1, 1, 1, 1, 0, 1, 0, 1}; // allowed is either GET only or POST only with content type multipart/form-data
const bool ENDPOINT_AUTH_REQUIRED[ENDPOINT_COUNT] = {0, 1, 1, 0, 0, 0, 1, 0, 1, 0}; // only upload-related endpoints require authentication

// large arrays
char actual_body[BODY_SIZE];
char packed_request[BUFF_SIZE];
char packed_response[BUFF_SIZE];
struct HTTPMessage request;
struct HTTPMessage response;

// Find endpoint type, and parse the file path if it has one
int get_endpoint_type(char* endpoint, char* dir){
    int endpoint_type = -1;
    if(strcmp(endpoint, "/") == 0) endpoint_type = ENDPOINT_ROOT;
    if(strcmp(endpoint, "/upload/file") == 0) endpoint_type = ENDPOINT_UPLOAD_FILE;
    if(strcmp(endpoint, "/upload/video") == 0) endpoint_type = ENDPOINT_UPLOAD_VIDEO;
    if(strcmp(endpoint, "/file/") == 0) endpoint_type = ENDPOINT_FILE;
    if(strcmp(endpoint, "/video/") == 0) endpoint_type = ENDPOINT_VIDEO;
    if(strncmp(endpoint, "/video/", 7) == 0 && strlen(endpoint) > 7) endpoint_type = ENDPOINT_VIDEO_DIR;
    if(strcmp(endpoint, "/api/file") == 0) endpoint_type = ENDPOINT_API_FILE;
    if(strncmp(endpoint, "/api/file/", 10) == 0) endpoint_type = ENDPOINT_API_FILE_DIR;
    if(strcmp(endpoint, "/api/video") == 0) endpoint_type = ENDPOINT_API_VIDEO;
    if(strncmp(endpoint, "/api/video/", 11) == 0) endpoint_type = ENDPOINT_API_VIDEO_DIR;
    // fprintf(stderr, "endpoint = %s, type = %d", endpoint, endpoint_type);

    if(dir != NULL){
        if(endpoint_type == ENDPOINT_VIDEO_DIR) strcpy(dir, endpoint + 7);
        if(endpoint_type == ENDPOINT_API_FILE_DIR) strcpy(dir, endpoint + 10);
        if(endpoint_type == ENDPOINT_API_VIDEO_DIR) strcpy(dir, endpoint + 11);
        // fprintf(stderr, ", filepath = %s", dir);
    }
    
    // fprintf(stderr, "\n");
    return endpoint_type;
}

int main(int argc, char *argv[]){

    // 2.1 Check arg count
    if(argc != 2){
        fprintf(STDERR_OUT, "Usage: ./server [port]\n");
        return -1;
    }
    PORT = atoi(argv[1]);

    int listenfd, connfd;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);
    // char *message = "Hello World!";

    // printf("%s\n", message);

    // vvvvvvvv BUILD CONNECTION WITH CLIENT vvvvvvvv
    // Get socket file descriptor
    if((listenfd = socket(AF_INET , SOCK_STREAM , 0)) < 0){
        ERR_EXIT("socket()");
    }

    // Set server address information
    bzero(&server_addr, sizeof(server_addr)); // erase the data
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    // Bind the server file descriptor to the server address
    if(bind(listenfd, (struct sockaddr *)&server_addr , sizeof(server_addr)) < 0){
        ERR_EXIT("bind()");
    }
    
    // Listen on the server file descriptor
    if(listen(listenfd , 100) < 0) ERR_EXIT("listen()");
    // ^^^^^^^^ BUILD CONNECTION WITH CLIENT ^^^^^^^^

    // Multiplexing
    struct pollfd fd_list[FD_MAX];
    for(int i = 0; i < FD_MAX; i++){
        fd_list[i].fd = -i;
        fd_list[i].events = POLLIN;
    }
    fd_list[0].fd = -1; // turn off 0
    fd_list[listenfd].fd = listenfd; // originally only listenfd is on
    // fprintf(stderr, "listenfd = %d\n", listenfd);
    // pollfd.fd >= 0: consider fd
    // pollfd.fd <  0: ignore fd

    while(1){

        // Wait until a client wants to connect or send request
        // fprintf(stderr, "polling: ");
        for(int i = 0; i < FD_MAX; i++){
            if(fd_list[i].fd >= 0){ // fprintf(stderr, "%d ", i);
            }
        }
        // fprintf(stderr, "\n");
        poll(fd_list, FD_MAX, -1);
        // fprintf(stderr, "poll ended\n");
        
        // find the fd of occurring event
        int cur_fd;
        for(int i = 0; i < FD_MAX; i++){
            if(fd_list[i].revents & POLLIN){
                cur_fd = i;
                // fprintf(stderr, "poll ended by %d\n", i);
            }
        }

        // connect
        if(cur_fd == listenfd){
            // Accept the client and get client file descriptor
            if((connfd = accept(listenfd, (struct sockaddr *)&client_addr, (socklen_t*)&client_addr_len)) < 0) ERR_EXIT("accept()");
            fd_list[connfd].fd = connfd;
            continue;
        }

        // send request
        else{
            connfd = cur_fd;
        }
        
        // char packed_request[BUFF_SIZE]; // MOVED
        // struct HTTPMessage request; // MOVED
        init_request(&request);

        // char packed_response[BUFF_SIZE]; // MOVED
        // struct HTTPMessage response; // MOVED
        init_response(&response);

        // Read HTTP request from client
        // fprintf(stderr, "reading %d\n", BUFF_SIZE);

        int bytes_read;
        if((bytes_read = HTTP_recv(connfd, packed_request, MSG_WAITALL)) < 0){
            close(connfd);
            fd_list[connfd].fd = -connfd; // client disconnected: disable fd
            // fprintf(stderr, "client disconnected\n");
            continue;
        }
        // fprintf(stderr, "read complete %d\n", bytes_read);
        unpack_message(packed_request, &request, UNPACK_AS_REQUEST);
        url_decode(request.args[REQUEST_ARG_QUERY]); // URL decoded here
        
        // debug zone
        // fprintf(stderr, "The request is:\n");
        debug_print_message(packed_request);

        // pack_message(packed_request, request);
        // // fprintf(stderr, "The decoded request is:\n");
        // debug_print_message(packed_request);
        // debug zone

        // (404) Find endpoint type
        char dir[VAL_SIZE];
        int endpoint_type = get_endpoint_type(request.args[REQUEST_ARG_QUERY], dir);
        // fprintf(stderr, "get endpoint type ok\n");
        if(endpoint_type == -1){
            response.headers[RESPONSE_HEADER_CONTENT_TYPE].is_active = 1;
            strcpy(response.args[RESPONSE_ARG_STATUS_CODE], "404");
            strcpy(response.args[RESPONSE_ARG_STATUS_MESSAGE], "Not Found");
            strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "text/plain");
            strcpy(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val, "10");
            strcpy(response.body, "Not Found\n");
            
            pack_message(packed_response, &response, UNPACK_AS_RESPONSE);
            HTTP_send(connfd, packed_response, atoi(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val), MSG_NOSIGNAL); // MSG_NOSIGNAL: shut up SIGPIPE
            continue;
        }
        // fprintf(stderr, "check endpoint type ok\n");

        // (405) Check if method is allowed
        if(!(   ( ENDPOINT_METHOD_IS_GET[endpoint_type] && strcmp(request.args[REQUEST_ARG_METHOD], "GET" ) == 0) ||
                (!ENDPOINT_METHOD_IS_GET[endpoint_type] && strcmp(request.args[REQUEST_ARG_METHOD], "POST") == 0 && strncmp(request.headers[REQUEST_HEADER_CONTENT_TYPE].header_val, "multipart/form-data", 19) == 0)))
            {
            response.headers[RESPONSE_HEADER_ALLOW].is_active = 1;
            strcpy(response.args[RESPONSE_ARG_STATUS_CODE], "405");
            strcpy(response.args[RESPONSE_ARG_STATUS_MESSAGE], "Method Not Allowed");
            strcpy(response.headers[RESPONSE_HEADER_ALLOW].header_val, ENDPOINT_METHOD_IS_GET[endpoint_type] ? "GET" : "POST");
            
            pack_message(packed_response, &response, UNPACK_AS_RESPONSE);
            HTTP_send(connfd, packed_response, atoi(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val), MSG_NOSIGNAL);
            continue;
        }
        // fprintf(stderr, "check method ok\n");

        // (401) Check authentication
        char* encoded_credentials;
        char* credentials;
        int encoded_len;
        int decoded_len;
        bool credentials_ok = 0;
        
        // - no creds
        if(!request.headers[REQUEST_HEADER_AUTHORIZATION].is_active){
            // fprintf(stderr, "no creds\n");
        }

        // - has creds, check it
        else{
            encoded_credentials = request.headers[REQUEST_HEADER_AUTHORIZATION].header_val;
            while(strncmp(encoded_credentials, "Basic ", 6) != 0) encoded_credentials++; // "Basic "
            encoded_credentials += 6;
            // fprintf(stderr, "encoded creds: %s\n", encoded_credentials);
            encoded_len = strlen(encoded_credentials); // The actual credentials in base64
            credentials = (char*)base64_decode(encoded_credentials, encoded_len, (size_t*)&decoded_len); // credentials: "u:p"
            // fprintf(stderr, "creds: %s\n", credentials);

            FILE* file_secret = fopen("secret", "r");
            if(file_secret == NULL) ERR_EXIT("open secret failed");
        
            char cur_line[VAL_SIZE];
            while(!feof(file_secret)){
                fgets(cur_line, VAL_SIZE, file_secret); // cur_line: "u:p\n" / "u:p\0"
                // fprintf(stderr, "fgets: %s", cur_line);
                if(strncmp(cur_line, credentials, decoded_len) == 0){
                    credentials_ok = 1;
                    // fprintf(stderr, "cred pass\n");
                }
                else{ // fprintf(stderr, "cred fail\n"); 
                }
            }
            fclose(file_secret);
        }
        // fprintf(stderr, "check creds ok\n");

        // - check if creds required and not provided
        if(ENDPOINT_AUTH_REQUIRED[endpoint_type] && !credentials_ok){
            response.headers[RESPONSE_HEADER_CONTENT_TYPE].is_active = 1;
            response.headers[RESPONSE_HEADER_WWW_AUTHENTICATE].is_active = 1;
            strcpy(response.args[RESPONSE_ARG_STATUS_CODE], "401");
            strcpy(response.args[RESPONSE_ARG_STATUS_MESSAGE], "Unauthorized");
            strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "text/plain");
            strcpy(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val, "13");
            strcpy(response.headers[RESPONSE_HEADER_WWW_AUTHENTICATE].header_val, "Basic"); // realm?
            strcpy(response.body, "Unauthorized\n");

            pack_message(packed_response, &response, UNPACK_AS_RESPONSE);
            HTTP_send(connfd, packed_response, atoi(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val), MSG_NOSIGNAL);
            continue;
        }

        // TODO: Process the client's request
        
        // - for file & dir operations
        struct dirent* dp;
        DIR* dfd;
        int file_c; // = =
        int file_size = 0;

        // fprintf(stderr, "processing requests: \n");
        // - 0 ~ 4. HTML page requests
        // - 5. HTML video player requests
        if( endpoint_type == ENDPOINT_ROOT ||
            endpoint_type == ENDPOINT_UPLOAD_FILE ||
            endpoint_type == ENDPOINT_UPLOAD_VIDEO ||
            endpoint_type == ENDPOINT_FILE ||
            endpoint_type == ENDPOINT_VIDEO ||
            endpoint_type == ENDPOINT_VIDEO_DIR){
            
            // - the .rhtml file to be converted
            // - for simplicity, "<?TAG_NAME?>" are changed to single "?"
            FILE* file_rhtml;

            // - the .html file to be sent
            FILE* file_html;

            // - convert .rhtml to .html
            if( endpoint_type == ENDPOINT_FILE ||
                endpoint_type == ENDPOINT_VIDEO ||
                endpoint_type == ENDPOINT_VIDEO_DIR
                ){
                char target_dir[VAL_SIZE];
                char* filepath;

                if(endpoint_type == ENDPOINT_FILE){
                    file_rhtml = fopen("web/listf.rhtml", "r");
                    file_html = fopen("web/listf_converted.html", "w");
                    strcpy(target_dir, "web/files");
                } 
                if(endpoint_type == ENDPOINT_VIDEO){
                    file_rhtml = fopen("web/listv.rhtml", "r");
                    file_html = fopen("web/listv_converted.html", "w");
                    strcpy(target_dir, "web/videos");
                }
                if(endpoint_type == ENDPOINT_VIDEO_DIR){
                    filepath = request.args[REQUEST_ARG_QUERY] + 7; // /video/{videoname}
                    // 5. if video DNE: 404 here?
                    file_rhtml = fopen("web/player.rhtml", "r");
                    file_html = fopen("web/player_converted.html", "w");
                }
                // fprintf(stderr, "rhtml open ok\n");

                while((file_c = fgetc(file_rhtml)) != EOF){
                    if(file_c == '?'){
                        // List all files in the specified directory
                        // https://stackoverflow.com/a/1274167
                            
                        if((dfd = opendir(target_dir)) == NULL) continue; // if directory is empty, skip "?" part
                        char cur_filename[VAL_SIZE];

                        // fprintf(stderr, "readdir...\n");
                        while((dp = readdir(dfd)) != NULL){
                            // fprintf(stderr, "readdir ok\n");
                            strcpy(cur_filename, dp -> d_name);
                            // fprintf(stderr, "filename: %s\n", cur_filename);
                            if(strcmp(cur_filename, ".") == 0 || strcmp(cur_filename, "..") == 0) continue; // = =
                            char cur_filename_encoded[VAL_SIZE]; // SOMEHOW WE STILL NEED THE ENCODED VERSION
                            strcpy(cur_filename_encoded, cur_filename);
                            url_encode(cur_filename_encoded);
                            if(endpoint_type == ENDPOINT_FILE){
                                fprintf(file_html, "<tr><td><a href=\"/api/file/%s\">%s</a></td></tr>\n", cur_filename_encoded, cur_filename);
                            }
                            if(endpoint_type == ENDPOINT_VIDEO){
                                fprintf(file_html, "<tr><td><a href=\"/video/%s\">%s</a></td></tr>\n", cur_filename_encoded, cur_filename);
                            }
                        }
                    }
                    else if(file_c == '@'){
                        fprintf(file_html, "%s", filepath);
                    }
                    else if(file_c == '$'){
                        fprintf(file_html, "/api/video/%s/dash.mpd", filepath);
                    }
                    else fputc(file_c, file_html);
                }
                // fprintf(stderr, "write to html ok\n");

                fclose(file_rhtml);
                fclose(file_html);
                // fprintf(stderr, "rhtml close ok\n");
            }
            
            if(endpoint_type == ENDPOINT_ROOT) file_html = fopen("web/index.html", "r");
            if(endpoint_type == ENDPOINT_UPLOAD_FILE) file_html = fopen("web/uploadf.html", "r");
            if(endpoint_type == ENDPOINT_UPLOAD_VIDEO) file_html = fopen("web/uploadv.html", "r");
            if(endpoint_type == ENDPOINT_FILE) file_html = fopen("web/listf_converted.html", "r");
            if(endpoint_type == ENDPOINT_VIDEO) file_html = fopen("web/listv_converted.html", "r");
            if(endpoint_type == ENDPOINT_VIDEO_DIR) file_html = fopen("web/player_converted.html", "r");
            // fprintf(stderr, "html open ok\n");

            strcpy(response.body, "");
            while((file_c = fgetc(file_html)) != EOF && file_size < BODY_SIZE){
                file_size++;
                strncat(response.body, (char*)&file_c, 1);
            }
            // fprintf(stderr, "write to html ok\n");
            fclose(file_html);
            // fprintf(stderr, "html close ok\n");

            response.headers[RESPONSE_HEADER_CONTENT_TYPE].is_active = 1;
            strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "text/html");
            itoa(file_size, response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val, 10);
        }

        // - 6., 8. file/vid upload requests
        if( endpoint_type == ENDPOINT_API_FILE ||
            endpoint_type == ENDPOINT_API_VIDEO
            ){
            char* cur_body = request.body;
            char* content_type_header = request.headers[REQUEST_HEADER_CONTENT_TYPE].header_val;
            char boundary[VAL_SIZE], name[VAL_SIZE], filename[VAL_SIZE]/*, actual_body[BUFF_SIZE] // MOVED */;
            char filename_notype[VAL_SIZE];
            int actual_body_len;
            bool is_binary;
            FILE* cur_file;

            // parse boundary
            strcpy(boundary, "--");
            while(strncmp_ci(content_type_header, "boundary=", 9) != 0) content_type_header++; // google chrome: boundary has no ""
            content_type_header += 9;
            while(*content_type_header != '\0'){
                if(*content_type_header != '\"') strncat(boundary, content_type_header, 1);
                content_type_header++;
            }
            // fprintf(stderr, "boundary: %s\n", boundary);

            // parse parts, write each part into file
            while(multipart_next_part(&cur_body, boundary, name, filename, actual_body, &actual_body_len, &is_binary)){
                // ??????? last part: name="submit", no filename
                if(strlen(filename) == 0){
                    // fprintf(stderr, "multipart submit\n");
                    continue;
                }

                // fprintf(stderr, "name: %s, filename: %s, actual body: %s\n", name, filename, actual_body);
                char cur_filepath[VAL_SIZE];
                if(endpoint_type == ENDPOINT_API_FILE){
                    mkdir("web/files", 0777);
                    strcpy(cur_filepath, "web/files/");
                    strcat(cur_filepath, filename);
                }
                if(endpoint_type == ENDPOINT_API_VIDEO){
                    mkdir("web/tmp", 0777);
                    strcpy(cur_filepath, "web/tmp/");
                    strcat(cur_filepath, filename);
                }
                // fprintf(stderr, "filepath: %s\n", cur_filepath);
                cur_file = fopen(cur_filepath, "wb");
                if(cur_file == NULL) ERR_EXIT("fopen failed");
                // fprintf(stderr, "fopen ok\n");
                
                // fprintf(stderr, "actual body len = %d\n", actual_body_len);
                fwrite(actual_body, 1, actual_body_len, cur_file);
                // fprintf(stderr, "fwrite ok\n");
                fclose(cur_file);
                // fprintf(stderr, "fclose ok\n");

                // filename_notype
                strcpy(filename_notype, filename);
                int tmp = 0;
                while(filename_notype[tmp] != '.') tmp++;
                filename_notype[tmp] = '\0';

                // 8. vid upload requests: copy video to web/videos, setup convert command
                char web_video_dir[VAL_SIZE + 32];
                char cp_command[4 * VAL_SIZE]; // shut up the compiler
                if(endpoint_type == ENDPOINT_API_VIDEO){
                    sprintf(web_video_dir, "web/videos/%s", filename_notype);
                    mkdir("web/videos", 0777);
                    mkdir(web_video_dir, 0777);
                    sprintf(cp_command, "cp %s %s/%s", cur_filepath, web_video_dir, filename);
                    // fprintf(stderr, "copy command: %s\n", cp_command);
                    system(cp_command);
                    sprintf(video_encoding_command, VIDEO_ENCODING_COMMAND, filename_notype, filename_notype);
                    // fprintf(stderr, "video encoding command: %s\n", video_encoding_command);
                }
            }

            strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "text/plain");
            if(endpoint_type == ENDPOINT_API_FILE){
                strcpy(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val, "14");
                strcpy(response.body, "File Uploaded\n");
            }
            if(endpoint_type == ENDPOINT_API_VIDEO){
                strcpy(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val, "15");
                strcpy(response.body, "Video Uploaded\n");
            }
        }

        // - 7., 9. file/vid download requests
        if( endpoint_type == ENDPOINT_API_FILE_DIR ||
            endpoint_type == ENDPOINT_API_VIDEO_DIR){
            char requested_filepath[VAL_SIZE];
            char* filepath_ptr = request.args[REQUEST_ARG_QUERY];
            char* file_type = filepath_ptr;
            while(*file_type != '.') file_type++; // string file_type = ".txt" ".mp4" etc.

            if(endpoint_type == ENDPOINT_API_FILE_DIR){
                while(strncmp(filepath_ptr, "/api/file/", 10) != 0) filepath_ptr++;
                filepath_ptr += 10;
                strcpy(requested_filepath, "web/files/");
                strcat(requested_filepath, filepath_ptr);
            }
            if(endpoint_type == ENDPOINT_API_VIDEO_DIR){
                while(strncmp(filepath_ptr, "/api/video/", 11) != 0) filepath_ptr++;
                filepath_ptr += 11;
                strcpy(requested_filepath, "web/videos/");
                strcat(requested_filepath, filepath_ptr);
            }
            // fprintf(stderr, "requested filepath: %s\n", requested_filepath);
            
            // fprintf(stderr, "Requested file: %s\n", requested_filepath);
            FILE* requested_file;

            // (404) if requested file DNE
            if((requested_file = fopen(requested_filepath, "rb")) == NULL){
                // fprintf(stderr, "Requested file DNE\n");
                response.headers[RESPONSE_HEADER_CONTENT_TYPE].is_active = 1;
                strcpy(response.args[RESPONSE_ARG_STATUS_CODE], "404");
                strcpy(response.args[RESPONSE_ARG_STATUS_MESSAGE], "Not Found");
                strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "text/plain");
                strcpy(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val, "10");
                strcpy(response.body, "Not Found\n");
                
                pack_message(packed_response, &response, UNPACK_AS_RESPONSE);
                HTTP_send(connfd, packed_response, atoi(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val), MSG_NOSIGNAL);
                continue;
            }

            // requested files exists
            else{
                // fprintf(stderr, "Requested file exists\n");
                
                // copy requested_file to response.body
                fseek(requested_file, 0, SEEK_END);
                int file_size = ftell(requested_file);
                fseek(requested_file, 0, SEEK_SET);
                fread(response.body, 1, file_size, requested_file);
                
                // fprintf(stderr, "file size = %d\n", file_size);

                response.headers[RESPONSE_HEADER_CONTENT_TYPE].is_active = 1;
                
                // MIME type
                if(strcmp(file_type, ".html") == 0 || strcmp(file_type, ".rhtml") == 0)
                    strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "text/html");
                else if(strcmp(file_type, ".mp4") == 0 || strcmp(file_type, ".m4v") == 0)
                    strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "video/mp4");
                else if(strcmp(file_type, ".m4s") == 0)
                    strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "video/iso.segment");
                else if(strcmp(file_type, ".m4a") == 0)
                    strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "audio/mp4");
                else if(strcmp(file_type, ".mpd") == 0)
                    strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "application/dash+xml");
                else
                    strcpy(response.headers[RESPONSE_HEADER_CONTENT_TYPE].header_val, "text/plain");
                
                itoa(file_size, response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val, 10);
                fclose(requested_file);
            }
        }

        // (200 OK) Send response to client
        strcpy(response.args[RESPONSE_ARG_STATUS_CODE], "200");
        strcpy(response.args[RESPONSE_ARG_STATUS_MESSAGE], "OK");
        
        pack_message(packed_response, &response, UNPACK_AS_RESPONSE);
        // fprintf(stderr, "The packed response is: \n");
        debug_print_message(packed_response);
        HTTP_send(connfd, packed_response, atoi(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val), MSG_NOSIGNAL);

        // TODO: keep / close connection
        if(request.headers[REQUEST_HEADER_CONNECTION].is_active && strlen(request.headers[REQUEST_HEADER_CONNECTION].header_val) <= 5){
            close(connfd);
            fd_list[connfd].fd = -connfd;
        }

        // 8. vid upload requests: move .mp4 to web/videos/{videoname} and convert
        if(endpoint_type == ENDPOINT_API_VIDEO){
            system(video_encoding_command);
        }
    }

    // Example: sending message to client
    /*
    if(send(connfd, message, strlen(message), 0) < 0){
        ERR_EXIT("send()");
    }
    */

    // close(connfd);
    close(listenfd);
}
