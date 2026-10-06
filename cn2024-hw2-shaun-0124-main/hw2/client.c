#include "format.h"

// ./client args
char host_name[VAL_SIZE], host_IP[VAL_SIZE];
char host_fullname[VAL_SIZE];
char credentials[VAL_SIZE];
char username[VAL_SIZE]; // need this?
char password[VAL_SIZE]; // need this?

// large arrays
char packed_request[BUFF_SIZE];
char packed_response[BUFF_SIZE];
struct HTTPMessage request;
struct HTTPMessage response;

int main(int argc, char *argv[]){

    // credentials
    bool credentials_validated = 0;

    // 3.1 Check arg count
    if(argc != 3 && argc != 4){
        fprintf(STDERR_OUT, "Usage: ./client [host] [port] [username:password] \n");
        return -1;
    }

    // no [username:password]: no credentials check, but do not allow put/putv
    if(argc == 3){
        credentials_validated = 1;
    }

    // port
    PORT = atoi(argv[2]);

    // TODO: 3.2.1 resolve domain name
    // ChatGPT
    strcpy(host_name, argv[1]);
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // AF_INET for IPv4, AF_INET6 for IPv6, AF_UNSPEC for either
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    if(getaddrinfo(host_name, NULL, &hints, &res) != 0) ERR_EXIT("getaddinfo failed");

    void *ip_addr;
    char *ipver;

    // Get the pointer to the address itself (different fields for IPv4 and IPv6)
    if (res -> ai_family == AF_INET) { // IPv4
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)res -> ai_addr;
        ip_addr = &(ipv4 -> sin_addr);
    } else { // IPv6
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)res -> ai_addr;
        ip_addr = &(ipv6 -> sin6_addr);
    }

    // Convert the IP to a string and print it
    inet_ntop(res -> ai_family, ip_addr, host_IP, sizeof host_IP);
    // fprintf(stderr, "Domain name is: %s\n", host_IP);

    // Parse args: full name of host (IP + port), username, password
    strcpy(host_fullname, host_name);
    strcat(host_fullname, ":");
    strcat(host_fullname, argv[2]);
    strcpy(credentials, "");
    if(argc == 4){
        strcpy(credentials, argv[3]);
        // need this?
        strcpy(username, credentials);
        int tmp = 0;
        while(argv[3][tmp] != ':') tmp++;
        username[tmp] = '\0';
        strcpy(password, credentials + tmp + 1);
        // fprintf(stderr, "creds: %s\n", credentials); // ok
        // fprintf(stderr, "username: %s\n", username); // ok
        // fprintf(stderr, "password: %s\n", password); // ok
    }
    
    // vvvvvvvv BUILD CONNECTION WITH SERVER vvvvvvvv
    int sockfd;
    struct sockaddr_in addr;
    // char buffer[BUFF_SIZE] = {};

    // Get socket file descriptor
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        ERR_EXIT("socket()");
    }

    // Set server address
    bzero(&addr,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(PORT);

    // Connect to the server
    if(connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        ERR_EXIT("connect()");
    }
   
    // Example: receive message from server
    /*
    ssize_t n;
    if((n = read(sockfd, buffer, sizeof(buffer))) < 0){
        ERR_EXIT("read()");
    }
    */
    // ^^^^^^^^ BUILD CONNECTION WITH SERVER ^^^^^^^^

    // 3.3 Shell
    while(1){
        
        char command[VAL_SIZE];
        char after_command[VAL_SIZE]; // ???
        char* decoded_filepath, encoded_filepath[VAL_SIZE];
        
        // 3.1 Check creds
        // Creds not validated yet - send a pseudo-request and check if 401
        // fprintf(stderr, "Validated?: %d\n", credentials_validated);
        if(!credentials_validated){
            // do not ask user for command and args
            // fprintf(stderr, "VALIDATE\n");
        }

        // Creds validated - ask user for command and args
        else{
            printf("> ");
            scanf("%s", command);
            int tmp = 0;
            char tc;
            while((tc = getchar()) != '\n') after_command[tmp++] = tc;
            after_command[tmp] = '\0';
            tmp = 0;
            while(after_command[tmp] == ' ') tmp++;
            decoded_filepath = after_command + tmp;
            
            // Encode filepath
            strcpy(encoded_filepath, decoded_filepath);
            url_encode(encoded_filepath);
        }

        // Pack HTTP request:
        init_request(&request);
        
        // - command related
        if(!credentials_validated){
            strcpy(request.args[REQUEST_ARG_METHOD], "GET");
            strcpy(request.args[REQUEST_ARG_QUERY], "/upload/file");
        }

        else if(strcmp(command, "put") == 0 ||
                strcmp(command, "putv") == 0
            ){
            
            if(strlen(decoded_filepath) == 0){
                fprintf(STDERR_OUT, "Usage: %s [file]\n", command);
                continue;
            }

            // if no credentials: fail
            if(argc == 3){
                fprintf(STDERR_OUT, "Command failed.\n");
                continue;
            }

            // open the file to be uploaded
            FILE* upload_file;
            // char fopen_method[VAL_SIZE];
            // if(strcmp(command, "put") == 0) strcpy(fopen_method, "r");
            // if(strcmp(command, "putv") == 0) strcpy(fopen_method, "rb");
            if((upload_file = fopen(decoded_filepath, "rb")) == NULL){
                fprintf(STDERR_OUT, "Command failed.\n");
                continue;
            }

            // request body:
            // CONSIDER BINARY FILES, USE memcpy() INSTEAD
            int dest_len = 0;

            // - boundary
            binary_append(request.body + dest_len, "--MULTIPARTBOUNDARY\r\n");

            // - Content-Disposition, filename, Content-Type
            binary_append(request.body + dest_len, "Content-Disposition: form-data; name=\"upfile\"; filename=\"");
            binary_append(request.body + dest_len, decoded_filepath);
            binary_append(request.body + dest_len, "\"\r\nContent-Type: ");
            if(strcmp(command, "put") == 0) binary_append(request.body + dest_len, "text/plain");
            if(strcmp(command, "putv") == 0) binary_append(request.body + dest_len, "video/mp4");
            binary_append(request.body + dest_len, "\r\n\r\n");

            // - actual body
            int file_c; // = =
            int nullbytes = 0;
            struct timeval start;
            gettimeofday(&start, NULL);
            // fprintf(stderr, "time: %ld.%ld\n", start.tv_sec, start.tv_usec);
            
            // copy upload_file to request.body
            fseek(upload_file, 0, SEEK_END);
            int file_size = ftell(upload_file);
            fseek(upload_file, 0, SEEK_SET);
            fread(request.body + dest_len, 1, file_size, upload_file);
            dest_len += file_size;

            fclose(upload_file);
            // fprintf(stderr, "nullbytes read from file: %d\n", nullbytes);
            gettimeofday(&start, NULL);
            // fprintf(stderr, "time: %ld.%ld\n", start.tv_sec, start.tv_usec);

            binary_append(request.body + dest_len, "\r\n");

            // - closing boundary
            binary_append(request.body + dest_len, "--MULTIPARTBOUNDARY--");

            strcpy(request.args[REQUEST_ARG_METHOD], "POST");
            if(strcmp(command, "put") == 0) strcpy(request.args[REQUEST_ARG_QUERY], "/api/file");
            if(strcmp(command, "putv") == 0) strcpy(request.args[REQUEST_ARG_QUERY], "/api/video");
            request.headers[REQUEST_HEADER_CONTENT_TYPE].is_active = 1;
            strcpy(request.headers[REQUEST_HEADER_CONTENT_TYPE].header_val, "multipart/form-data; boundary=\"MULTIPARTBOUNDARY\"");
            itoa(dest_len, request.headers[REQUEST_HEADER_CONTENT_LENGTH].header_val, 10);
        }

        else if(strcmp(command, "get") == 0){
            if(strlen(decoded_filepath) == 0){
                fprintf(STDERR_OUT, "Usage: get [file]\n");
                continue;
            }
            char* file_format = decoded_filepath;
            while(*file_format != '.') file_format++;
            if(strncmp(file_format, ".mp4", 4) == 0) strcpy(request.args[REQUEST_ARG_QUERY], "/api/video/");
            else strcpy(request.args[REQUEST_ARG_QUERY], "/api/file/");
            strcpy(request.args[REQUEST_ARG_METHOD], "GET");
            strcat(request.args[REQUEST_ARG_QUERY], encoded_filepath);
        }

        else if(strcmp(command, "quit") == 0){
            printf("Bye.\n");
            break;
        }

        else{
            fprintf(STDERR_OUT, "Command Not Found.\n");
            continue;
        }

        // - host
        strcpy(request.headers[REQUEST_HEADER_HOST].header_val, host_fullname);

        // - authorization
        int decoded_len = strlen(credentials);
        int encoded_len;
        // fprintf(stderr, "creds: %s, len = %d\n", credentials, decoded_len);
        if(decoded_len > 0){
            char* encoded_credentials = base64_encode((unsigned char*)credentials, decoded_len, (size_t*)&encoded_len);
            // fprintf(stderr, "encoded len = %d\n", encoded_len);
            encoded_credentials[encoded_len] = '\0'; // ???????????
            // fprintf(stderr, "encoded creds: %s\n", encoded_credentials);
            request.headers[REQUEST_HEADER_AUTHORIZATION].is_active = 1;
            strcpy(request.headers[REQUEST_HEADER_AUTHORIZATION].header_val, "Basic ");
            strcat(request.headers[REQUEST_HEADER_AUTHORIZATION].header_val, encoded_credentials);
            // fprintf(stderr, "encoded creds: %s\n", encoded_credentials);
        }

        pack_message(packed_request, &request, UNPACK_AS_REQUEST);
        
        // debug zone
        // fprintf(stderr, "The packed request is:\n");
        debug_print_message(packed_request);
        // debug zone

        // Send HTTP request to server
        // fprintf(stderr, "writing\n");
        if(HTTP_send(sockfd, packed_request, atoi(request.headers[REQUEST_HEADER_CONTENT_LENGTH].header_val), MSG_NOSIGNAL) < 0){
            ERR_EXIT("server disconnected");
        }
        // fprintf(stderr, "write complete\n");

        // Receive HTTP response from server
        // char packed_response[BUFF_SIZE]; // MOVED
        // struct HTTPMessage response; // MOVED
        // fprintf(stderr, "reading\n");
        if(HTTP_recv(sockfd, packed_response, MSG_WAITALL) < 0){
            ERR_EXIT("server disconnected");
        }

        // fprintf(stderr, "read complete\n");
        unpack_message(packed_response, &response, UNPACK_AS_RESPONSE);
        
        // fprintf(stderr, "The packed response is:\n");
        debug_print_message(packed_response);

        // (for odd server) if connection closed: reconnect to server
        if(response.headers[RESPONSE_HEADER_CONNECTION].is_active && strlen(response.headers[RESPONSE_HEADER_CONNECTION].header_val) <= 5){ // "close"
            close(sockfd);
            if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
                ERR_EXIT("socket()");
            }
            if(connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
                ERR_EXIT("connect()");
            }
        }

        // If credentials invalid: print error and terminate shell
        if(!credentials_validated){
            if(strcmp(response.args[RESPONSE_ARG_STATUS_CODE], "401") == 0){
                fprintf(STDERR_OUT, "Invalid user or wrong password.\n");
                break;
            }
            credentials_validated = 1;
            continue;
        }

        // get: save file IF SUCCEEDED
        if(strcmp(command, "get") == 0 && strcmp(response.args[RESPONSE_ARG_STATUS_CODE], "200") == 0){
            // fprintf(stderr, "response ok\n");
            char local_filepath[VAL_SIZE] = "files/";
            strcat(local_filepath, decoded_filepath);
            // fprintf(stderr, "local filepath = %s\n", local_filepath);
            mkdir("files", 0777);

            // for files from web/videos/{videoname}
            char local_filepath2[VAL_SIZE];
            strcpy(local_filepath2, local_filepath);
            char* tmp = local_filepath2 + 6;
            while(*tmp != '/' && *tmp != '\0'){
                tmp++;
                if(*tmp == '/'){
                    *tmp = '\0';
                    // fprintf(stderr, "local filepath 2 = %s\n", local_filepath2);
                    mkdir(local_filepath2, 0777);
                }
            }

            FILE* requested_file = fopen(local_filepath, "wb");
            fwrite(response.body, 1, atoi(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val), requested_file);
            fclose(requested_file);

            int nullbytes = 0;
            for(int i = 0; i < atoi(response.headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val); i++){
                if(response.body[i] == 0) nullbytes++;
            }
            // fprintf(stderr, "file saved, null byes = %d\n", nullbytes);
        }

        // TODO: Print result
        if(strcmp(response.args[RESPONSE_ARG_STATUS_CODE], "200") == 0){
            printf("Command succeeded.\n");
        }
        else{
            fprintf(STDERR_OUT, "Command failed.\n");
        }

    }

    close(sockfd);

    return 0;
}
