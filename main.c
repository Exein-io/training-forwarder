/* Copyright 2020 Exein. All Rights Reserved.

Licensed under the GNU General Public License, Version 3.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.gnu.org/licenses/gpl-3.0.html

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include <sys/socket.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <sys/types.h>
#include <signal.h>

#define NETLINK_USER 31
#define MAX_PAYLOAD 1024 /* maximum payload size*/
#define FORWARDSRECEIVER "192.168.1.2"
#define MAX_MSG 75

typedef struct
{
	uint32_t key;
	uint8_t message_id;
	uint8_t padding;
	uint16_t tag;
	pid_t pid;
} exein_prot_req_t;

exein_prot_req_t keepalive = {
	.key = 648441310,
	.message_id = 2,
	.tag = 0,
	.padding = 0,
	.pid = 0,
};
exein_prot_req_t registration = {
	.key = 648441310,
	.message_id = 1,
	.tag = 0,
	.padding = 0,
	.pid = 0,
};
uint16_t put16w(uint16_t data)
{
#if __BYTE_ORDER__ == 4321
	uint16_t app = data << 8;
	return app + ((data & 0xff00) >> 8);
#else
	return data;
#endif
}
int udpsockfd;
long udpcount = 0, netlink_count = 0;

static void rf_handler(int sig, siginfo_t *si, void *unused)
{
	printf("Netlink Packets in: %u, udp Packets out: %u\n", netlink_count, udpcount);
	exit(0);
}

int main(int argc, char *argv[])
{
	pid_t cpid;
	struct sockaddr_nl src_addr, dest_addr;
	struct nlmsghdr *nlh = NULL;
	struct iovec iov;
	int sock_fd;
	struct msghdr msg;

	int udpsockfd;
	struct sockaddr_in addr;
	uint16_t *data;
	int pktsize;
	char *buffer;
	int cpos = 0, ipos = 0;
	uint16_t index[MAX_MSG];
	uint16_t pseq = 0xaa55;
	struct sigaction sa;

	if (argc != 6)
	{
		printf("%s <IP> <PORT> <TAG> <SECRET> <PKT_SIZE>\n", argv[0]);
		exit(-1);
	}

	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = rf_handler;
	if (sigaction(SIGINT, &sa, NULL) == -1)
	{
		printf("Receive feeds can't install the signal handler.");
	}

	//udp setup
	if ((udpsockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("socket creation failed");
		exit(-1);
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(argv[2])); //port
	inet_aton(argv[1], &addr.sin_addr);
	registration.key = atoi(argv[4]);
	keepalive.key = atoi(argv[4]);
	registration.tag = atoi(argv[3]);
	keepalive.tag = atoi(argv[3]);

	//netlink setup
	sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_USER);
	if (sock_fd < 0)
		return -1;

	memset(&src_addr, 0, sizeof(src_addr));
	src_addr.nl_family = AF_NETLINK;
	src_addr.nl_pid = getpid(); /* self pid */

	bind(sock_fd, (struct sockaddr *)&src_addr, sizeof(src_addr));

	memset(&dest_addr, 0, sizeof(dest_addr));
	memset(&dest_addr, 0, sizeof(dest_addr));
	dest_addr.nl_family = AF_NETLINK;
	dest_addr.nl_pid = 0;	 /* For Linux Kernel */
	dest_addr.nl_groups = 0; /* unicast */

	nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
	memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
	nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
	nlh->nlmsg_pid = getpid();
	nlh->nlmsg_flags = 0;

	memcpy(NLMSG_DATA(nlh), &registration, MAX_PAYLOAD);

	iov.iov_base = (void *)nlh;
	iov.iov_len = nlh->nlmsg_len;
	memset(&msg, 0, sizeof(msg));
	msg.msg_name = (void *)&dest_addr;
	msg.msg_namelen = sizeof(dest_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	printf("Sending registration message for tag %d to kernel\n", atoi(argv[3]));
	if (sendmsg(sock_fd, &msg, 0) < 0)
	{
		printf("error occurred in sending nlmsg\n");
		exit(-1);
	}
	printf("Waiting for kernel answer\n");

	if (recvmsg(sock_fd, &msg, 0) < 0)
	{
		printf("error occurred in receiving nlmsg\n");
		exit(-1);
	}
	printf("Received Confirmation message payload: %s\n", (char *)NLMSG_DATA(nlh));

	if (fork() != 0)
	{
		while (1)
		{ //parent
			sleep(5);
			memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
			nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
			nlh->nlmsg_pid = getpid();
			nlh->nlmsg_flags = 0;
			memcpy(NLMSG_DATA(nlh), &keepalive, MAX_PAYLOAD);
			iov.iov_base = (void *)nlh;
			iov.iov_len = nlh->nlmsg_len;
			msg.msg_name = (void *)&dest_addr;
			msg.msg_namelen = sizeof(dest_addr);
			msg.msg_iov = &iov;
			msg.msg_iovlen = 1;
			sendmsg(sock_fd, &msg, 0);
		}
	}
	else
	{ //child
		buffer = malloc(atoi(argv[5]) + MAX_MSG);
		printf("Receiving data\n");
		memset(index, 0, sizeof(index));
		while (1)
		{
			recvmsg(sock_fd, &msg, 0);
			netlink_count++;
			data = (uint16_t *)NLMSG_DATA(nlh);
			pktsize = (*(data + 5) - *(data + 4)) + 7;
			if (*(data + pktsize - 1) == pseq)
				printf("Duplicate detected seqn=%d\n", *(data + pktsize - 1));
			pseq = *(data + pktsize - 1);
			for (int i = 0; i < pktsize; i++)
				*(((uint16_t *)(buffer + cpos * 2 + sizeof(index)) + i)) = put16w(*(((uint16_t *)NLMSG_DATA(nlh)) + i));

			index[ipos++] = cpos;
			cpos += pktsize;
			if (((cpos * 2 + sizeof(index)) > atoi(argv[5])) || (ipos > MAX_MSG - 1))
			{
				for (int i = 0; i < MAX_MSG; i++)
					*(((uint16_t *)buffer) + i) = put16w(index[i]);

				sendto(udpsockfd, buffer, atoi(argv[5]) + MAX_MSG, 0, (const struct sockaddr *)&addr, sizeof(addr));
				udpcount++;
				ipos = 0;
				cpos = 0;
				memset(index, 0, sizeof(index));
			}
		}
	}
	close(sock_fd);
}
