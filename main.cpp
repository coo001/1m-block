#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/netfilter.h>		/* for NF_ACCEPT */
#include <errno.h>
#include <string.h>
#include <iostream>
#include <string>
#include <set>
#include <ctime>
#include <libnetfilter_queue/libnetfilter_queue.h>

using namespace std;
set <string> sites;

int block = 0;
int site_len;
char *site;

typedef struct IP{
	uint8_t v_i;
	uint8_t tos;
	uint16_t t_len;
	uint16_t id;
	uint16_t flag_N_f_off;
	uint8_t ttl;
	uint8_t prot;
	uint16_t hd_chk;
	uint32_t src_adr;
	uint32_t dst_adr;	
}*ip_hdr;

typedef struct TCP{
	int16_t src_port;
	int16_t dst_port;
	int32_t sq_num;
	int32_t ack_num;
	int16_t do_rsv_f;
	int16_t win_size;
	int16_t chk;
	int16_t urg_p; 
}*tcp_hdr;

struct http{
	ip_hdr ipv4;
	tcp_hdr tcp;
	char data[2048];
};


void dump(unsigned char* buf, int size) {
	struct http ht;
	char site[1000];
	memcpy(&ht, buf, size);
	int i;
	for(i=0;i<2042;i++){
		if(!strncmp(ht.data + i, "Host: ",6)){
	 		break;
      		}
	}
	int a = i+6;
	for(i=a;i<size;i++){
		if(buf[i]=='\n'){
			site[i-a-1]='\0';
			break;
		}
		site[i-a]=buf[i];
	}
	if(i!=size-i-6){
		if(sites.count(string(site))){
			block=1;
			printf("Brock\n");
		}
		else{
			for(i=0;i<strlen(site);i++){
				if(site[i]=='.'){
					if(sites.count(string(site+i+1))){
						block=1;
						printf("brock\n");
						break;
					}
				}
			}
		}
	}
}

/* returns packet id */
static uint32_t print_pkt (struct nfq_data *tb)
{
	int id = 0;
	struct nfqnl_msg_packet_hdr *ph;
	struct nfqnl_msg_packet_hw *hwph;
	uint32_t mark, ifi, uid, gid;
	int ret;
	unsigned char *data, *secdata;

	ph = nfq_get_msg_packet_hdr(tb);
	if (ph) {
		id = ntohl(ph->packet_id);
		printf("hw_protocol=0x%04x hook=%u id=%u ",
			ntohs(ph->hw_protocol), ph->hook, id);
	}

	hwph = nfq_get_packet_hw(tb);
	if (hwph) {
		int i, hlen = ntohs(hwph->hw_addrlen);

		printf("hw_src_addr=");
		for (i = 0; i < hlen-1; i++)
			printf("%02x:", hwph->hw_addr[i]);
		printf("%02x ", hwph->hw_addr[hlen-1]);
	}

	mark = nfq_get_nfmark(tb);
	if (mark)
		printf("mark=%u ", mark);

	ifi = nfq_get_indev(tb);
	if (ifi)
		printf("indev=%u ", ifi);

	ifi = nfq_get_outdev(tb);
	if (ifi)
		printf("outdev=%u ", ifi);
	ifi = nfq_get_physindev(tb);
	if (ifi)
		printf("physindev=%u ", ifi);

	ifi = nfq_get_physoutdev(tb);
	if (ifi)
		printf("physoutdev=%u ", ifi);

	if (nfq_get_uid(tb, &uid))
		printf("uid=%u ", uid);

	if (nfq_get_gid(tb, &gid))
		printf("gid=%u ", gid);

	ret = nfq_get_secctx(tb, &secdata);
	if (ret > 0)
		printf("secctx=\"%.*s\" ", ret, secdata);

	ret = nfq_get_payload(tb, &data);
	if (ret >= 0)
		dump(data, ret);
	fputc('\n', stdout);

	return id;
}
	

static int cb(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg,
	      struct nfq_data *nfa, void *data)
{
	block = 0;
	uint32_t id = print_pkt(nfa);
	printf("block : %d\n\n\n", block);
	if(block) return nfq_set_verdict(qh, id, NF_DROP, 0, NULL);
	else return nfq_set_verdict(qh, id, NF_ACCEPT, 0, NULL);
}

int main(int argc, char **argv)
{
	struct nfq_handle *h;
	struct nfq_q_handle *qh;
	int fd;
	int rv;
	uint32_t queue = 0;
	char buf[4096] __attribute__ ((aligned));
	
	long t_start, t_end;
	t_start = clock();
	FILE *fp = fopen(argv[1], "r");
	char line[1000];
	int i;
	while(fgets(line, sizeof(line), fp)){
		line[strlen(line)-1]='\0';
		for(i=0;i<strlen(line);i++){
			if(line[i]!=',') continue;
			i++; break;
		}
		string site(&line[i]);
		sites.insert(site);
	}
	fclose(fp);
	t_end=clock();
	printf("%ld clocks elapsed to load list\nCLOCKS PER SEC: %ld\n", t_end - t_start, CLOCKS_PER_SEC);
	
	printf("opening library handle\n");
	h = nfq_open();
	if (!h) {
		fprintf(stderr, "error during nfq_open()\n");
		exit(1);
	}

	printf("unbinding existing nf_queue handler for AF_INET (if any)\n");
	if (nfq_unbind_pf(h, AF_INET) < 0) {
		fprintf(stderr, "error during nfq_unbind_pf()\n");
		exit(1);
	}

	printf("binding nfnetlink_queue as nf_queue handler for AF_INET\n");
	if (nfq_bind_pf(h, AF_INET) < 0) {
		fprintf(stderr, "error during nfq_bind_pf()\n");
		exit(1);
	}

	printf("binding this socket to queue '%d'\n", queue);
	qh = nfq_create_queue(h, queue, &cb, NULL);
	if (!qh) {
		fprintf(stderr, "error during nfq_create_queue()\n");
		exit(1);
	}

	printf("setting copy_packet mode\n");
	if (nfq_set_mode(qh, NFQNL_COPY_PACKET, 0xffff) < 0) {
		fprintf(stderr, "can't set packet_copy mode\n");
		exit(1);
	}

	printf("setting flags to request UID and GID\n");
	if (nfq_set_queue_flags(qh, NFQA_CFG_F_UID_GID, NFQA_CFG_F_UID_GID)) {
		fprintf(stderr, "This kernel version does not allow to "
				"retrieve process UID/GID.\n");
	}

	printf("setting flags to request security context\n");
	if (nfq_set_queue_flags(qh, NFQA_CFG_F_SECCTX, NFQA_CFG_F_SECCTX)) {
		fprintf(stderr, "This kernel version does not allow to "
				"retrieve security context.\n");
	}

	printf("Waiting for packets...\n");

	fd = nfq_fd(h);

	for (;;) {
		if ((rv = recv(fd, buf, sizeof(buf), 0)) >= 0) {
			printf("pkt received\n");
			t_start = clock();
			nfq_handle_packet(h, buf, rv);
			t_end = clock();
			printf("%ld clocks elapsed\n", t_end - t_start);
			continue;
		}
		/* if your application is too slow to digest the packets that
		 * are sent from kernel-space, the socket buffer that we use
		 * to enqueue packets may fill up returning ENOBUFS. Depending
		 * on your application, this error may be ignored. Please, see
		 * the doxygen documentation of this library on how to improve
		 * this situation.
		 */
		if (rv < 0 && errno == ENOBUFS) {
			printf("losing packets!\n");
			continue;
		}
		perror("recv failed");
		break;
	}

	printf("unbinding from queue 0\n");
	nfq_destroy_queue(qh);

#ifdef INSANE
	/* normally, applications SHOULD NOT issue this command, since
	 * it detaches other programs/sockets from AF_INET, too ! */
	printf("unbinding from AF_INET\n");
	nfq_unbind_pf(h, AF_INET);
#endif

	printf("closing library handle\n");
	nfq_close(h);
	free(site);

	exit(0);
}
