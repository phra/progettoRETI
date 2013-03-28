/*      chat-server.c
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
			else // continuo a leggere
			nleft -= nread;
	}
	return(n - nleft); // return >= 0
}

ssize_t writen (int fd, char *buf, size_t n){
	size_t nleft; ssize_t nwritten; char *ptr;
	ptr = buf; nleft = n;
	while (nleft > 0) {
		if ( (nwritten = send(fd, ptr, nleft, MSG_NOSIGNAL )) < 0) {
			if (errno == EINTR) nwritten = 0;
			else return(-1);
		}
		nleft -= nwritten; ptr += nwritten;
	}
	return(n);
}
*/

void error(char* str){
	perror(str);
	fflush(stderr);
	exit(EXIT_FAILURE);
}

void stampabuf(char* buf){
	printf("il buffer all'indirizzo %lu contiene:\n%s\nfine buff.\n",(long int)buf,buf);
}

void set_timeout(struct timeval* timeout,int sec, int usec){
	pulisci(timeout,sizeof(struct timeval));
	timeout->tv_sec = sec;
	timeout->tv_usec = usec;
}

typedef struct client_t{
	char* nick;
	char* ip;
	int fd;
}client;

client* crea_client(char* nick, struct in_addr addr, int fd){
	client* p = malloc(sizeof(client));
	pulisci(p,sizeof(p));
	asprintf(&p->nick,"%s",nick);
	asprintf(&p->ip,"%s",inet_ntoa(addr));
	p->fd = fd;
	return p;
}

void stampaclient(client** clients, int maxclients){
	int i = 0;
	for(;i<=maxclients;i++){
		if (clients[i] != NULL)	printf("il client all'indirizzo %lu contiene: fd = %d, ip = %s, nick = %s.\n",(long int)clients[i],clients[i]->fd,clients[i]->ip,clients[i]->nick);
	}
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
	int len1 = strlen(&bin[1]) +1;
	int len2 = strlen(&bin[1+len1]) +1;
	#ifdef DEBUG
	printf("deserializemsg!\n");
	#endif
	*message = crea_messaggio(*bin, &bin[1], &bin[1+len1]);
	return 1+len1+len2;
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

msg* recv_msg(int fd){
	int size;
	char* p;
	ssize_t n,rd;
	msg* message;
	#ifdef DEBUG
	printf("recv_msg!\n");
	#endif
	pulisci(&rd,sizeof(rd));
	pulisci(&n,sizeof(n));
	if ((rd = read(fd,&size,4)) < 0) error("errore nella recv dell'header.\n");
	else if (rd == 0) return NULL;
	size = ntohl(size) - 4;
	p = malloc(size*sizeof(char));
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

void sendmsgtoall(client** clients, int maxclient, msg* message){
	register int i = 0;
	msg* m = message;
	client** c = clients;
	#ifdef DEBUG
	printf("sendmsgtoall!\n");
	#endif
	for (;i <= maxclient;i++){
		if (c[i] != NULL) send_msg(c[i]->fd,m);
	}
}

void sendmsgtoother(client** clients, int maxclient, int fdmittente, msg* message){
	register int i = 0;
	msg* m = message;
	client** c = clients;
	#ifdef DEBUG
	printf("sendmsgtoother!\n");
	#endif
	for (;i <= maxclient;i++){
		if ((c[i] != NULL) && (i != fdmittente)) send_msg(c[i]->fd,m);
	}
}

ssize_t sendstr(int fd, char* str){
	ssize_t n,wr;
	pulisci(&wr,sizeof(wr));
	pulisci(&n,sizeof(n));
	do {
		if ((wr = send(fd,&str[n],strlen(&str[n])+1,MSG_NOSIGNAL)) < 0)
			if (errno != EINTR) error("errore nella write.\n");
			else wr = 0;
		n += wr;
	} while (str[n-1] != '\0');
	return n;
}

ssize_t recvstr(int fd, char* str){
	ssize_t n,rd;
	pulisci(&rd,sizeof(rd));
	pulisci(&n,sizeof(n));
	do {
		if ((rd = read(fd,&str[n],BUFFSIZE-n)) < 0)
			if (errno != EINTR) error("errore nella recv.\n");
			else rd = 0;
		else if (rd == 0) return 0;
		n += rd;
	} while (str[n-1] != '\0');
	return n;
}

void sendstrtoo(client** clients, int maxclient, int j, char* str){
	register int i = 0;
	char* p = str;
	client** c = clients;
	printf("%s\n",p);
	for (;i <= maxclient;i++){
		if (c[i] != NULL && j != i) sendstr(c[i]->fd,p);
	}
}

void sendstrtoa(client** clients, int maxclient, char* str){
	register int i = 0;
	char* p = str;
	client** c = clients;
	for (;i <= maxclient;i++){
		if (c[i] != NULL) sendstr(c[i]->fd,p);
	}
}

void aggiungi_client(int fd, client** clients,fd_set* fdset,int* maxfd,int* maxclient, char* nick, struct in_addr addr){
	register int i;
	client** c = clients;
	char* n = nick;
	struct in_addr a = addr;
	for (i=0;i < FD_SETSIZE;i++) {
		if (clients[i] == NULL){
			msg* m;
			char* p;
			clients[i] = crea_client(n,a,fd);
			printf("[[ aggiunto %s con fd=%d in posizione %d. (%s)]]\n",n,fd,i,clients[i]->ip);
			if (i==FD_SETSIZE) {close(fd); sendstrtoa(c,*maxclient,"troppi client."); return;}
			*maxclient = MAX(*maxclient,i);
			*maxfd = MAX(*maxfd,fd);
			#ifdef DEBUG
			printf("maxclient = %d, maxfd = %d\n",*maxclient,*maxfd);
			#endif
			FD_SET(fd,fdset);
			asprintf(&p,"[[ %s si e' collegato. ]]\n",n);
			m = crea_messaggio(0,p,"SERVER");
			sendmsgtoall(c,*maxclient,m);
			free(p);
			freemsg(m);
			break;
		}
	}
	stampaclient(c,*maxclient);
}

void freeclient(client* c){
	free(c->nick);
	free(c->ip);
	free(c);
}

void rimuovi_client(int i,client** clients,fd_set* allset,int* maxfd, int* maxclient){
	close(clients[i]->fd);
	FD_CLR(clients[i]->fd,allset);
	if (clients[i]->fd == *maxfd) (*maxfd)--;
	if (i == *maxclient) (*maxclient)--;
	printf("[[ rimosso clients con fd=%d in posizione %d. ]]\n",clients[i]->fd,i);
	free(clients[i]);
	clients[i] = NULL;
}

int main(int argc, char **argv)
{
	struct sockaddr_in addrserv,addrcli;
	int fdcli,fdlisten,i,ready,maxfd,maxclient;
	client* clients[FD_SETSIZE];
	short int port;
	char buf[BUFFSIZE];
	fd_set allset;
	fdcli = fdlisten = i = ready = maxfd = maxclient = 0;
	if (argc != 2) error("parametri sbagliati.\nusage: ./server portTObind#");
	printf("configurazione, ");
	port = atoi(argv[1]);
	pulisci((void*)&addrserv,sizeof(addrserv));
	pulisci((void*)&addrcli,sizeof(addrcli));
	addrserv.sin_family = AF_INET;
	addrserv.sin_addr.s_addr = htonl(INADDR_ANY);
	addrserv.sin_port = htons(port);
	printf("creo il socket, ");
	if ((fdlisten = socket(AF_INET,SOCK_STREAM,0)) < 0) error("errore socket #1.");
	printf("faccio la bind, ");
	if (bind(fdlisten,(struct sockaddr*) &addrserv, sizeof(addrserv)) < 0) error("errore nella bind.");
	printf("faccio la listen ");
	if (listen(fdlisten,MAX_CONN) < 0) error("errore nella listen.");
	printf("e rimango in attesa di una connessione sulla porta %d.\n",port);
	for (i=0;i < FD_SETSIZE;i++) clients[i] = NULL;
	FD_ZERO(&allset);
	FD_SET(STDIN_FILENO,&allset);
	FD_SET(fdlisten,&allset);
	maxfd = fdlisten;
	for (;;){
		fd_set fdset = allset;
		struct timeval timeout;
		set_timeout(&timeout,10,0);
		if (!(ready = select(maxfd+1,&fdset,NULL,NULL,&timeout))){
			printf("[[ nessuna attivita' nei 10 secondi precedenti. ]]\n");
			continue;
		}
		printf("ci sono %d fd pronti.\n",ready);
		stampaclient(clients,maxclient);
		if (FD_ISSET(fdlisten,&fdset)){
			pulisci(buf,BUFFSIZE);
			socklen_t len = sizeof(addrcli);
			fdcli = accept(fdlisten,(struct sockaddr*)&addrcli,&len);
			printf("[[ client connesso. ]]\n");
			recvstr(fdcli,buf);
			aggiungi_client(fdcli,clients,&allset,&maxfd,&maxclient,buf,addrcli.sin_addr);
			ready--;
		}
		if(FD_ISSET(STDIN_FILENO,&fdset)) {
			msg* message;
			printf("dati da stdin.\n");
			pulisci(buf,BUFFSIZE);
			scanf("%[^\n]s",buf);
			printf("%c",getchar());
			message = crea_messaggio(0,buf,"SERVER");
			sendmsgtoall(clients,maxclient,message);
			ready--;
			freemsg(message);
		}
		for(i=0;(i <= maxclient) && (ready > 0);i++){
			if (clients[i] != NULL){
				int fd = clients[i]->fd;
				if (FD_ISSET(fd,&fdset)){
					msg* message;
					#ifdef DEBUG
					printf("dati da fd=%d.\n",fd);
					#endif
					if ((message = recv_msg(fd)) == NULL) rimuovi_client(i,clients,&allset,&maxfd,&maxclient);
					else if (message->type == 0) {
						printf("%s ha scritto:\n%s\n",message->mittente,message->message);
						sendmsgtoother(clients,maxclient,fd,message);
						freemsg(message);
					}
					ready--;
				}
			}
		}
	}
	return 0;
}