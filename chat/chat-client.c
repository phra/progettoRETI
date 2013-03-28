/*      chat-client.c
//
//      Copyright 2012 phra <phra[at]phra-VirtualBox>
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
//      MA 02110-1301, USA.
//
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/param.h>

#define BUFFSIZE 100
#define MAX_CONN 10

#define pulisci(p,size) memset(p,0,size)
#define acapo() printf("\n")
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define alloc(a) malloc(sizeof(a))

void error(char* str){
	perror(str);
	fflush(stderr);
	exit(EXIT_FAILURE);
}

void stampabuf(char* buf){
	printf("il buffer all'indirizzo %lu contiene:\n",(long int)buf);
	printf("%s\n",buf);
	printf("fine buff.\n");
}

void set_timeout(struct timeval* timeout,int sec, int usec){
	pulisci(timeout,sizeof(struct timeval));
	timeout->tv_sec = sec;
	timeout->tv_usec = usec;
}

/* ghini dixit..
ssize_t readn (int fd, char *buf, size_t n){
	size_t nleft; ssize_t nread;
	nleft = n;
	while (nleft > 0) {
		if ( (nread = read(fd, buf+n-nleft, nleft)) < 0)
			if (errno != EINTR)	return(-1);
			else if (nread == 0) {
				break;
			}
			else
			nleft -= nread;
	}
	return(n - nleft);
}

ssize_t writen (int fd, char *buf, size_t n){
	size_t nleft; ssize_t nwritten; char *ptr;
	ptr = buf; nleft = n;
	while (nleft > 0) {
		if ( (nwritten = send(fd, ptr, nleft, MSG_NOSIGNAL | MSG_DONTWAIT)) < 0) {
			if (errno == EINTR) nwritten = 0;
			else return(-1);
		}
		nleft -= nwritten; ptr += nwritten;
	}
	return(n);
}
*/

ssize_t sendstr(int fd, char* str){
	ssize_t n,wr;
	pulisci(&wr,sizeof(wr));
	pulisci(&n,sizeof(n));
	#ifdef DEBUG
	printf("sendstr!\n");
	#endif
	do {
		if ((wr = send(fd,&str[n],strlen(&str[n])+1,MSG_NOSIGNAL)) < 0)
			if (errno != EINTR) error("errore nella write.");
			else wr = 0;
		n += wr;
	} while (str[n-1] != '\0');
	return n;
}

ssize_t recvstr(int fd, char* str){
	ssize_t n,rd;
	pulisci(&rd,sizeof(rd));
	pulisci(&n,sizeof(n));
	#ifdef DEBUG
	printf("recvstr!\n");
	#endif
	do {
		if ((rd = recv(fd,&str[n],BUFFSIZE-n,0)) < 0)
			if (errno != EINTR) error("errore nella recv.\n");
			else rd = 0;
		else if (rd == 0) return 0;
		n += rd;
	} while (str[n-1] != '\0');
	return n;
}

typedef struct msg_t {
	char type;
	char* message;
	char* mittente;
}msg;

msg* crea_messaggio(char type, char* body, char* mittente){
    char* b = body;
    char* mit = mittente;
    #ifdef DEBUG
	printf("crea_messaggio!\n");
	#endif
	msg* message = alloc(msg);
	pulisci(message,sizeof(msg));
	message->type = type;
	asprintf(&message->message,"%s",b);
	asprintf(&message->mittente,"%s",mit);
	return message;
}

void freemsg(msg* message){
	#ifdef DEBUG
	printf("freemsg!\n");
	#endif
	free(message->message);
	free(message->mittente);
	free(message);
}

int serializemsg(msg* message,char** bin){
	char* body = message->message;
	char* sender = message->mittente;
	int sizebody = (strlen(body)+1);
	int sizesender = (strlen(sender)+1);
	int sizetot = sizebody + sizesender + 5;
	char* p = malloc(sizetot*sizeof(char));
	int* size = (int*)p;
	#ifdef DEBUG
	printf("serializemsg!\n");
	#endif
	pulisci(p,sizetot);
	*size = htonl(sizetot);
	p[4] = message->type;
	strncpy(&p[5],body,sizebody);
	strncpy(&p[5+sizebody],sender,sizesender);
	*bin = p;
	return sizetot;
}

int deserializemsg(char* bin,msg** message){
	char* b = bin;
	int len1 = strlen(&bin[1]) +1;
	int len2 = strlen(&bin[1+len1]) +1;
	#ifdef DEBUG
	printf("deserializemsg! chiamo crea_messaggio(%d,%s,%s);\n",*b, &b[1], &b[1+len1]);
	#endif
	*message = crea_messaggio(*b, &b[1], &b[1+len1]);
	return 1+len1+len2;
}

msg* recv_msg(int fd){
	int size;
	char* p;
	ssize_t n,rd;
	msg* message;
	#ifdef DEBUG
	printf("recv_msg su fd=%d!\n",fd);
	#endif
	pulisci(&rd,sizeof(rd));
	pulisci(&n,sizeof(n));
	if ((rd = read(fd,&size,4)) < 0) error("errore nella recv dell'header.\n");
	if (rd == 0) return NULL;
	size = ntohl(size) - 4;
	p = malloc(size*sizeof(char));
	#ifdef DEBUG
	printf("alloco size=%d char.\n",size);
	#endif
	do {
		if ((rd = read(fd,&p[n],size-n)) < 0)
			if (errno == EINTR) continue;
			else error("errore nella recv.\n");
		if (rd == 0) return NULL;
		n += rd;
	} while (n < size);
	deserializemsg(p,&message);
	free(p);
	return message;
}

void send_msg(int fd, msg* message){
	msg* m = message;
	char* bin;
	int size = serializemsg(m,&bin);
	ssize_t n,wr;
	#ifdef DEBUG
	printf("send_msg!\n");
	#endif
	pulisci(&wr,sizeof(wr));
	pulisci(&n,sizeof(n));
	do {
		if ((wr = send(fd,&bin[n],size-n,MSG_NOSIGNAL)) < 0)
			if (errno == EINTR) continue;
			else error("errore nella write.\n");
		n += wr;
	} while (n < size);
	free(bin);
}

int main(int argc, char **argv)
{
	struct sockaddr_in addrserv;
	int fdconnect,i,maxfd,ready;
	fdconnect = i = maxfd = ready = 0;
	short int port;
	char buf[BUFFSIZE];
	char* nick;
	socklen_t len;
	fd_set allset;
	if (argc != 3) error("parametri sbagliati.\nusage: ./client portTOconnect nickname");
	printf("configurazione, ");
	port = atoi(argv[1]);
	nick = argv[2];
	pulisci((void*)&addrserv,sizeof(addrserv));
	addrserv.sin_family = AF_INET;
	addrserv.sin_addr.s_addr = inet_addr("127.0.0.1");
	addrserv.sin_port = htons(port);
	printf("creo il socket, connessione ");
	if ((fdconnect = socket(AF_INET,SOCK_STREAM,0)) < 0) error("errore socket #1.");
	if (connect(fdconnect,(struct sockaddr*)&addrserv,sizeof(addrserv)) < 0) error("errore nella connect.");
	printf("effettuata.\n");
	sendstr(fdconnect,nick);
	maxfd = fdconnect;
	FD_ZERO(&allset);
	FD_SET(fdconnect,&allset);
	FD_SET(STDIN_FILENO,&allset);
	for (;;){
		fd_set fdset = allset;
		struct timeval timeout;
		set_timeout(&timeout,10,0);
		if (!(ready = select(maxfd+1,&fdset,NULL,NULL,&timeout))){
			#ifdef DEBUG
			printf("nessuna attivita' nei 10 secondi precedenti.\nmaxfd = %d\n",maxfd);
			#endif
			continue;
		}
		if (FD_ISSET(fdconnect,&fdset)){
			msg* message;
			#ifdef DEBUG
			printf("dati dal server.\n");
			#endif
			pulisci(buf,BUFFSIZE);
			if ((message = recv_msg(fdconnect)) == NULL) error("la connessione e' stata chiusa.\n");
			printf("\n%s ha scritto:\n%s\n",message->mittente,message->message);
			freemsg(message);
		}
		if(FD_ISSET(STDIN_FILENO,&fdset)) {
			msg* message;
			char* bin;
			pulisci(buf,BUFFSIZE);
			scanf("%[^\n]s",buf);
			printf("%c",getchar());
			message = crea_messaggio(0,buf,nick);
			send_msg(fdconnect,message);
			ready--;
			freemsg(message);
		}
	}
	return 0;
}