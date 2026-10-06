// given headers and defines
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#define ERR_EXIT(a){ perror(a); exit(1); }

// stderr that should be printed
// to enable / disable other debug messages, use replace
#define STDERR_OUT stderr

// debug
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

// port
int PORT;

// multiplexing
#include <poll.h>
#define FD_MAX 128

// bool
#include <stdbool.h>

// directory operations
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

// base64
#include "utils/base64.h"

// resolve domain name
#include <netdb.h>

// EXCUSE ME WTF
#define strncpy_nullterm(dest, src, num) strncpy(dest, src, num); dest[num] = '\0'
#define binary_append(dest, src) memcpy((dest), (src), strlen(src)), dest_len += strlen(src) // append string to binary, DO NOT APPEND NULL BYTES USING THIS

// my compiler does not have this :(
char* itoa(int value, char* str, int base){ // only value >= 0, base <= 10
    int digits = 1;
    int tmp_value = value;
    while(tmp_value >= base){
        tmp_value /= base;
        digits++;
    }
    str[digits] = '\0';
    int ptr = digits - 1;
    tmp_value = value;
    while(ptr >= 0){
        str[ptr] = '0' + (tmp_value % base);
        tmp_value /= base;
        ptr--;
    }
    return str;
}
/*
int atoi(char* str){
    int ret = 0;
    while(*str != '\0'){
        ret = 10 * ret + (*str - '0');
        str++;
    }
    return ret;
}
*/
// char foo[10];
// if(strcmp(itoa(1582, foo, 10), "1582") != 0) ERR_EXIT("itoa failed"); // probably ok

// string size
#define BUFF_SIZE (210 * 1024 * 1024 + 4096) // 200MB -> segmentation fault ???
#define HEADER_TYPE_SIZE 64
#define VAL_SIZE 128
#define BODY_SIZE (210 * 1024 * 1024 + 16)

// HTTP Message format
#define MESSAGE_ARG_COUNT 3
#define MESSAGE_HEADER_COUNT 6

// 3.2.2 Request format
#define REQUEST_ARG_METHOD 0
#define REQUEST_ARG_QUERY 1
#define REQUEST_ARG_VERSION 2

#define REQUEST_HEADER_HOST 0
#define REQUEST_HEADER_USER_AGENT 1
#define REQUEST_HEADER_CONNECTION 2
#define REQUEST_HEADER_CONTENT_TYPE 3
#define REQUEST_HEADER_CONTENT_LENGTH 4
#define REQUEST_HEADER_AUTHORIZATION 5

// 2.2.3 Response format
#define RESPONSE_ARG_VERSION 0
#define RESPONSE_ARG_STATUS_CODE 1
#define RESPONSE_ARG_STATUS_MESSAGE 2

#define RESPONSE_HEADER_SERVER 0
#define RESPONSE_HEADER_CONTENT_TYPE 1
#define RESPONSE_HEADER_CONTENT_LENGTH 2
#define RESPONSE_HEADER_ALLOW 3
#define RESPONSE_HEADER_WWW_AUTHENTICATE 4
#define RESPONSE_HEADER_CONNECTION 5 // only used in responses from other servers

#define UNPACK_AS_REQUEST 0
#define UNPACK_AS_RESPONSE 1

// command for video encoding
#define VIDEO_ENCODING_COMMAND "ffmpeg -re -i %s.mp4 -c:a aac -c:v libx264 \
-map 0 -b:v:1 6M -s:v:1 1920x1080 -profile:v:1 high \
-map 0 -b:v:0 144k -s:v:0 256x144 -profile:v:0 baseline \
-bf 1 -keyint_min 120 -g 120 -sc_threshold 0 -b_strategy 0 \
-ar:a:1 22050 -use_timeline 1 -use_template 1 \
-adaptation_sets \"id=0,streams=v id=1,streams=a\" -f dash \
web/videos/%s/dash.mpd &"
// %s: video name
char video_encoding_command[8 * VAL_SIZE];

// >vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv<
// >FIELD NAMES ARE CASE-INSENSITIVE<
// >^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^<
int strncmp_ci(char* a, char* b, int len){
    for(int i = 0; i < len; i++){
        char ca = a[i], cb = b[i];
        if('A' <= ca && ca <= 'Z') ca = ca - 'A' + 'a';
        if('A' <= cb && cb <= 'Z') cb = cb - 'A' + 'a';
        if(ca < cb) return -1;
        if(ca > cb) return 1;
    }
    return 0;
}

// HTTP message structure
struct Header{
    bool is_active;
    char header_type[HEADER_TYPE_SIZE];
    char header_val[VAL_SIZE];
};

struct HTTPMessage{
    char args[MESSAGE_ARG_COUNT][VAL_SIZE];
    struct Header headers[MESSAGE_HEADER_COUNT];
    char body[BODY_SIZE];
};

// returns bytes sent if full message sent, -1 otherwise
int HTTP_send(int fd, char* buffer, int body_len, int flags){
    int offset = 0;
    int ret; // return value of send()
    // fprintf(stderr, "HTTP_send: body len = %d\n", body_len);

    // send everything before double CRLF
    while(strncmp(buffer + offset, "\r\n\r\n", 4) != 0){
        ret = send(fd, buffer + offset, 1, flags);
        if(ret >= 0) offset += ret;
        else{
            // fprintf(stderr, "send failed, offset = %d\n", offset);
            // ERR_EXIT("send failed");
            return -1;
        } 
    }
    // fprintf(stderr, "HTTP_send: headers, offset = %d\n", offset);

    // send double CRLF
    ret = send(fd, buffer + offset, 4, flags);
    if(ret >= 0) offset += ret;
    else{
        // fprintf(stderr, "send failed, offset = %d\n", offset);
        // ERR_EXIT("send failed");
        return -1;
    } 
    // fprintf(stderr, "HTTP_send: double CRLF, offset = %d\n", offset);

    // send body_len bytes
    while(body_len > 0){
        ret = send(fd, buffer + offset, body_len, flags);
        if(ret >= 0){
            offset += ret;
            body_len -= ret;
        }
        else{
            // fprintf(stderr, "send failed, offset = %d\n", offset);
            // ERR_EXIT("send failed");
            return -1;
        } 
    }
    // fprintf(stderr, "HTTP_send: body, offset = %d\n", offset);
    
    return offset;
}

// returns bytes received if full message received, -1 otherwise
int HTTP_recv(int fd, char* buffer, int flags){
    int offset = 0;
    int ret;
    // fprintf(stderr, "HTTP_recv\n");

    // read until double CRLF
    while(offset < 4 || strncmp(buffer + offset - 4, "\r\n\r\n", 4) != 0){
        ret = recv(fd, buffer + offset, 1, flags);
        if(ret <= 0){
            // fprintf(stderr, "recv failed, offset = %d\n", offset);
            return -1;
        }
        offset++;
    }
    // fprintf(stderr, "HTTP_recv: headers, offset = %d\n", offset);

    // parse content length
    int ptr = 0;
    while(ptr < offset && strncmp_ci(buffer + ptr, "Content-Length: ", 16) != 0){
        ptr++;
    }
    if(ptr == offset){
        // fprintf(stderr, "HTTP_recv: no body\n");
        return offset;
    }

    ptr += 16;
    int body_len = 0;
    while('0' <= buffer[ptr] && buffer[ptr] <= '9'){
        body_len = 10 * body_len + (buffer[ptr] - '0');
        ptr++;
    }
    // fprintf(stderr, "HTTP_recv: body len = %d\n", body_len);

    // read content length
    if(body_len > 0){
        ret = recv(fd, buffer + offset, body_len, flags);
        if(ret < body_len){
            // fprintf(stderr, "recv failed, offset = %d\n", offset);
            return -1;
        }
        offset += body_len;
    }
    // fprintf(stderr, "HTTP_recv: body, offset = %d\n", offset);

    return offset;
}

// TODO?: modify it to support binary files
// Print a packed message
void debug_print_message(char* packed){
    // fprintf(stderr, "======== CURRENT MESSAGE IS ========\n");
    if(strlen(packed) > 3000){
        for(int i = 0; i < 3000; i++){ // fprintf(stderr, "%c", packed[i]);
        }
        // fprintf(stderr, "<TRUNCATED>\n");
    }
    else{ // fprintf(stderr, "%s\n", packed);
    }
    // fprintf(stderr, "======== END OF MESSAGE ============\n");
}

// Clear message and disable all headers
// So when creating a new message, only need to init and enable extra headers
void clear_message(struct HTTPMessage* message_ptr){
    for(int i = 0; i < MESSAGE_ARG_COUNT; i++){
        strcpy(message_ptr -> args[i], "");
    }
    for(int i = 0; i < MESSAGE_HEADER_COUNT; i++){
        message_ptr -> headers[i].is_active = 0;
        strcpy(message_ptr -> headers[i].header_type, "");
        strcpy(message_ptr -> headers[i].header_val, "");
    }
    strcpy(message_ptr -> body, "");
}

// Init message as request
// Default content length is "0", not "", so content length is only modified when body exists
void init_request(struct HTTPMessage* request_ptr){
    clear_message(request_ptr);
    request_ptr -> headers[REQUEST_HEADER_HOST].is_active = 1;
    request_ptr -> headers[REQUEST_HEADER_USER_AGENT].is_active = 1;
    request_ptr -> headers[REQUEST_HEADER_CONNECTION].is_active = 1;
    request_ptr -> headers[REQUEST_HEADER_CONTENT_LENGTH].is_active = 1;
    strcpy(request_ptr -> args[REQUEST_ARG_VERSION], "HTTP/1.1");
    strcpy(request_ptr -> headers[REQUEST_HEADER_HOST].header_type, "Host");
    strcpy(request_ptr -> headers[REQUEST_HEADER_USER_AGENT].header_type, "User-Agent");
    strcpy(request_ptr -> headers[REQUEST_HEADER_CONNECTION].header_type, "Connection");
    strcpy(request_ptr -> headers[REQUEST_HEADER_CONTENT_TYPE].header_type, "Content-Type");
    strcpy(request_ptr -> headers[REQUEST_HEADER_CONTENT_LENGTH].header_type, "Content-Length");
    strcpy(request_ptr -> headers[REQUEST_HEADER_AUTHORIZATION].header_type, "Authorization");
    strcpy(request_ptr -> headers[REQUEST_HEADER_USER_AGENT].header_val, "CN2024Client/1.0");
    strcpy(request_ptr -> headers[REQUEST_HEADER_CONNECTION].header_val, "keep-alive");
    strcpy(request_ptr -> headers[REQUEST_HEADER_CONTENT_LENGTH].header_val, "0");
}

// Init message as response
// Default content length is "0", not "", so content length is only modified when body exists
void init_response(struct HTTPMessage* response_ptr){
    clear_message(response_ptr);
    response_ptr -> headers[RESPONSE_HEADER_SERVER].is_active = 1;
    response_ptr -> headers[RESPONSE_HEADER_CONTENT_LENGTH].is_active = 1;
    strcpy(response_ptr -> args[RESPONSE_ARG_VERSION], "HTTP/1.1");
    strcpy(response_ptr -> headers[RESPONSE_HEADER_SERVER].header_type, "Server");
    strcpy(response_ptr -> headers[RESPONSE_HEADER_CONTENT_TYPE].header_type, "Content-Type");
    strcpy(response_ptr -> headers[RESPONSE_HEADER_CONTENT_LENGTH].header_type, "Content-Length");
    strcpy(response_ptr -> headers[RESPONSE_HEADER_ALLOW].header_type, "Allow");
    strcpy(response_ptr -> headers[RESPONSE_HEADER_WWW_AUTHENTICATE].header_type, "WWW-Authenticate");
    strcpy(response_ptr -> headers[RESPONSE_HEADER_CONNECTION].header_type, "Connection");
    strcpy(response_ptr -> headers[RESPONSE_HEADER_SERVER].header_val, "CN2024Server/1.0");
    strcpy(response_ptr -> headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val, "0");
}

// Pack an HTTPMessage into a string
void pack_message(char* packed, struct HTTPMessage* message_ptr, int pack_type){

    // args
    strcpy(packed, message_ptr -> args[0]);
    strcat(packed, " ");
    strcat(packed, message_ptr -> args[1]);
    strcat(packed, " ");
    strcat(packed, message_ptr -> args[2]);
    strcat(packed, "\r\n");

    // headers
    for(int i = 0; i < MESSAGE_HEADER_COUNT; i++){
        if(message_ptr -> headers[i].is_active){
            strcat(packed, message_ptr -> headers[i].header_type);
            strcat(packed, ": ");
            strcat(packed, message_ptr -> headers[i].header_val);
            strcat(packed, "\r\n");
        }
    }

    // empty line
    strcat(packed, "\r\n");
    int len = strlen(packed);

    // body
    memcpy(packed + len, message_ptr -> body, 
            atoi((pack_type == UNPACK_AS_REQUEST) ? message_ptr -> headers[REQUEST_HEADER_CONTENT_LENGTH].header_val : message_ptr -> headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val));

}

// Unpack a string into an HTTPMessage
void unpack_message(char* packed, struct HTTPMessage* message_ptr, int unpack_type){

    // fprintf(stderr, "unpack_message():\n");
    int from = 0, to = 0;
    clear_message(message_ptr);
    if(unpack_type == UNPACK_AS_REQUEST) init_request(message_ptr);
    else init_response(message_ptr);

    // args
    for(int i = 0; i < MESSAGE_ARG_COUNT; i++){
        while(packed[to] != ' ' && packed[to] != '\r') to++;
        // fprintf(stderr, "from = %d, to = %d, ", from, to);
        strncpy_nullterm(message_ptr -> args[i], packed + from, to - from);
        if(packed[to] == ' ') to++;
        else if(packed[to] == '\r') to += 2;
        from = to;
        // fprintf(stderr, "args[%d] = %s\n", i, message_ptr -> args[i]);
    }

    // headers & empty line
    char type[HEADER_TYPE_SIZE];
    int type_id;

    for(int i = 0; i < MESSAGE_HEADER_COUNT; i++){
        message_ptr -> headers[i].is_active = 0;
    }
    while(1){
        
        // empty line
        if(packed[to] == '\r'){
            to += 2;
            from = to;
            break;
        }

        // get header type
        while(packed[to] != ':') to++;
        // fprintf(stderr, "from = %d, to = %d, ", from, to);
        strncpy_nullterm(type, packed + from, to - from);
        type_id = -1;
        for(int i = 0; i < MESSAGE_HEADER_COUNT; i++){
            if(strncmp_ci(type, message_ptr -> headers[i].header_type, strlen(message_ptr -> headers[i].header_type)) == 0) type_id = i;
        }
        to += 2;
        from = to;
        // fprintf(stderr, "%s: header type = %d\n", type, type_id);

        // get header value
        while(packed[to] != '\r') to++;
        // fprintf(stderr, "from = %d, to = %d, ", from, to);
        if(type_id != -1){
            strncpy_nullterm(message_ptr -> headers[type_id].header_val, packed + from, to - from);
            message_ptr -> headers[type_id].is_active = 1;
            // fprintf(stderr, "headers[%d] = %s\n", type_id, message_ptr -> headers[type_id].header_val);
        }
        to += 2;
        from = to;
    }

    // body
    // fprintf(stderr, "unpack: body size is %d\n", atoi((unpack_type == UNPACK_AS_REQUEST) ? message_ptr -> headers[REQUEST_HEADER_CONTENT_LENGTH].header_val : message_ptr -> headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val));
    memcpy(message_ptr -> body, packed + from, 
            atoi((unpack_type == UNPACK_AS_REQUEST) ? message_ptr -> headers[REQUEST_HEADER_CONTENT_LENGTH].header_val : message_ptr -> headers[RESPONSE_HEADER_CONTENT_LENGTH].header_val));
    // while(packed[to] != '\0') to++;
    // strncpy_nullterm(message_ptr -> body, packed + from, to - from);
    // fprintf(stderr, "body = %s\n", message_ptr -> body);

    // fprintf(stderr, "End of unpack_message()\n");

}

// 1.1.4 URL encoding / decoding
void url_encode(char* dec){
    char enc[VAL_SIZE];
    int i = 0, tmp = 0;
    while(dec[i] != '\0'){

        // normal char
        if( ('A' <= dec[i] && dec[i] <= 'Z') ||
            ('a' <= dec[i] && dec[i] <= 'z') ||
            ('0' <= dec[i] && dec[i] <= '9') ||
            dec[i] == '-' ||
            dec[i] == '_' ||
            dec[i] == '.' ||
            dec[i] == '~' ||
            dec[i] == '/')
            {
            enc[tmp] = dec[i];
            tmp++;
            i++;
        }

        // encoded char
        else{
            enc[tmp] = '%';
            int hi = dec[i] / 16, lo = dec[i] % 16;
            enc[tmp + 1] = (hi >= 10 ? 'A' - 10 : '0') + hi;
            enc[tmp + 2] = (lo >= 10 ? 'A' - 10 : '0') + lo;
            tmp += 3;
            i++;
        }
    }
    enc[tmp] = '\0';
    strcpy(dec, enc);
}

void url_decode(char* enc){
    char dec[VAL_SIZE];
    int i = 0, tmp = 0;
    while(enc[i] != '\0'){
        
        // normal char
        if(enc[i] != '%'){
            dec[tmp] = enc[i];
            tmp++;
            i++;
        }

        // encoded char
        else{
            int hi = enc[i + 1] >= 'A' ? enc[i + 1] - 'A' + 10 : enc[i + 1] - '0';
            int lo = enc[i + 2] >= 'A' ? enc[i + 2] - 'A' + 10 : enc[i + 2] - '0';
            dec[tmp] = 16 * hi + lo;
            tmp++;
            i += 3;
        }
    }
    dec[tmp] = 0;
    strcpy(enc, dec);
}

// == REDO ==
// Parse one part of a multipart/form-data body
// Return 1 on success, 0 if no parts left
// Will move the body pointer
// Will write content at the given pointers (write empty string if no entry found)
bool multipart_next_part(char** body, char* boundary, char* name, char* filename, char* actual_body, int* actual_body_len, bool* is_binary){
    
    // no *& so use **
    // *(*body): first char of current body
    //  (*body): current body as a string

    // always starts and ends at a boundary

    // format:
    // <BOUNDARY><CRLF>
    // Content-Disposition: form-data(; name="<NAME>")(; filename="<FILENAME>")<CRLF>
    // Content-Type: [text/[plain/html]]/[others]<CRLF>
    // other headers?
    // <CRLF>
    // [<STRING>/<BINARY>]<CRLF>

    // (next multipart)
    // <BOUNDARY><CRLF>

    // assume all names are enclosed in ""
    // if no type found -> default type is text
    int boundary_len = strlen(boundary);
    *is_binary = 0;
    *actual_body_len = 0;
    char crlf_boundary[VAL_SIZE] = "\r\n";
    char boundary_end[VAL_SIZE];
    strcat(crlf_boundary, boundary);
    strcpy(boundary_end, boundary);
    strcat(boundary_end, "--");
    strcpy(name, "");
    strcpy(filename, "");

    if(strncmp((*body), boundary_end, boundary_len + 2) == 0){
        // fprintf(stderr, "parse: end reached\n");
        (*body) += boundary_len + 2;
        return 0;
    }

    // fprintf(stderr, "parse\n");
    // headers: search until finding double CRLF
    while(strncmp((*body), "\r\n\r\n", 4) != 0){
        // usleep(100000);
        if(strncmp_ci((*body), "name=\"", 6) == 0){
            (*body) += 6;
            while(*(*body) != '\"'){
                strncat(name, (*body), 1);
                (*body)++;
                // usleep(100000);
                // fprintf(stderr, "parse: name = %s\n", name);
            }
            // fprintf(stderr, "parse: name = %s\n", name);
        }
        else if(strncmp_ci((*body), "filename=\"", 10) == 0){
            (*body) += 10;
            while(*(*body) != '\"'){
                strncat(filename, (*body), 1);
                (*body)++;
                // usleep(100000);
                // fprintf(stderr, "parse: filename = %s\n", filename);
            }
            // fprintf(stderr, "parse: filename = %s\n", filename);
        }
        else if(strncmp_ci((*body), "Content-Type: ", 14) == 0){
            (*body) += 14;
            if(strncmp((*body), "text", 4) == 0){
                *is_binary = 0;
                // fprintf(stderr, "parse: type = text\n");
            }
            else{
                *is_binary = 1;
                // fprintf(stderr, "parse: type = binary\n");
            }
        }
        else{
            (*body)++;
        }
    }
    (*body) += 4;
    // fprintf(stderr, "parse: header done\n");

    // actual body: search until finding CRLF + boundary
    int foo = 0;
    int nullbytes = 0;

    while(strncmp((*body), crlf_boundary, 2 + boundary_len) != 0){
        actual_body[*actual_body_len] = *(*body);
        if(*(*body) == 0) nullbytes++;
        (*actual_body_len)++;
        (*body)++;
        foo++;
        if(foo % 1000000 == 0){
            // usleep(100000);
            // fprintf(stderr, "parse: body: bytes = %d\n", foo);
        }
    }
    (*body) += 2;

    // for text, add terminating char?
    // actual_body[*actual_body_len] = '\0';
    // fprintf(stderr, "parse: body done, bytes = %d\n", foo);
    // fprintf(stderr, "parse: nullbytes = %d\n", nullbytes);

    return 1;

}
