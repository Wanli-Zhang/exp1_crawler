#ifndef URL2_H_INCLUDED
#define URL2_H_INCLUDED

#include<string.h>
#include<stdlib.h>
#include<sys/types.h>
#include<regex.h>
#include"ternaryTree.h"
#include <queue>
#include "bloom2.h"
using namespace std;

#define MAX_LINK_LEN 128	/*长度大于MAX_LINK_LEN的超链接忽略 */
#define MAX_PATH_LENGTH 1024
#define MAX_LINK 160000


typedef struct {
	char *domain;
	char *ip;
	char *path;
	int number;
} Url;
extern char links[MAX_LINK][MAX_LINK_LEN];
extern int urlsNum;
extern void putlinks2queue(char* links, int num);
extern TREE *tree;
extern BFILTER* bf;
extern FILE *ffp;

void freeUrl(Url * url)
{
	free(url->domain);
	free(url->ip);
	free(url->path);
}

/*去掉开头的http[s]，如果是以“/”开头的，就把它接在domain后面*/
char *patchlink(char *link, char *domain)
{
	int len1 = strlen(link);
	int len2 = strlen(domain);
	char *rect;
	int i;
	if (strncmp(link, "http", 4) == 0) {
		int llen;
		if (strncmp(link, "https", 5) == 0)
			llen = 8;
		else
			llen = 7;
		rect = (char *)malloc(len1 - llen + 1);
		for (i = 0; i < len1 - llen; ++i)
			rect[i] = link[i + llen];
		rect[len1 - llen] = '\0';
	} else if (strncmp(link, "/", 1) == 0) {
		rect = (char *)malloc(len1 + len2 + 1);
		for (i = 0; i < len2; ++i)
			rect[i] = domain[i];
		for (i = 0; i < len1; ++i)
			rect[i + len2] = link[i];
		rect[len1 + len2] = '\0';
		//printf("在函数中补全之后的超接：|%s|\n",rect);
	} else {		/*既不是以http[s]开头，也不是以“/”开头，则返回NULL */
		return NULL;
	}

	return rect;
}

/*把超链接末尾的/去掉,长度大于MAX_LINK_LEN的超链接不爬取，把link设为NULL*/
void pretreatLink(char *link)
{
	if (link == NULL)
		return;
	//printf("预处理之前link=%s\n",link);
	int len = strlen(link);
	if (link[len - 1] == '/')	/*把超链接末尾的/去掉 */
		link[len - 1] = '\0';
	if (strlen(link) > MAX_LINK_LEN) {	/*长度大于128的超链接不爬取，把link设为NULL */
		free(link);
		link = NULL;
	}
	//printf("预处理之后link=%s\n",link);
}

/*获取超链接中资源的路径深度*/
int getDepth(char *link)
{
	int depth = 0;
	int len = strlen(link);
	int i;
	for (i = 0; i < len; ++i) {
		if (link[i] == '/')
			depth++;
	}
	return depth;
}

/*从link中获取host和resource*/
void getHRfromlink(char *link, char *host, char *resource)
{
	//printf("link=%s\n",link);
	char *p = index(link, '/');
	//printf("p=%s\n",p);
	if (p == NULL) {
		strcpy(host, link);
		resource[0] = '/';
		resource[1] = '\0';
	} else {
		int dlen = p - link;
		int plen = strlen(link) - dlen;
		strncpy(host, link, dlen);
		host[dlen] = '\0';
		strcpy(resource, p);
		resource[plen] = '\0';
	}
	printf("从link得到host=%s\tresource=%s\n",host,resource);
}



/*字符串向左平移，直到最后一个空格移到首位为止，返回字符串中还剩多少字符*/
int leftshift(char *buf)
{
	char *p = rindex(buf, ' ');
	if (p == NULL) {	/*空格没有出现，则清空buf，返回0 */
		memset(buf, 0x00, strlen(buf));
		return 0;
	} else {
		int leftlen = p - buf;
		int rightlen = strlen(buf) - leftlen;
		char *tmp = (char *)malloc(rightlen);
		strncpy(tmp, p, rightlen);
		memset(buf, 0x00, strlen(buf));
		strncpy(buf, tmp, rightlen);
		free(tmp);
		return rightlen;
	}
}

int bloomFilter(char* newLink){
    	//printf("进入过滤器\n"); 
 
  		
	if(B_get(bf, newLink, strlen(newLink))) { 
  		return 1; //链接列表中存在 
    }
    	B_set(bf,newLink,strlen(newLink));
    	return 0; //不存在 
} 

void linkRelationship(char* newLink, int currentNum){
	FILE *fp;
	fp = fopen("relation.txt","a");
	//printf("打开关系文件\n"); 
	int bloom =  bloomFilter(newLink);
	
	if(bloom == 1){ //链接列表中存在，找到相应编号 
		printf("bloom判断存在\n"); 
		int id = tr_search(tree, strlen(newLink), newLink); 
		printf("相应编号:%d\n",id);
		fprintf(fp, "%d %d\n", currentNum, id);
		//printf("输出到关系文件\n");
		fclose(fp);
	}
	else{ //不存在 
		
		if(urlsNum < MAX_LINK){ 
			printf("bloom判断不存在\n");
			strcpy(links[urlsNum], newLink);//插入链接列表 
			
			fprintf(ffp, "%d %s\n", urlsNum, links[urlsNum]);
	
			/*int i;
			printf("当前链接列表：\n");
			for(i=0;i<=urlsNum;i++){
				printf("%s\n",links[i]);
			}*/
			tr_add_string(tree, newLink, strlen(newLink), urlsNum); //插入三叉树
			putlinks2queue(newLink, urlsNum); //插入待爬取队列 
			fprintf(fp, "%d %d\n", currentNum, urlsNum);
			//printf("输出到关系文件\n");
			fclose(fp);
			urlsNum++;
		}
		else{
			fclose(fp);
			return; 
		} 
	}
}

/*提取当前连接页面中的超链接*/ 
//传入存储链接内容的数组currentpage[],当前链接域名 ,链接列表，三叉树，当前链接编号 
int extractLink(char* currentpage, char* domain, int currentNum){  

    int state = 0;
    int i = 0;
    int j = 0;
    char currentchar;
    char urlbuf[MAX_PATH_LENGTH];
    char *searchedurl;

    FILE *fp;
    fp = fopen("page.txt","a");
    fprintf(fp, "%s", currentpage);
    fclose(fp);

    
    while(currentpage[i] != '\0'){
    	currentchar = currentpage[i];
    	switch(state){
    		case 0:if(currentchar == '<'){
                           state=1; break;
                       } else {
                           state=0; j=0; break;
                       }
            	case 1:if(currentchar == 'a'){
                           state=2; break;
                       } else {
                           state=0; j=0; break;
                       }
                case 2:if(currentchar == 'h'){
                           state=3; break;
                       } else if(currentchar == '>'){
                           state=0; j=0; break;
                       } else {
                           state=2; break;
                       }
                case 3:if(currentchar == 'r'){
                           state=4; break;
                       } else if(currentchar == '>') {
                           state=0; j=0; break;
                       } else {
                           state=2; break;
                       }
                case 4:if(currentchar == 'e'){
                           state=5; break;
                       }
                       else if(currentchar == '>'){
                           state=0; j=0; break;
                       } else {
                           state=2; break;
                       }
                case 5:if(currentchar == 'f') {
                           state=6; break;
                       } else if(currentchar == '>'){
                           state=0; j=0; break;
                       } else {
                           state=2; break;
                       }
                case 6:if(currentchar == '='){
                           state=7; break;
                       }
                       else if(currentchar == '>'){
                           state=0; j=0; break;
                       } else {
                           state=2; break;
                       }
                case 7:if(currentchar == '"') {
                           state=10; break;
                       } else if(currentchar == ' ') {
                           state=7; break;
                       } else {
                           state=0; j=0; break;
                       }
                case 10:if((currentchar=='"')||(currentchar=='|')||(currentchar=='>')||(currentchar=='#')) {
                            state=0; j=0; break;
                        } else if(currentchar == '/') {
                            state=8;
                            urlbuf[j++] = currentchar;
                            break;
                        } else {
                            state=10;
                            urlbuf[j++] = currentchar;
                            break;
                        }
                case 8:if(currentchar == '"'){
                           state=9; break;
                       } else  if(currentchar=='>') {
                           state=0; j=0; break;
                       } else {
                           state=8;
                           urlbuf[j++] = currentchar;
                           break;
                       }
                case 9:urlbuf[j] = '\0';      
                       state=0;
                       int len1 = strlen(urlbuf);
                       int len2 = strlen(domain);
                       //printf("urlbuf:%s\n",urlbuf);
                       //提取以http开头的超链接，如果以http开头去掉"http://",如果以'/'开头加上domain 
                       if(strncmp(urlbuf, "http://news.cctv.com", 20) == 0){
                            searchedurl=(char*)malloc(sizeof(char)*(len1 - 7 + 1));
			    			memset(searchedurl,0,sizeof(char)*(len1 - 7 + 1));
                            int copy_i = 0;
                            for(copy_i = 0; urlbuf[copy_i + 7] != '\0'; copy_i++){
                                searchedurl[copy_i] = urlbuf[copy_i + 7];
                            }
                            searchedurl[copy_i] = '\0';
                            //putlinks2queue(&searchedurl, 1);
                            printf("1 进入extract提取链接\n"); 
                            printf("searchedurl:%s\n",searchedurl);
                            linkRelationship(searchedurl, currentNum); 
                            
                            state=0;
                            j=0;
                       }

						else if(strncmp(urlbuf, "/", 1) == 0){
							
							searchedurl=(char*)malloc(sizeof(char)*(len1 +len2 + 1));
							memset(searchedurl,0,sizeof(char)*(len1 +len2 + 1));
							int copy_j = 0;
							for(copy_j =0; copy_j < len2; copy_j++){
								searchedurl[copy_j] = domain[copy_j];	
							}
							
							for(copy_j =0; copy_j < len1; copy_j++){
								searchedurl[len2 + copy_j] = urlbuf[copy_j];
							}
							
							searchedurl[len1+len2] = '\0';
			                           
			                printf("2 进入extract提取链接\n"); 
			                printf("searchedurl:%s\n",searchedurl);
			                linkRelationship(searchedurl, currentNum);
			                            
			                state=0;
			                j=0;

                       }
                       else{
                       	    break;
                       }
                       
                       break;
        }
        i++;
    }

    return 0;
}



#endif // URL2_H_INCLUDED
