#ifndef TFTP_H
#define TFTP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>


#define CMD_RRQ (short)1
#define CMD_WRQ (short)2
#define CMD_DATA (short)3
#define CMD_ACK (short)4
#define CMD_ERROR (short)5


char *conf_document_root;


#define SERVER_PORT 22900
#define MAX_REQUEST_SIZE 1024
#define DATA_SIZE 512
#define LIST_BUF_SIZE (DATA_SIZE * 8)


#define PKT_MAX_RXMT 3 //最大重试次数
#define PKT_SND_TIMEOUT 12*1000*1000 //数据包最大发送时间
#define PKT_RCV_TIMEOUT 3*1000*1000 //数据包最大接收时间
#define PKT_TIME_INTERVAL 5*1000


struct tftp_packet{
	unsigned short cmd;//操作码
	union{
		unsigned short code;//差错码
		unsigned short block;//块编号
		char filename[2];//文件名
	};
	char data[DATA_SIZE];
};

struct tftp_request{
	int size;
	struct sockaddr_in client;
	struct tftp_packet packet;
};

#endif

/*
Error Codes

   Value     Meaning

   0         未定义的错误，请查看错误信息
   1         文件不存在
   2         服务器空间已满或分配出错
   3         不合规的TFTP操作
   4         已存在同名文件
*/
