void *work_thread(void *arg);

int send_ack(int sock, struct tftp_packet *packet, int size);
int send_err(int sock, struct tftp_packet *packet, int err_code);
int send_packet(int sock, struct tftp_packet *packet, int size);

void handle_rrq(int sock, struct tftp_request *request);
void handle_wrq(int sock, struct tftp_request *request);
