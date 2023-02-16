#include "client.h"

int sock;
struct sockaddr_in server;
socklen_t addr_len;
int blocksize = DATA_SIZE;

void help()
{
	printf("示例: 命令 参数0[,参数1,参数2...]\n");
	printf("  -下载文件:\n");
	printf("    get 远程文件名 [另存为本地文件名]\n");
	printf("  -上传文件:\n");
	printf("    put 文件名称\n");
	printf("  -命令提示\n");
	printf("    help\n");
	printf("  -结束连接\n");
	printf("    quit\n");
}

int main(int argc, char **argv)
{
	char cmd_line[LINE_BUF_SIZE];
	char *buf;
	char *arg;
	char *local_file;

	char *server_ip;
	unsigned short port = SERVER_PORT;

	addr_len = sizeof(struct sockaddr_in);

	if (argc < 2)
	{
		printf("示例: %s 服务器IP [服务器端口]\n", argv[0]);
		printf("    服务器默认运行在端口22900\n");
		return 0;
	}

	server_ip = argv[1];
	if (argc > 2)
	{
		port = (unsigned short)atoi(argv[2]);
	}
	printf("已连接到服务器%s:%d", server_ip, port);

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	{
		printf("套接字创建失败！\n");
		return 0;
	}
	help();

	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	inet_pton(AF_INET, server_ip, &(server.sin_addr.s_addr));

	while (1)
	{
		printf(">> ");
		memset(cmd_line, 0, LINE_BUF_SIZE);
		buf = fgets(cmd_line, LINE_BUF_SIZE, stdin);

		arg = strtok(buf, " \t\n"); // 返回以制表符为分割的buf中的字符串
		if (arg == NULL)
		{
			continue;
		}

		if (strcmp(arg, "get") == 0)
		{
			arg = strtok(NULL, " \t\n");
			local_file = strtok(NULL, " \t\n");
			if (arg == NULL)
			{
				printf("Error : 参数缺失！\n");
			}
			else
			{
				if (local_file == NULL)
				{
					local_file = arg; // 若没有指定本地的文件名则以远程的文件名代替
				}
				do_get(arg, local_file);
			}
		}
		else if (strcmp(arg, "put") == 0)
		{
			arg = strtok(NULL, " \t\n");
			if (arg == NULL)
			{
				printf("Error : 参数缺失！\n");
			}
			else
			{
				do_put(arg);
			}
		}
		else if (strcmp(arg, "quit") == 0)
		{
			printf("\nBye.\n");
			break;
		}
		else if (strcmp(arg, "help") == 0)
		{
			help();
		}
		else
		{
			printf("未知指令！.\n");
		}
	}
	return 0;
}

void do_get(char *remote_file, char *local_file)
{
	struct tftp_packet snd_packet, rcv_packet;
	struct sockaddr_in sender;

	int r_size = 0;
	int time_wait_data;
	unsigned short block = 1;

	// Send request.
	snd_packet.cmd = htons(CMD_RRQ);
	sprintf(snd_packet.filename, "%s%c%s%c%d%c", remote_file, 0, "octet", 0, blocksize, 0);
	sendto(sock, &snd_packet, sizeof(struct tftp_packet), 0, (struct sockaddr *)&server, addr_len);

	FILE *fp = fopen(local_file, "w");
	if (fp == NULL)
	{
		printf("创建文件\"%s\"失败！\n", local_file);
		return;
	}
	else
	{
		printf("创建文件\"%s\"成功！\n", local_file);
	}

	// Receive data.
	snd_packet.cmd = htons(CMD_ACK);
	do
	{
		for (time_wait_data = 0; time_wait_data < PKT_RCV_TIMEOUT * PKT_MAX_RXMT; time_wait_data += 10000)
		{
			r_size = recvfrom(sock, &rcv_packet, sizeof(struct tftp_packet), MSG_DONTWAIT,
							  (struct sockaddr *)&sender,
							  &addr_len);
			if (r_size > 0 && r_size < 4)
			{
				printf("数据包出错！r_size=%d\n", r_size);
			}
			if (r_size >= 4 && rcv_packet.cmd == htons(CMD_DATA) && rcv_packet.block == htons(block)) // block确保发送和接收的块大小相同
			{
				printf("DATA: 数据块编号：%d, 数据块大小：%d\n", ntohs(rcv_packet.block), r_size - 4);
				// 发送ACK回执
				snd_packet.block = rcv_packet.block;
				sendto(sock, &snd_packet, sizeof(struct tftp_packet), 0, (struct sockaddr *)&sender, addr_len);
				fwrite(rcv_packet.data, 1, r_size - 4, fp); // 写入文件
				break;
			}
			else if (r_size >= 4 && rcv_packet.cmd == htons(CMD_ERROR))
			{
				printf("ERROR: %s\n", rcv_packet.data);
				// 发送ACK回执
				snd_packet.block = rcv_packet.block;
				sendto(sock, &snd_packet, sizeof(struct tftp_packet), 0, (struct sockaddr *)&sender, addr_len);
				goto do_get_error;
			}
			usleep(10000);
		}
		if (time_wait_data >= PKT_RCV_TIMEOUT * PKT_MAX_RXMT)
		{
			printf("数据块#%d 传输超时！\n", block);
			goto do_get_error;
		}
		block++;
	} while (r_size == blocksize + 4);

do_get_error:
	fclose(fp);
}

void do_put(char *filename)
{
	struct sockaddr_in sender;
	struct tftp_packet rcv_packet, snd_packet;
	int r_size = 0;
	int time_wait_ack;

	snd_packet.cmd = htons(CMD_WRQ);
	sprintf(snd_packet.filename, "%s%c%s%c%d%c", filename, 0, "octet", 0, blocksize, 0);
	sendto(sock, &snd_packet, sizeof(struct tftp_packet), 0, (struct sockaddr *)&server, addr_len); // 发送请求要写的文件
	for (time_wait_ack = 0; time_wait_ack < PKT_RCV_TIMEOUT; time_wait_ack += 20000)
	{
		r_size = recvfrom(sock, &rcv_packet, sizeof(struct tftp_packet), MSG_DONTWAIT,
						  (struct sockaddr *)&sender,
						  &addr_len);
		if (r_size > 0 && r_size < 4)
		{
			printf("数据包出错！r_size=%d\n", r_size);
		}
		if (r_size >= 4 && rcv_packet.cmd == htons(CMD_ACK) && rcv_packet.block == htons(0))
		{
			break;
		}
		else if (r_size >= 4 && rcv_packet.cmd == htons(CMD_ERROR))
		{
			printf("ERROR: %s\n", rcv_packet.data);
			return;
		}
		usleep(20000);
	}
	if (time_wait_ack >= PKT_RCV_TIMEOUT)
	{
		printf("无法接收到服务器确认报文！\n");
		return;
	}

	FILE *fp = fopen(filename, "r");
	if (fp == NULL)
	{
		printf("%s 文件不存在！\n", filename);
		return;
	}

	int s_size = 0;
	int rxmt;
	unsigned short block = 1;
	snd_packet.cmd = htons(CMD_DATA);
	do
	{
		memset(snd_packet.data, 0, sizeof(snd_packet.data));
		snd_packet.block = htons(block);
		s_size = fread(snd_packet.data, 1, blocksize, fp);

		for (rxmt = 0; rxmt < PKT_MAX_RXMT; rxmt++)
		{
			sendto(sock, &snd_packet, s_size + 4, 0, (struct sockaddr *)&sender, addr_len);
			printf("已发送数据块%d\n", block);
			// 等待ACK回执
			for (time_wait_ack = 0; time_wait_ack < PKT_RCV_TIMEOUT; time_wait_ack += 20000)
			{
				r_size = recvfrom(sock, &rcv_packet, sizeof(struct tftp_packet), MSG_DONTWAIT,
								  (struct sockaddr *)&sender,
								  &addr_len);
				if (r_size > 0 && r_size < 4)
				{
					printf("数据包出错！r_size=%d\n", r_size);
				}
				if (r_size >= 4 && rcv_packet.cmd == htons(CMD_ACK) && rcv_packet.block == htons(block))
				{
					break;
				}
				usleep(20000);
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
		if (rxmt >= PKT_MAX_RXMT)
		{
			printf("无法接收到服务器确认报文！\n");
			fclose(fp);
			return;
		}

		block++;
	} while (s_size == blocksize);

	printf("\n文件发送成功\n");
	fclose(fp);
	return;
}
