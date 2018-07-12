#ifndef bloom_H_INCLUDED
#define bloom_H_INCLUDED

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<unistd.h>
#include<string.h>
 
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
 
#define HNUM 4
 
 typedef struct BloomFilter{
 	uint32_t hashf[HNUM]; // hash 函数个数
	uint8_t * bit_table;  //位表
 	uint32_t bitSize;     // 位总共大小
 }BFILTER;
 
 const unsigned char bitMask[8] = {// 屏蔽位  
 	0x01,  //00000001 
	0x02,  //00000010 
	0x04,  //00000100 
 	0x08,  //00001000 
 	0x10,  //00010000 
 	0x20,  //00100000 
 	0x40,  //01000000 
 	0x80   //10000000 
 };
 void generate_seed(BFILTER *psBFilter) {   //内部使用                                    						    
	  const uint32_t predef_salt[16] = { 0x0CD5DA28, 0x6E9E355A, 0x689B563E, 0x0C9831A8, 0x6753C18B, 0xA622689B,
	    	0x8CA63C47, 0x42CC2884, 0x8E89919B, 0x6EDBD7D3, 0x15B6796C,
	    	0x1D6FDFE4, 0x63FF9092, 0xE7401432, 0xEFFE9412, 0xAEAEDF79,
	   };

	  int i = 0;
	  for (i = 0; i < HNUM; i++) {
	   	psBFilter->hashf[i] = predef_salt[i];
	  }
	  for (i = 0; i < HNUM; i++) {
	   	psBFilter->hashf[i] = psBFilter->hashf[i] * psBFilter->hashf[(i + 3) % HNUM] + 0xA5A5A5A5;
	  }
 
 }
uint32_t hash_ap(char* str, uint32_t nlen, uint32_t hash) {// 计算字符串的hash 值    内部使用 
 	 char* it = str;
 	 while (nlen >= 2) {

	  	hash ^= (hash << 7) ^ (*it++) * (hash >> 3);
	 	hash ^= (~((hash << 11) + ((*it++) ^ (hash >> 5))));
	  	nlen -= 2;
     }
	 if (nlen) {
	  	hash ^= (hash << 7) ^ (*it) * (hash >> 3);
	 }
//	 char *m[20] ={0};
//	itoa(hash, m, 2);
//	 printf("hash %s\n",m);
	 return hash;
}
BFILTER *B_init(uint32_t tblSize,char *fname) {  /*如果fname 不空，则从文件中读取*/
 
 	BFILTER*psBFilter = NULL;
 	psBFilter = (BFILTER *) malloc(sizeof(BFILTER));
 
 	psBFilter->bitSize=tblSize*8;
 	psBFilter->bit_table=(uint8_t*)malloc(tblSize);
 
    generate_seed(psBFilter); /*利用同样的hash函数，根据不同的参数，生成不同的值*/
	memset(psBFilter->bit_table, 0, tblSize); 
 
 	if(fname){
   
     		int fds = open(fname, O_RDONLY); 
    		if (fds < 0) {  
     			printf("%s,%d,can't open the file %s\n",__FILE__,__LINE__,fname);
  		}   
  		else  {  
 	   		uint32_t bSize=64*1024;
    			ssize_t sr = 0; 
     			ssize_t len=0;
    			while((len=read(fds,psBFilter->bit_table+sr,bSize))>0){
      				sr+=len;
       			}  
      			close(fds); 
 
   		} 
 	}
   return psBFilter;
}
/*最好保证除数为素数*/
void B_set(BFILTER *pb,char* key,int nLen) {  
 	int i=0;
 	int bitIndex;
 	for(;i<HNUM;i++){
  		bitIndex=hash_ap(key,nLen,pb->hashf[i]) % (pb->bitSize-1);
  		pb->bit_table[bitIndex/8] |=bitMask[bitIndex%8];
 	}
}  
int B_get(BFILTER *pb,char* key,int nLen) {  
 	int i=0;
 	int bitIndex;
 	int bit;
 	for(;i<HNUM;i++){
  		bitIndex=hash_ap(key,nLen,pb->hashf[i]) % (pb->bitSize-1);
  		//printf("index:%d",bitIndex);
   		bit  = bitIndex % 8; 
  		if((pb->bit_table[bitIndex/8] & bitMask[bit] ) !=bitMask[bit])
  			 return 0;
 	}
 return 1; /*exist*/
}
 
 
int B_free(BFILTER *pb){
 if(pb){
  	free(pb->bit_table);
  	free(pb);
  	pb=NULL;
  }
}
/*p0=(1-1/m)^nk
   p=(1-p0)^k
*/

#endif


