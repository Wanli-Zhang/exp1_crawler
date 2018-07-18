#ifndef HTTP2_H_INCLUDED
#define HTTP2_H_INCLUDED

#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<errno.h>
#include<stdio.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/epoll.h>

#include<sys/stat.h>
#include<fcntl.h>

#include <time.h>

#include"url2.h"

#include <iostream>
#include "decompress.hpp"
#include "compress.hpp"
#include "utils.hpp"
#include "version.hpp"
#include "config.hpp"

#define MAX_REQUEST_SIZE 1024
#define MAX_RECV_SIZE 2 * 1024 * 1024
#define buff 40960

int connect_pending = 0;
char buf[MAX_RECV_SIZE] = {0};

int recv_response_call_time = 0;

typedef struct
{
    Url *url;
    int fd;
} Ev_arg;
extern int epfd;
extern int page_num;
extern struct epoll_event ev;
extern struct epoll_event events[2560];

/*设置文件描述符为非阻塞模式*/
void setnoblocking(int sockfd)
{
    int opts;
    opts = fcntl(sockfd, F_GETFL);	//获取文件标志和访问模式
    if (opts < 0)
    {
        perror("fcntl(sockfd,GETFL)");
        exit(1);
    }
    opts |= O_NONBLOCK;	//非阻塞
    if (fcntl(sockfd, F_SETFL, opts) < 0)
    {
        perror("fcntl(sockfd,SETFL)");
        exit(1);
    }
}

int buildConnect(int *client_sockfd, char *ip)
{
    /*创建服务器套接口地址 */
    struct sockaddr_in server_address;
    bzero(&server_address, sizeof(struct sockaddr_in));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(80);
    if (inet_aton(ip, &(server_address.sin_addr)) != 1)  	//将点分十进制形式转换为套接口内部数据类型
    {
        perror("inet_pton");
        printf("ip=%s\n", ip);
        return -1;
    }
    if ((*client_sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        return -1;
    }
    /*连接到服务器 */
    if (connect(*client_sockfd, (struct sockaddr *)&server_address,
                sizeof(struct sockaddr_in)) == -1)
    {
        close(*client_sockfd);
        perror("connect");
        return -1;
    }
    connect_pending++;
    return 0;
}

/*发送http request*/
int sendRequest(Url * url, int fd)
{
    char request[MAX_REQUEST_SIZE] = { '\0' };
    sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", url->path,
            url->domain);
    int need = strlen(request);	/*还需要向sockfd中写入这么多的数据 */
    int tmp = 0;		/*记录已发送的数据量 */
    int n;			//记录读写的实际字节数
    while (need > 0)
    {
        n = write(fd, request + tmp, need);
        if (n < 0)
        {
            if (errno == EAGAIN)  	/*写缓冲队列已满，延时后重试 */
            {
                usleep(1000);
                continue;
            }
            freeUrl(url);	/*如果是其他错误则返回-1,表示发送失败,同时释放url */
            close(fd);
            return -1;
        }
        need -= n;
        tmp += n;
    }
    return 0;
}

int hex2dec(char* hex);
char * my_strstr(char * str1, const int size1, const char * str2, const int size2);
bool all_hex_char(char* str, int l, int r);

// 0: 正常返回
//-1: HTTP Response非200OK
// 1: 数据无法通过gzip解压
// 2: 传输超时
int recvResponse(Ev_arg* arg, char* buf)
{
    char text[buff];
    Url *url = arg->url;
    int fd = arg->fd;
    int buf_len = 0;
    int total_chunk_size = 0;
    int read_length = 0, eachRead = 0;
    memset(buf, 0, MAX_RECV_SIZE);
    int j = 0;
    int body_start_pos = 0;
    int max_body_start_pos = 0;
    bool body_start = false;
    bool http_end = false;
    bool is_gzip = false;
    bool is_chunked = false;
    bool res200ok = false;
    int timeout = 10000;
    while(!http_end && timeout>0)  //读
    {
        eachRead = read(fd, text, buff);
        if (eachRead <= 0)
        {
//            printf("sleep1ms.\n");
            timeout--;
            usleep(1000);
            continue;
        }
        char * search = text;
        char * pos = NULL;
        int search_size = eachRead;
        body_start_pos = 0;
        
        if (!body_start) // http response body starts
        {
            if (my_strstr(search, search_size, "200 OK", 6))
            {
                res200ok = true;
            }
            if (my_strstr(search, search_size, "gzip", 4))
            {
                is_gzip = true;
            }
            if (my_strstr(search, search_size, "chunked", 7))
            {
                is_chunked = true;
            }
            if (pos = my_strstr(search, search_size, "\r\n\r\n", 4))
            {
                body_start = true;
                search_size -= pos - search;
                search = pos;
                search += 2;
                search_size -= 2;
                body_start_pos = search - text;
                max_body_start_pos = body_start_pos;
            }
        }
        if (!body_start)
            continue;
        
        if (!res200ok)
            return -1;
        
        if (is_chunked)
        {
            while (pos = my_strstr(search, search_size, "\r\n", 2))
            {
                search_size -= pos - search;
                search = pos;
                int chunk_size = 0;
                char chunk_size_char[10];
                if (search - text <= 5 && all_hex_char(text, 0, search-text))
                {
                    strncpy(chunk_size_char, text, search - text);
                    chunk_size_char[search - text] = '\0';
                    int chunk_size_int = hex2dec(chunk_size_char);
                    total_chunk_size += chunk_size_int;
                    if (chunk_size_int == 0)
                    {
                        http_end = true;
                        break;
                    }
                }
                if (all_hex_char(search, 2, 3))
                {
                    char * search2 = my_strstr(search+2, search_size-2, "\r\n", 2);
                    if (search2 - search - 2 <= 5 && all_hex_char(search, 2, search2-search))
                    {
                        strncpy(chunk_size_char, search + 2, search2 - search - 2);
                        chunk_size_char[search2-search-2] = '\0';
                        int chunk_size_int = hex2dec(chunk_size_char);
                        total_chunk_size += chunk_size_int;
                        search_size -= search2 - search;
                        search = search2;
                        if (chunk_size_int == 0)
                        {
                            http_end = true;
                            break;
                        }
                    }
                }
                search += 2;
                search_size -= 2;
            }
        }
        if (!is_chunked)
        {
            if (strstr(text, "</html>"))
                http_end = true;
        }
        
        if (body_start)
        {
            for (int i=0; i<eachRead - body_start_pos; i++)
                buf[buf_len+i] = text[i + body_start_pos];
            buf_len += eachRead - body_start_pos;
        }
        //            printf("eachRead=%d, body_start_pos=%d, buf_len=%d\n", eachRead, body_start_pos, buf_len);
        memset(text, '\0', buff);
        read_length += eachRead;
    }
    //        printf("buf_len=%d, read_length-body_start_pos=%d, total chunk size=%d\n", buf_len, read_length-max_body_start_pos, total_chunk_size);
    if (timeout <= 0)
    {
        return 2;
    }
    
    if (is_chunked)
    {
        char buf_dechunk[MAX_RECV_SIZE];
        memset(buf_dechunk, 0, MAX_RECV_SIZE);
        int len_dechunk = 0;
        int chunk_start[100], chunk_end[100];
        memset(chunk_start, 0, sizeof(chunk_start));
        memset(chunk_end, 0, sizeof(chunk_end));
        int chunk_num = 0;
        int last_chunk_size = -1;
        char * pos1, * pos2;
        char * pos_now = buf;
        int len_remaining = buf_len;
        while (last_chunk_size != 0)
        {
            pos1 = my_strstr(pos_now, len_remaining, "\r\n", 2);
            pos1 += 2;
            len_remaining -= pos1 - pos_now;
            pos_now = pos1;
            if (all_hex_char(pos1, 0, 1))
            {
                pos2 = my_strstr(pos_now, len_remaining, "\r\n", 2);
                if (pos2-pos1 <= 5 && all_hex_char(pos1, 0, pos2 - pos1))
                {
                    char chunk_size_char[10];
                    strncpy(chunk_size_char, pos1, pos2 - pos1);
                    chunk_size_char[pos2 - pos1] = '\0';
                    last_chunk_size = hex2dec(chunk_size_char);
                    
                    if (chunk_start[chunk_num] != 0)
                    {
                        chunk_end[chunk_num] = pos1 - buf - 2;
                        chunk_num++;
                    }
                    if (last_chunk_size != 0)
                        chunk_start[chunk_num] = pos2 - buf + 2;
                    
                    len_remaining -= pos2 + 2 - pos_now;
                    pos_now = pos2 + 2;
                }
            }
        }
        for (int i=0; i<chunk_num; i++)
        {
            for (int j=chunk_start[i]; j<chunk_end[i]; j++)
            {
                buf_dechunk[len_dechunk++] = buf[j];
            }
        }
        buf_dechunk[len_dechunk] = '\0';
        
        if (is_gzip)
        {
            bool gziped = gzip::is_compressed(buf_dechunk, len_dechunk);
            if (gziped)
            {
                std::string string_degzip = gzip::decompress(buf_dechunk, len_dechunk);
                //                    std::cout << "string_degzip:" << std::endl << string_degzip;
                strcpy(buf, string_degzip.data());
            }
            else
            {
                printf("Error: Is not compressed by gzip.\n");
                return 1;
            }
        }
        else
        {
            strcpy(buf, buf_dechunk);
        }
    }
    return 0;
}

int hex2dec(char* hex)
{
    int dec = 0, temp = 0;
    for (int i=0; i<strlen(hex); i++)
    {
        if (hex[i]<='9' && hex[i]>='0')
            temp = hex[i] - '0';
        else
            temp = hex[i] - 'a' + 10;
        dec *= 16;
        dec += temp;
    }
    return dec;
}

char * my_strstr(char * str1, const int size1, const char * str2, const int size2)
{
    for (int i=0; i<size1; i++)
    {
        bool ok = true;
        for (int j=0; j<size2; j++)
            if (i+j >= size1 || str1[i+j] != str2[j])
            {
                ok = false;
                break;
            }
        if (ok)
            return str1 + i;
    }
    return NULL;
}

//return if str[l] to str[r-1] all are hex char
bool all_hex_char(char* str, int l, int r)
{
    for (int i=l; i<r; i++)
    {
        if (!(str[i]>='0' && str[i]<='9' || str[i]>='a' && str[i]<='f'))
            return false;
    }
    return true;
}


#endif // HTTP2_H_INCLUDED
