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

#define MAX_REQUEST_SIZE 1024
#define MAX_RECV_SIZE 2 * 1024 * 1024

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






#endif // HTTP2_H_INCLUDED
