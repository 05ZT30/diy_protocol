#include "tftp.h"
#include "work_thread.h"

int send_ack(int sock, struct tftp_packet *packet, int size)
{
	if (send(sock, packet, size, 0) != size)
	{
		return -1;
	}
	return size;
}

int send_err(int sock, struct tftp_packet *packet, int err_code)
{
	memset(packet->data, 0, sizeof(packet->data));
	switch (err_code)
	{
	case 0:
		strcpy(packet->data, "未定义的错误，请查看错误信息！");
		break;
	case 1:
		strcpy(packet->data, "文件不存在！");
		break;
	case 2:
		strcpy(packet->data, "服务器空间已满或分配出错！");
		break;
	case 3:
		strcpy(packet->data, "不合规的TFTP操作！");
		break;
	case 4:
		strcpy(packet->data, "同名文件已存在！");
		break;
	}

	int size = 4 + strlen(packet->data) + 1;

	if (send(sock, packet, size, 0) != size)
	{
		return -1;
	}
	return size;
}

int send_packet(int sock, struct tftp_packet *packet, int size)
{
	struct tftp_packet rcv_packet;
	int time_wait_ack = 0; // 等待收到确认报文的时间
	int rxmt = 0;
	int r_size = 0;

	for (rxmt = 0; rxmt < PKT_MAX_RXMT; rxmt++)
	{
		printf("正在发生数据块%d\n", ntohs(packet->block));
		if (send(sock, packet, size, 0) != size)
		{
			return -1;
		}
		for (time_wait_ack = 0; time_wait_ack < PKT_RCV_TIMEOUT; time_wait_ack += 10000)
		{
			r_size = recv(sock, &rcv_packet, sizeof(struct tftp_packet), MSG_DONTWAIT);
			if (r_size >= 4 && rcv_packet.cmd == htons(CMD_ACK) && rcv_packet.block == packet->block) // 正常收到ACK
			{
				break;
			}
			usleep(10000);
		}
		if (time_wait_ack < PKT_RCV_TIMEOUT)
		{
			break;
		}
		else
		{
			continue;
		}
	}
	if (rxmt == PKT_MAX_RXMT)
	{
		printf("数据包重传达到最大次数！\n");
		return -1;
	}

	return size;
}

void *work_thread(void *arg)
{
	int sockfd;
	struct tftp_packet snd_packet; // 将要发出的数据包
	struct tftp_request *request = (struct tftp_request *)arg;
	struct sockaddr_in server;
	static socklen_t addr_len = sizeof(struct sockaddr_in);

	if (request->size <= 0)
	{
		printf("请求出错！\n");
		return NULL;
	}

	printf("子线程启动.\n");

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	{
		fprintf(stderr, "子线程套接字创建失败！--- %s\n", strerror(errno));
		return NULL;
	}

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = 0;

	if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
	{
		fprintf(stderr, "子线程套接字绑定失败：%s\n", strerror(errno));
		return NULL;
	}

	if (connect(sockfd, (struct sockaddr *)&(request->client), addr_len) < 0)
	{
		fprintf(stderr, "客户端连接失败：%s\n", strerror(errno));
		return NULL;
	}

	switch (request->packet.cmd)
	{
	case CMD_RRQ:
		printf("调用handle读请求\n");
		handle_rrq(sockfd, request);
		break;
	case CMD_WRQ:
		printf("调用handle写请求\n");
		handle_wrq(sockfd, request);
		break;
	default:
		printf("不合规的TFTP操作!\n");
		snd_packet.cmd = htons(CMD_ERROR);
		snd_packet.code = htons(1);
		send_err(sockfd, &snd_packet, 3);
		break;
	}

	free(request);
	close(sockfd);
	return NULL;
}

// 读请求处理
void handle_rrq(int sockfd, struct tftp_request *request)
{
	struct tftp_packet snd_packet;			 // 将要发出的数据包
	char fullpath[256];						 // 存储文件的路径
	char *r_path = request->packet.filename; // 请求读取的文件名称
	char *mode = r_path + strlen(r_path) + 1;
	char *blocksize_str = mode + strlen(mode) + 1;
	int blocksize = atoi(blocksize_str);
	printf("r_path = %d\n mode = %d\n blocksize_str = %d\n blocksize=%d\n",r_path,mode,blocksize_str,blocksize);
	printf("*r_path = %s\n *mode = %s\n *blocksize_str = %s\n",*r_path,*mode,*blocksize_str);
	if (blocksize <= 0 || blocksize > DATA_SIZE)
	{ // 一个包的大小不能过短或过长
		blocksize = DATA_SIZE;
	}

	if (strlen(r_path) + strlen(conf_document_root) > sizeof(fullpath) - 1)
	{
		printf("路径过长！路径长度：%d\n", strlen(r_path) + strlen(conf_document_root));
		return;
	}

	memset(fullpath, 0, sizeof(fullpath));
	strcpy(fullpath, conf_document_root);

	if (r_path[0] != '/')
	{ // 路径规范
		strcat(fullpath, "/");
	}
	strcat(fullpath, r_path);

	printf("读请求路径: \"%s\", 文件数据块大小=%d\n", fullpath, blocksize);

	FILE *fp = fopen(fullpath, "r");
	if (fp == NULL)
	{
		printf("文件不存在！\n");
		// 文件不存在handle给客户端发送出错报文
		snd_packet.cmd = htons(CMD_ERROR);
		snd_packet.code = htons(1);
		send_err(sockfd, &snd_packet, 1);
		return;
	}

	int s_size = 0;
	unsigned short block = 1;
	snd_packet.cmd = htons(CMD_DATA);
	do
	{
		memset(snd_packet.data, 0, sizeof(snd_packet.data));
		snd_packet.block = htons(block);
		s_size = fread(snd_packet.data, 1, blocksize, fp);//从fp指向的文件中读取以1字节为单位的元素blocksize个，写入data中
		if (send_packet(sockfd, &snd_packet, s_size + 4) == -1)//+4为结构体中cmd和union的大小
		{
			printf("数据传输过程中出错，数据块编号：%d.\n", block);
			goto rrq_error;
		}
		block++;
	} while (s_size == blocksize);

	printf("\n文件传输完毕！\n");

rrq_error:
	fclose(fp);

	return;
}

// 写请求处理
void handle_wrq(int sockfd, struct tftp_request *request)
{
	struct tftp_packet ack_packet, rcv_packet, err_packet;
	char fullpath[256];
	char *r_path = request->packet.filename;  // r_path存放客户端上传的文件名所在的地址
	char *mode = r_path + strlen(r_path) + 1; // mode存放模式字段开始的地址
	char *blocksize_str = mode + strlen(mode) + 1;//blocksize_str存放tftp数据包末尾的地址+1
	int blocksize = atoi(blocksize_str);//tftp数据包的大小

	if (blocksize <= 0 || blocksize > DATA_SIZE)
	{
		blocksize = DATA_SIZE;
	}

	if (strlen(r_path) + strlen(conf_document_root) > sizeof(fullpath) - 1)
	{
		printf("路径过长！路径长度：%d\n", strlen(r_path) + strlen(conf_document_root));
		return;
	}

	memset(fullpath, 0, sizeof(fullpath));
	strcpy(fullpath, conf_document_root);
	if (r_path[0] != '/')
	{
		strcat(fullpath, "/");
	}
	strcat(fullpath, r_path);

	printf("写请求路径: \"%s\", 文件数据块大小=%d\n", fullpath, blocksize);

	FILE *fp = fopen(fullpath, "r");
	if (fp != NULL)
	{
		printf("文件 \"%s\" 已存在\n", fullpath);
		// 文件已存在handle给客户端发送出错报文
		err_packet.cmd = htons(CMD_ERROR);
		err_packet.code = htons(1);
		send_err(sockfd, &err_packet, 4);
		fclose(fp);
		return;
	}

	fp = fopen(fullpath, "w");
	if (fp == NULL)
	{
		printf("文件 \"%s\" 创建失败！\n", fullpath);
		err_packet.cmd = htons(CMD_ERROR);
		err_packet.code = htons(1);
		send_err(sockfd, &err_packet, 2);
		return;
	}

	ack_packet.cmd = htons(CMD_ACK);
	ack_packet.block = htons(0);
	if (send_ack(sockfd, &ack_packet, 4) == -1)
	{ // 发送接收确认，ACK报文
		printf("发送ACK报文时出错！ 数据块序号：%d.\n");
		goto wrq_error;
	}

	int s_size = 0;
	int r_size = 0;
	unsigned short block = 1;
	int time_wait_data; // 数据传输计时器
	do
	{
		for (time_wait_data = 0; time_wait_data < PKT_RCV_TIMEOUT * PKT_MAX_RXMT; time_wait_data += 20000)
		{
			r_size = recv(sockfd, &rcv_packet, sizeof(struct tftp_packet), MSG_DONTWAIT);
			if (r_size > 0 && r_size < 4)
			{
				printf("数据包出错！ 数据大小=%d, 数据块大小=%d\n", r_size, blocksize);
			}
			if (r_size >= 4 && rcv_packet.cmd == htons(CMD_DATA) && rcv_packet.block == htons(block))
			{
				printf("DATA: 数据块序号=%d, 数据大小=%d\n", ntohs(rcv_packet.block), r_size - 4);
				fwrite(rcv_packet.data, 1, r_size - 4, fp);
				break;
			}
			usleep(20000); // sleep 2ms
		}
		if (time_wait_data >= PKT_RCV_TIMEOUT * PKT_MAX_RXMT) // 数据接收时间>超时时间*重试次数
		{
			printf("数据接收超时！\n");
			goto wrq_error;
			break;
		}

		ack_packet.block = htons(block);
		if (send_ack(sockfd, &ack_packet, 4) == -1)
		{
			printf("ACK报文发送时出错！ 数据块序号：%d.\n", block);
			goto wrq_error;
		}
		printf("数据块：%d ACK报文已发送！\n", block);
		block++;
	} while (r_size == blocksize + 4);

	printf("文件接受成功！\n");

wrq_error:
	fclose(fp);

	return;
}
