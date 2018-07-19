#include "url2.h"
#include "http2.h"
#include "dns.h"
#include <netdb.h>
#include <string.h>


#define MAX_QUEUE_SIZE 200000


struct epoll_event ev, events[2560];	//ev用于注册事件，events数组用于回传要处理的事件
queue < Url * >url_queue;
int epfd;
int urlsNum = 0;
int page_num = 0;
unsigned char ipp[16];
char links[MAX_LINK][MAX_LINK_LEN];
TREE *tree;
BFILTER* bf;
FILE *ffp;

void putlinks2queue(char *links, int count);
void addurl2queue(Url * url);
void mergeFiles();

extern void ngethostbyname(unsigned char* , unsigned char*);
extern void get_dns_servers();

int main(int argc, char *argv[]){
	int i, n;
/*bloom*/
	
	bf=B_init(16 * 160000,NULL); //初始化过滤器 
/**********************************/	
	ffp = fopen(argv[3],"w");


	tree = tr_alloc();
	

	/*dns*/
    get_dns_servers();
	unsigned char hostname[100];
	strcpy((char*)hostname, argv[1]);
	ngethostbyname(hostname, ipp);
/*
	struct hostent * result = gethostbyname(argv[1]);
	char   str[32];		
        const char* resultIp = inet_ntop(result->h_addrtype, result->h_addr, str, sizeof(str));
	strcpy(ipp, resultIp);
*/
	printf("%s\n", ipp);

	if (argc != 4) {
		printf
		    ("Usage: ./crawler domain port url.txt\n"
		     "For example:./crawler news.ifeng.com 80 url.txt\n");
		return 0;
	}
	chdir("pagecrawler");
    	FILE* logfd = fopen("log.txt", "w+");
    	if (logfd == NULL) {
        	printf("log文件创建失败\n");
        	exit(1);
   	}
	urlsNum = 0;
	strcpy(links[urlsNum], argv[1]);
	fprintf(ffp, "%s %d\n", links[urlsNum], urlsNum);
	B_set(bf,argv[1],strlen(argv[1])); 
	tr_add_string(tree, argv[1], strlen(argv[1]), urlsNum);
	putlinks2queue(argv[1], urlsNum++);	/*把用户命令行提供的link放入待爬取url队列 */

	epfd = epoll_create(2560);	//生成用于处理accept的epoll专用文件描述符，最多监听2560个事件
	n = url_queue.size() < 500 ? url_queue.size() : 500;	/*最多同时放出去2500条socket连接 */
	for (i = 0; i < n; i++) {
		Url *url = url_queue.front();
		int sockfd;
		int rect = buildConnect(&sockfd, url->ip);	/*发出connect请求 */
		setnoblocking(sockfd);	/*设置sockfd非阻塞模式 */
		rect = sendRequest(url, sockfd);	/*发送http request */
		Ev_arg *arg = (Ev_arg *) calloc(sizeof(Ev_arg), 1);
		arg->url = url;
		arg->fd = sockfd;
		ev.data.ptr = arg;
		ev.events = EPOLLIN | EPOLLET;	//设置要处理的事件类型。可读，边缘触发
		printf("将要把%d: [%s%s]放入epoll\n", sockfd, url->domain, url->path);
		epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);	//注册ev
		url_queue.pop();
	}

    int ask_num = 0;
	while (1) {
        clock_t start, finish;
        double  duration;
        start = clock();

		n = epoll_wait(epfd, events, 2560, 100000);	/*等待sockfd可读，即等待http response */
		if((n == 0) && (url_queue.size() == 0))
        {
            finish = clock();
            duration = (double)(finish - start) / CLOCKS_PER_SEC;
            printf("epoll_wail call : %d times\n \
                    %d pages has been crawled.\n \
                        Queue Size : %d\n", ++ask_num, page_num, 					url_queue.size());
            fprintf(logfd, "The %d ask processed in %f seconds\n \
                        %d epoll_wait has been processed.\n \
                        Queue Size : %d\n", ++ask_num, duration, n, 					url_queue.size());
            return 0;
        }
        printf("有%d个sockfd准备就绪\n", n);
        
        for (i = 0; i < n; ++i)
        {
            Ev_arg *arg = (Ev_arg *) (events[i].data.ptr);
            Url * url = arg->url;
            int fd = arg->fd;
	printf("收到[%s%s]的响应\n", url->domain, url->path);
            recvResponse(arg, buf);
	
            printf("读完了\n");
            //printf("读完了%d\n%s", strlen(buf), buf);
            //printf("Has crawled %d pages.\n", ++page_num);

            extractLink(buf, url->domain, url->number);
            close(fd);

            connect_pending--;
        }



            int add_connect = url_queue.size() < (200 - connect_pending) ? url_queue.size() : (200 - connect_pending);
            for (i = 0; i < add_connect; ++i) {
                if (url_queue.empty()){
                    //usleep(1000);
                    //exit(15);
                    continue;
                }
                Url *url = url_queue.front();
                //url_queue.pop();
                int sockfd;
                //printf("将要发起连接请求\n");
                int rect = buildConnect(&sockfd, url->ip);	/*发出connect请求 */
                setnoblocking(sockfd);	/*设置sockfd非阻塞模式 */
                rect = sendRequest(url, sockfd);	/*发送http request */
                Ev_arg *arg = (Ev_arg *) calloc(sizeof(Ev_arg), 1);
                arg->url = url;
                arg->fd = sockfd;
                ev.data.ptr = arg;
                ev.events = EPOLLIN | EPOLLET;	//设置要处理的事件类型。可读，边缘触发
                printf("将要把%d: [%s%s]放入epoll\n", sockfd, url->domain, url->path);
                epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);	//注册ev
                url_queue.pop();
            }
        
        fclose(logfd);
        logfd = fopen("log.txt", "a");

        finish = clock();
        duration = (double)(finish - start) / CLOCKS_PER_SEC;
        fprintf(logfd, "The %d ask processed in %f seconds\n \
               %d epoll_wait has been processed.\n \
                Queue Size : %d\n", ++ask_num, duration, n, url_queue.size());
	}
	close(epfd);
	B_free(bf);

	fclose(ffp);
	
	mergeFiles();

}

/*把超链接放入待爬取url队列*/
void putlinks2queue(char *links, int num){
    	int i = 0;
	char *h = (char *)calloc(MAX_LINK_LEN, 1);
	char *r = (char *)calloc(MAX_LINK_LEN, 1);
	char *iipp = (char *)calloc(20, 1);
	pretreatLink(links);
	if (links == NULL) return;
	getHRfromlink(links, h, r);
	/*if(strcmp(h, "news.sogou.com") != 0){
            		free(h);
            		free(r);
            		free(iipp);
            		return;
	}*/
	//else{
            		
		Url *tmp = (Url *) calloc(1, sizeof(Url));
		tmp->domain = h;
		tmp->path = r;
		tmp->ip = (char*)ipp;
		tmp->number = num;
		addurl2queue(tmp);
                return;
            
       // }
}

/*把url放入待爬取队列*/
void addurl2queue(Url * url)
{
	if (url == NULL || url->domain == NULL || strlen(url->domain) == 0
        || url->ip == NULL || strlen(url->ip) == 0) {
		fprintf(stderr,
			"Url内容不完整。domain=%s\tpath=%s\tip=%s\n",
			url->domain, url->path, url->ip);
		exit(1);
	}
	//fprintf(stderr,"Url内容完整。domain=%s\tpath=%s\tip=%s\n",url->domain,url->path,url->ip);
	if (url_queue.size() >= MAX_QUEUE_SIZE){	/*如果队列已满，就忽略此url */
	    printf("队列满\n");
        free(url);
		return;
	}
	url_queue.push(url);
	printf("%s%s已放入待爬取队列\n",url->domain,url->path);
}

void mergeFile()
{
	FILE *fp;
	FILE *fp2;

	fp = fopen("relation.txt","rt");
	if(fp == NULL){
        printf("relation File open error!\n");
        
    }
    
	fp2 = fopen("out.txt","a");
	if(fp2 == NULL){
        printf("out File open error!\n");
        
    }
    fprintf(fp2, "\n");
   
	
	int i=0,j=0;
	int record=0;
	int flag=0;
	int first =0;
	int currentNum=0;
	int num[10000];
	while(!feof(fp)){
		fscanf(fp,"%d",&record);
		
		num[i]=record;
		if(i%2==0 && record!=currentNum){
			//printf("首位变了\n");
			currentNum=record;
			memset(num,0,sizeof(num));
			num[0]=record;
			i=0;
		}
		else if(i%2==1 && num[i-1]==currentNum){
			//printf("首位为 %d\n",currentNum) ;
			printf("record %d\n",num[i]);
			for(j=1;j<=i-2;j=j+2){
				if(num[i]==num[j]){
					flag=1;
					break;
				}
			}
			if(flag==0){
				 printf("写入文件\n");
				fprintf(fp2, "%d %d\n", currentNum,num[i]);
			}
			flag=0;
		}
		i++;
	}

}




