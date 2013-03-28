/*      proxy.c
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
#define MAX_CONN 1
#define RECVBUF 65535
#define BODY (char)0
#define ACK (char)1
#define RAND_MAX 49


#define pulisci(p,size) memset(p,0,size)
#define acapo() printf("\n")
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define alloc(a) malloc(sizeof(a))

struct sockaddr_in way1,way2,way3;

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

int refreshtimeout(struct timeval* timeout, struct timeval timepass){
	timeout->tv_sec -= timepass.tv_sec;
	timeout->tv_usec -= timepass.tv_usec;
	if (((timeout->tv_sec * (10^6)) - timeout->tv_usec) <= 0) return 1;
	else return 0;
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

void aggiungi_client(int fd, client** clients,fd_set* fdset,int* maxfd,int* maxclient, char* nick, struct in_addr addr){
	register int i;
	char* n = nick;
	struct in_addr a = addr;
	for (i=0;i < FD_SETSIZE;i++) {
		if (clients[i] == NULL){
			clients[i] = crea_client(n,a,fd);
			printf("[[ aggiunto %s con fd=%d in posizione %d. (%s)]]\n",n,fd,i,clients[i]->ip);
			if (i==FD_SETSIZE) {close(fd); return;}
			*maxclient = MAX(*maxclient,i);
			*maxfd = MAX(*maxfd,fd);
			#ifdef DEBUG
			printf("maxclient = %d, maxfd = %d\n",*maxclient,*maxfd);
			#endif
			FD_SET(fd,fdset);
			break;
		}
	}
	#ifdef DEBUG
	stampaclient(c,*maxclient);
	#endif
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

typedef struct pacco_t {
	int id;
	int sizepayload;
	char type;
	char* payload;
}pacco;

pacco* crea_pacco(int id, int size, char* body, char type){
	char* b = malloc(size*sizeof(char));
	pacco* p = malloc(sizeof(pacco));
	pulisci(p,sizeof(pacco));
	p->id = id;
	p->sizepayload = size;
	memcpy(b,body,size);
	p->payload = b;
	p->type = type;
	return p;
}

void freepacco(pacco* p){
	free(&p->payload);
	free(p);
}

typedef struct listapacchi_t {
	pacco* this;
	struct listapacchi_t* next;
	struct timeval timeout;
}listapacchi;

void aggiungi_pacco(listapacchi* root,pacco* pkt, struct timeval timeout){
	listapacchi* p = root;
	listapacchi* ultimo;
	struct timeval* time = malloc(sizeof(struct timeval));
	*time = timeout;
	while(p->next != NULL) p = p->next;
	ultimo = malloc(sizeof(listapacchi));
	pulisci(ultimo,sizeof(ultimo));
	ultimo->this = pkt;
	ultimo->next = NULL;
	ultimo->timeout = *time;
	p->next = ultimo;
	#ifdef DEBUG
	printf("aggiunto pacco alla lista con id=%d.\n",pkt->id);
	#endif
}

void rimuovi_pacco(listapacchi* root,int id){
	listapacchi* p = root;
	 while ((p->next != NULL) && (p->next->this->id != id)) p = p->next;
	 if ((p->next != NULL) && (p->next->this->id == id)){
		listapacchi* new = p->next->next;
		freepacco(p->next->this);
		free(p->next);
		p->next = new;
	}
	else {
		#ifdef DEBUG
		printf("doppio ACK con id=%d.\n",id);
		#endif
	}
}

listapacchi* inlista(listapacchi* root, int id){
	listapacchi* p = root;
	if (p->next == NULL) return NULL;
	do {
		p = p->next;
		if (p->this->id == id) return p;
	} while (p->next != NULL);
	return NULL;
}

int serializepacco(pacco* pkt, char** bin){
	char B = 'B';
	char A = 'A';
	int id = htonl(pkt->id);
	int sizepayload = htonl(pkt->sizepayload);
	char type = pkt->type;
	char* payload = pkt->payload;
	char* serial = malloc((10+sizepayload)*sizeof(char));
	char* p = serial;
	int temp = htonl((*(int*)payload));
	memcpy(p,&id,4);
	p += 4;
	memcpy(p++,&B,1);
	switch(type){
		case 0:
			memcpy(p++,&B,1);
			memcpy(p,&sizepayload,4);
			p += 4;
			memcpy(p,payload,sizepayload);
			break;
		case 1:
			memcpy(p++,&A,1);
			memcpy(p,&sizepayload,4);
			p += 4;
			memcpy(p,&temp,sizepayload);
			break;
		default:
			error("pacchetto fuori dal protocollo in uscita.\n");
	}
	*bin = serial;
	return p - serial;
}

void deserializepacco(char* bin, pacco** pkt){
	int id = ntohl(*(int*)bin);
	char type1 = bin[4];
	char type2 = bin[5];
	int sizepayload = ntohl(*((int*)&bin[6]));
	char* payload = &bin[10];
	int temp = htonl((*(int*)payload));
	if (type1 == 'B') {
		if (type2 == 'B'){
			#ifdef DEBUG
			printf("deserializzo un pacchetto body.\n");
			#endif
			*pkt = crea_pacco(id,sizepayload,payload,BODY);
		} else if (type2 == 'A') {
			#ifdef DEBUG
			printf("ricevuto un ACK con id=%d.\n",temp);
			#endif
			*pkt = crea_pacco(id,sizepayload,(char*)&temp,ACK);
		} else error("pacchetto fuori dal protocollo in entrata.\n");
	} else if (type1 == 'I') {
		#ifdef DEBUG
		printf("ricevuto un pacchetto ICMP con id=%d.\n",temp);
		#endif
		*pkt = NULL;
	} else error("pacchetto fuori dal protocollo in entrata.\n");
}

pacco* recvtcp(int fd, int* id){
	char buf[RECVBUF];
	ssize_t rd;
	pacco* p;
	pulisci(&rd,sizeof(rd));
	do {
		if (((rd = read(fd,buf,RECVBUF)) < 0) && (errno != EINTR)) error("errore nella recv del pacchetto tcp.\n");
	} while (errno == EINTR);
	if (rd > 0){
		p = crea_pacco(*id++,rd,buf,0);
		return p;
	}
	else return NULL;
}

void _sendtcp(int fd, pacco* p, int* id){
	ssize_t wr,n;
	int size = p->sizepayload;
	pulisci(&wr,sizeof(wr));
	pulisci(&n,sizeof(n));
	if (p->id != *id) error("stiamo inviando senza rispettare l'ordine corretto.\n");
	do {
		if ((wr = send(fd,&p->payload[n],size-n,MSG_NOSIGNAL)) < 0)
			if (errno == EINTR) continue;
			else error("errore nella send del pacchetto tcp.\n");
		n += wr;
	} while (n < size);
	*id += 1;
}

void sendtcp(int fd, pacco* p, int* id, listapacchi* lista){
	listapacchi* elem;
	_sendtcp(fd,p,id);
	rimuovi_pacco(lista,p->id);
	while (elem = inlista(lista,*id)) {
		_sendtcp(fd,elem->this,id);
		rimuovi_pacco(lista,elem->this->id);
	}
}

void _sendudp(int fd, pacco* p, struct sockaddr_in* address) {
	ssize_t wr,n;
	char* bin;
	pacco* pkt = p;
	struct sockaddr_in* addr = address;
	int size = serializepacco(pkt,&bin);
	pulisci(&wr,sizeof(wr));
	pulisci(&n,sizeof(n));
	do {
		if ((wr = sendto(fd,&bin[n],size-n,MSG_NOSIGNAL,(struct sockaddr*)addr,sizeof(struct sockaddr_in))) < 0)
			if (errno == EINTR) continue;
			else error("errore nella send del pacchetto udp.\n");
		n += wr;
	} while (n < size);
	free(bin);
}

void sendudp(int fd, pacco* p, int how) {
	pacco* pkt = p;
	switch (how){
		case 1:
			_sendudp(fd,pkt,&way1);
			break;
		case 2:
			_sendudp(fd,pkt,&way2);
			break;
		case 3:
			_sendudp(fd,pkt,&way3);
			break;
		case 4:
			_sendudp(fd,pkt,&way1);
			_sendudp(fd,pkt,&way2);
			break;
		case 5:
			_sendudp(fd,pkt,&way1);
			_sendudp(fd,pkt,&way3);
			break;
		case 6:
			_sendudp(fd,pkt,&way2);
			_sendudp(fd,pkt,&way3);
			break;
		case 7:
			_sendudp(fd,pkt,&way1);
			_sendudp(fd,pkt,&way2);
			_sendudp(fd,pkt,&way3);
			break;
		default:
			error("sendudp con parametro how sconosciuto.\n");
	}
}

pacco* recvudp(int fd){
	char buf[RECVBUF];
	ssize_t rd;
	pacco* p;
	pulisci(&rd,sizeof(rd));
	pulisci(buf,RECVBUF);
	do {
		if (((rd = read(fd,buf,RECVBUF)) < 0) && (errno != EINTR)) error("errore nella recv del pacchetto tcp.\n");
	} while (errno == EINTR);
	deserializepacco(buf,&p);
	return p;
}

void sendack(int fd, int id){
	int idn = htonl(id);
	pacco* ack = crea_pacco(0,4,(char*)&idn,1);
	sendudp(fd,ack,7);
	freepacco(ack);
}

int main(int argc, char **argv)
{
	struct sockaddr_in addrserv,addrcli;
	int fdcli,fdlisten,fdudp,i,ready,maxfd,maxclient,IDtoCLIENT,IDtoNET,have2connect;
	client* clients[FD_SETSIZE];
	short int port,port2,port3;
	char buf[BUFFSIZE];
	fd_set allset;
	listapacchi rootTOnet,rootTOclient;
	pulisci(&rootTOnet,sizeof(listapacchi));
	pulisci(&rootTOclient,sizeof(listapacchi));
	pulisci(&way1,sizeof(struct sockaddr_in));
	pulisci(&way2,sizeof(struct sockaddr_in));
	pulisci(&way3,sizeof(struct sockaddr_in));
	IDtoCLIENT = IDtoNET = 0;
	rootTOnet.this = NULL;
	rootTOnet.next = NULL;
	fdcli = fdlisten = i = ready = maxfd = maxclient = 0;
	if (argc != 6) error("parametri sbagliati.\nusage: ./server portTObindTCP portTObindUDP ipTOsendUDP FIRSTportTOsendUDP ACTLIKE");
	printf("configurazione, ");
	port = atoi(argv[1]);
	port2 = atoi(argv[2]);
	port3 = atoi(argv[4]);
	have2connect = atoi(argv[5]);
	way1.sin_family = AF_INET;
	way1.sin_addr.s_addr = htonl(atoi(argv[3]));
	way2 = way3 = way1;
	way1.sin_port = htons(port3);
	way2.sin_port = htons(port3+1);
	way3.sin_port = htons(port3+2);
	pulisci((void*)&addrserv,sizeof(addrserv));
	pulisci((void*)&addrcli,sizeof(addrcli));
	/* connessione tcp */
	if (!have2connect){
		addrserv.sin_family = AF_INET;
		addrserv.sin_addr.s_addr = htonl(INADDR_ANY);
		addrserv.sin_port = htons(port);
		printf("creo il socket, ");
		if ((fdlisten = socket(AF_INET,SOCK_STREAM,0)) < 0) error("errore socket tcp.");
		printf("faccio la bind, ");
		if (bind(fdlisten,(struct sockaddr*) &addrserv, sizeof(addrserv)) < 0) error("errore nella bind.");
		printf("faccio la listen ");
		if (listen(fdlisten,MAX_CONN) < 0) error("errore nella listen.");
		printf("e rimango in attesa di una connessione sulla porta %d.\n",port);
	} else {
		addrserv.sin_family = AF_INET;
		addrserv.sin_addr.s_addr = inet_addr("127.0.0.1");
		addrserv.sin_port = htons(9000);
		if ((fdcli = socket(AF_INET,SOCK_STREAM,0)) < 0) error("errore socket tcp.");
		if (connect(fdcli,(struct sockaddr*)&addrserv,sizeof(addrserv)) < 0) error("errore nella connect.");
	}
	/* connessione udp */
	pulisci((void*)&addrserv,sizeof(addrserv));
	addrserv.sin_family = AF_INET;
	addrserv.sin_addr.s_addr = htonl(INADDR_ANY);
	addrserv.sin_port = htons(port2);
	if ((fdudp = socket(AF_INET,SOCK_DGRAM,0)) < 0) error("errore socket udp.");
	if (bind(fdudp,(struct sockaddr*) &addrserv, sizeof(addrserv)) < 0) error("errore nella bind.");
	/*configurazione select*/
	for (i=0;i < FD_SETSIZE;i++) clients[i] = NULL;
	FD_ZERO(&allset);
	FD_SET(STDIN_FILENO,&allset);
	if (!have2connect){
		FD_SET(fdlisten,&allset);
	} else {
		FD_SET(fdcli,&allset);
	}
	FD_SET(fdudp,&allset);
	maxfd = fdlisten;
	for (;;){
		fd_set fdset = allset;
		struct timeval timeout,timeofdaypre,timeofdaypost,timepass;
		listapacchi* p = &rootTOnet;
		set_timeout(&timeout,0,500000);
		gettimeofday(&timeofdaypre,NULL);
		if (!(ready = select(maxfd+1,&fdset,NULL,NULL,&timeout))){
			#ifdef DEBUG
			printf("[[ nessuna attivita' nei 0,5 secondi precedenti. ]]\n");
			#endif
			continue;
		}
		gettimeofday(&timeofdaypost,NULL);
		timepass.tv_sec = timeofdaypost.tv_sec - timeofdaypre.tv_sec;
		timepass.tv_usec = timeofdaypost.tv_usec - timeofdaypre.tv_usec;
		#ifdef DEBUG
		printf("ci sono %d fd pronti.\n",ready);
		stampaclient(clients,maxclient);
		#endif
		if (!have2connect){
			if (FD_ISSET(fdlisten,&fdset)){
				pulisci(buf,BUFFSIZE);
				socklen_t len = sizeof(addrcli);
				fdcli = accept(fdlisten,(struct sockaddr*)&addrcli,&len);
				printf("[[ client connesso. ]]\n");
				strncpy(buf,"CLIENT",strlen("CLIENT"));
				aggiungi_client(fdcli,clients,&allset,&maxfd,&maxclient,buf,addrcli.sin_addr);
				ready--;
			}
		}
		/*#ifdef DEBUG
		if (FD_ISSET(STDIN_FILENO,&fdset)) {
			msg* message;
			#ifdef DEBUG
			printf("dati da stdin.\n");
			#endif
			pulisci(buf,BUFFSIZE);
			scanf("%[^\n]s",buf);
			printf("%c",getchar());
			message = crea_messaggio(0,buf,"SERVER");
			sendmsgtoall(clients,maxclient,message);
			ready--;
			freemsg(message);
		}
		#endif*/
		if (FD_ISSET(fdcli,&fdset)) {
			pacco* pkt;
			if ((pkt = recvtcp(fdcli,&IDtoNET)) == NULL) rimuovi_client(0,clients,&allset,&maxfd,&maxclient);
			struct timeval timeout;
			set_timeout(&timeout,0,0);
			aggiungi_pacco(&rootTOnet,pkt,timeout);
		}
		if (FD_ISSET(fdudp,&fdset)) {
			pacco* pkt = recvudp(fdudp);
			if (pkt != NULL){
				int temp = ntohl(*(int*)pkt->payload);
				struct timeval timeout;
				set_timeout(&timeout,0,0);
				switch (pkt->type){
					case BODY:
						#ifdef DEBUG
						printf("[[ arrivato un pacchetto di tipo BODY. ]]\n");
						#endif
						/*arrivato un pacchetto body*/
						if (pkt->id >= IDtoCLIENT){
							if (pkt->id == IDtoCLIENT){
								#ifdef DEBUG
								printf("[[ mando al client pkt con id=%d ]]\n",pkt->id);
								#endif
								sendtcp(fdcli,pkt,&IDtoCLIENT,&rootTOclient);
								sendack(fdudp,pkt->id);
								freepacco(pkt);
							}
							else if (!inlista(&rootTOclient,pkt->id)){
								#ifdef DEBUG
								printf("[[ mancano dei pkt prima di inviare questo (id=%d). accodo. ]]\n",pkt->id);
								#endif
								aggiungi_pacco(&rootTOclient,pkt,timeout);
								sendack(fdudp,pkt->id);
							} else {
								/*pacco doppio*/
								#ifdef DEBUG
								printf("[[ ricevuto doppio pkt (id=%d). mando ack. ]]\n",pkt->id);
								#endif
								sendack(fdudp,pkt->id);
								freepacco(pkt);
							}
						break;
					case ACK:
						/*arrivato un ack*/
						rimuovi_pacco(p,temp);
						freepacco(pkt);
						break;
					}
				}
			}
		}
		while (p->next != NULL) {
			p = p->next;
			if (refreshtimeout(&p->timeout,timepass)) {
				int how = rand() % 3;
				sendudp(fdudp,p->this,how);
				set_timeout(&p->timeout,0,450000);
			}
		}
	}
	return 0;
}