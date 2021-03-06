#ifndef WIN32
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#else
#include <winsock2.h>
#include <time.h>
#endif

#include "packet.h"
#include "tcpclient.h"
#include "diffprobe.h"

#ifndef WIN32
int connect_nonb(int sockfd, const struct sockaddr *saptr, int salen, int nsec)
{
	int flags, n, error;
	int len;
	fd_set rset, wset;
	struct timeval tval;

	flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

	error = 0;
	if ( (n = connect(sockfd, (struct sockaddr *) saptr, salen)) < 0)
		if (errno != EINPROGRESS)
		return(-1);

	/* Do whatever we want while the connect is taking place. */
	if (n == 0)
	goto done;	/* connect completed immediately */

	FD_ZERO(&rset);
	FD_SET(sockfd, &rset);
	wset = rset;
	tval.tv_sec = nsec;
	tval.tv_usec = 0;

	if((n = select(sockfd+1, &rset, &wset, NULL, nsec ? &tval : NULL)) == 0)
	{
		close(sockfd);		/* timeout */
		errno = ETIMEDOUT;
		return(-1);
	}

	if(FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset))
	{
		len = sizeof(error);
		if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
		return(-1);			/* Solaris pending error */
	}
	else
	{
		fprintf(stderr, "select error: sockfd not set");
		return(-1);
	}

done:
	fcntl(sockfd, F_SETFL, flags);	/* restore file status flags */
	if (error)
	{
		close(sockfd);		/* just in case */
		errno = error;
		return(-1);
	}

	return(0);
}
#else
int connect_nonb(int sockfd, const struct sockaddr *saptr, int salen, int nsec)
{
	return connect(sockfd, saptr, salen);
}
#endif

int connect2server(unsigned int serverip, int fileid)
{
	int       conn_s;
	struct    sockaddr_in servaddr;
	short int port = SERV_PORT;
	int ret = 0;
	int sndsize = 1024*1024;
	pnewclientpacket pkt;
	pnewclientack pnewack;
	extern double TB_RATE_AVG_INTERVAL;

	if ( (conn_s = (int)socket(AF_INET, SOCK_STREAM, 0)) < 0 ) 
	{
		fprintf(stderr, "CLNT: Error creating listening socket.\n");
		return -1;
	}

	ret = setsockopt(conn_s, SOL_SOCKET, SO_SNDBUF, 
			(char *)&sndsize, sizeof(int));
	sndsize = 1024*1024;
	ret = setsockopt(conn_s, SOL_SOCKET, SO_RCVBUF, 
			(char *)&sndsize, sizeof(int));

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_port        = htons(port);
	servaddr.sin_addr.s_addr = serverip;

	if (connect_nonb(conn_s, (struct sockaddr *)&servaddr, sizeof(servaddr), 5) < 0 )
	{
		//MessageBox(NULL, "Cannot connect to server. Server may be busy; please try in a few minutes.\n", "Connect", MB_ICONERROR|MB_OK);
		return -1;
	}

	pkt.header.ptype = P_NEWCLIENT;
	pkt.header.length = 0;
	pkt.version = htonl(VERSION);
	pkt.fileid = 0;
	pkt.delta = TB_RATE_AVG_INTERVAL;
	writewrapper(conn_s, (char *)&pkt, sizeof(struct _newclientpkt));

	readwrapper(conn_s, (char *)&pnewack, sizeof(struct _newclientack));
	if(pnewack.header.ptype != P_NEWCLIENT_ACK)
	{
		printf("Error: bad packet type: %d\n", pnewack.header.ptype);
		closesocket(conn_s);
		return -1;
	}
	if(pnewack.compatibilityFlag == 0)
	{
		MessageBox(NULL, "Incompatible server. Please download the latest version of ShaperProbe client from:\nhttp://www.cc.gatech.edu/~partha/diffprobe/shaperprobe.html\n", "Authentication", MB_ICONERROR|MB_OK);
		return -1;
	}

	return conn_s;
}

int udpclient(unsigned int serverip, unsigned int targetport)
{
	int conn_s;
	struct    sockaddr_in servaddr;
	int sndsize = 1024*1024, ret = 0;

	if ((conn_s = (int)socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		fprintf(stderr, "Error creating socket.\n");
		return -1;
	}

	ret = setsockopt(conn_s, SOL_SOCKET, SO_SNDBUF, 
			(char *)&sndsize, sizeof(int));
	sndsize = 1024*1024;
	ret = setsockopt(conn_s, SOL_SOCKET, SO_RCVBUF, 
			(char *)&sndsize, sizeof(int));

	return conn_s;

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_port        = htons(targetport);
	servaddr.sin_addr.s_addr = serverip;

	if (connect(conn_s, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
	{
		printf("Error calling connect()\n");
		return -1;
	}

	return conn_s;
}

#ifdef _PAIRS_
double estimateCapacity_pairs(int tcpsock)
{
	pcapeststart pcap;
	pcapestack pcapack;
	int udpsock = 0;
	char buf[2000];
	int ret;
	struct timeval tv;
	double ct = 0;

	pcap.header.ptype = P_CAPEST_START;
	pcap.header.length = 0;
	writewrapper(tcpsock, (char *)&pcap, sizeof(struct _capeststart));
	ret = readwrapper(tcpsock, (char *)&pcapack, sizeof(struct _capestack));
	if(ret == -1 || pcapack.header.ptype != P_CAP_ACK)
	{
		fprintf(stderr, "cannot read OR wrong cap ack\n");
		return -1;
	}

	udpsock = udpclient();
	while(1)
	{
		gettimeofday(&tv, NULL);
		ct = tv.tv_sec + tv.tv_usec/1.0e6;
		memcpy(buf, (const char *)&ct, sizeof(ct));
		ret = send(udpsock, buf, 500, 0);
		if(ret == -1)
		{
			fprintf(stderr, "cannot send\n");
			return -1;
		}
		ret = send(udpsock, buf, 500, 0);
		if(ret == -1)
		{
			fprintf(stderr, "cannot send\n");
			return -1;
		}

		ret = readwrapper(tcpsock, (char *)&pcapack, 
				sizeof(struct _capestack));
		if(ret == -1 || pcapack.header.ptype != P_CAP_ACK)
		{
			fprintf(stderr, "cannot read OR wrong cap ack\n");
			return -1;
		}
		//printf("Capacity: %.2f\n", pcapack.capacity);
		printf("."); fflush(stdout);
		if(ntohl(pcapack.finalflag) == 1) break;
		usleep(30000);
	}

	close(udpsock);

	printf("Capacity: %d\n", ntohl(pcapack.capacity));
	return pcapack.capacity;
}
#else
double estimateCapacity(int tcpsock, int udpsock, struct sockaddr_in *from, void *edit, int (*estatus)(char *,void *))
{
	pcapeststart pcap;
	pcapestack pcapack;
	//int udpsock = 0;
	char buf[2000], str[256];
	int ret, count = 0, niters = 0;
	int trainlength = 5;
	struct sockaddr_in frm = *from;
	int fromlen = sizeof(struct sockaddr_in);
	int ULSZ = sizeof(unsigned long), UCSZ = sizeof(unsigned char);
	unsigned long sendtstamp = 0;
	unsigned char seq = 0;
	struct timeval ts;

	pcap.header.ptype = P_CAPEST_START;
	pcap.header.length = 0;
	writewrapper(tcpsock, (char *)&pcap, sizeof(struct _capeststart));
	ret = readwrapper(tcpsock, (char *)&pcapack, sizeof(struct _capestack));
	if(ret == -1 || pcapack.header.ptype != P_CAP_ACK)
	{
		fprintf(stderr, "cannot read OR wrong cap ack\n");
		return -1;
	}
	trainlength = ntohl(pcapack.trainlength);

	//udpsock = udpclient();
	while(1)
	{
		for(count = 0; count < trainlength; count++)
		{
			seq = count;
			gettimeofday(&ts, NULL);
			memcpy(buf, &seq, sizeof(unsigned char));
			sendtstamp = htonl(ts.tv_sec);
			memcpy((char *)buf+UCSZ, (char *)&sendtstamp, ULSZ);
			sendtstamp = htonl(ts.tv_usec);
			memcpy((char *)buf+UCSZ+ULSZ, (char *)&sendtstamp, ULSZ);

			ret = sendto(udpsock, buf, 1400, 0, 
					(struct sockaddr *)&frm, fromlen);
			if(ret == -1)
			{
				perror("cannot send\n");
				return -1;
			}
		}
		niters++;

		ret = readwrapper(tcpsock, (char *)&pcapack, 
				sizeof(struct _capestack));
		if(ret == -1 || pcapack.header.ptype != P_CAP_ACK)
		{
			fprintf(stderr, "cannot read OR wrong cap ack\n");
			return -1;
		}
		trainlength = ntohl(pcapack.trainlength);
		sprintf(str, "Upload packet train %d: %d Kbps", niters, ntohl(pcapack.capacity)); estatus(str, edit);
		if(ntohl(pcapack.finalflag) == 1) break;
		Sleep(500);
	}
	estatus("", edit);

	return ntohl(pcapack.capacity);
}
#endif

int sendCapEst(int tcpsock)
{
	pcapeststart pcap;
	pcapestack pcapack;
	int ret = 0;

	ret = readwrapper(tcpsock, (char *)&pcap, sizeof(struct _capeststart));
	if(ret == -1)
	{
		fprintf(stderr, "SERV: error reading from client: %d\n", tcpsock);
		closesocket(tcpsock);
		return -1;
	}
	if(pcap.header.ptype != P_CAPEST_START)
	{
		fprintf(stderr, "Bad capstart message!\n");
		closesocket(tcpsock);
		return -1;
	}

	pcapack.header.ptype = P_CAP_ACK;
	pcapack.header.length = 0;
	pcapack.capacity = pcapack.finalflag = 0;
	pcapack.trainlength = htonl(TRAIN_LENGTH);
	ret = writewrapper(tcpsock, (char *)&pcapack, 
			sizeof(struct _capestack));
	if(ret == -1)
	{
		fprintf(stderr, "SERV: error writing to client: %d\n", tcpsock);
		closesocket(tcpsock);
		return -1;
	}

	return 0;
}

