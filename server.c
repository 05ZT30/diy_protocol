#include "tftp.h"
#include "work_thread.h"

void config()
{
	conf_document_root = "/home/molly/tftp_server_folder"; // 服务器提供的客户访问文件夹
}

int main(int argc, char **argv)
{
	int sockfd;
	int done = 0; // Server exit.
	socklen_t addr_len;
	pthread_t t_id;
	struct sockaddr_in server;
	unsigned short port = SERVER_PORT;

	printf("服务器默认运行在端口%d\n", port);

	config();
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) // 数据报套接字，使用无连接的udp
	{
		fprintf(stderr, "套接字创建失败！--- %s\n", strerror(errno));
		return 0;
	}

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);

	if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
	{
		fprintf(stderr, "套接字绑定失败：%s\n", strerror(errno));
		return 2;
	}

	printf("Server started at 0.0.0.0:%d.\n", port);

	struct tftp_request *request;
	addr_len = sizeof(struct sockaddr_in);
	while (!done)
	{
		request = (struct tftp_request *)malloc(sizeof(struct tftp_request));
		memset(request, 0, sizeof(struct tftp_request));
		request->size = recvfrom(
			sockfd, &(request->packet), MAX_REQUEST_SIZE, 0,
			(struct sockaddr *)&(request->client),
			&addr_len);
		request->packet.cmd = ntohs(request->packet.cmd);
		printf("收到客户端请求\n");
		if (pthread_create(&t_id, NULL, work_thread, request) != 0)
		{
			fprintf(stderr, "创建线程失败:%s\n", strerror(errno));
		}
	}

	return 0;
}