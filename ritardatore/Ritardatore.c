/* Ritardatore.c  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <math.h>

#include "Util.h"

/* #define VICDEBUG */

#define P(X) do { fprintf(stderr,X "\n"); fflush(stderr); } while(0)

#define MAXNUMCONNECTIONS 3
#define ADDRESSSTRINGLEN 32

#define BIANCO "\033[0m"
#define ROSSO  "\033[22;31m"
#define VERDE  "\033[22;32m"


typedef struct {
	int32_t fd_latosender;
	uint16_t port_number_latosender;
	int32_t fd_latoreceiver;
	uint16_t port_number_latoreceiver;
	int attivo;
	long int sec_istcreazione;
	int stato_trasmissione; /* 0 perdere, 1 spedire */
	struct timeval ist_prossimo_cambio_stato;
} COPPIAFD;
COPPIAFD coppiafd[MAXNUMCONNECTIONS];

#define CMD_ADDPORT	1
#define CMD_SEND	2

struct structelementolista;
typedef struct structelementolista{
	int cmd;
	int32_t fd;
	uint16_t port_number_local;
	uint16_t port_number_dest;
	char IP_dest[ADDRESSSTRINGLEN];
	uint32_t len;
	char *buf;
	struct timeval timeout;
	struct structelementolista *next;
} ELEMENTOLISTA;
ELEMENTOLISTA *root;

typedef struct {
	uint32_t id_network_order;
	char tipo;
}  __attribute__((packed)) COMMON_HEADER;


typedef struct {
	uint32_t id_network_order;
	char tipo;
	uint32_t packet_lost_id_network_order;
}  __attribute__((packed)) ICMP;


#define PERC_ERR 15   /* 15% */

double PERCENTUALE_ERRORE;
int counter_localport_sender_side=0, counter_localport_receiver_side=0;
uint16_t	first_local_port_number_sender_side, first_local_port_number_receiver_side;
uint32_t local_counter=0;

fd_set rdset, all;
int maxfd;
long int numspediti=0;
long int numscartati=0;
long int numscartatiSENZAICMP=0;
int printed=0;
unsigned long int bytespediti=0, bytescartati=0;

void close_coppia(int i);

void sig_print(int signo)
{
	int i;
	if(printed==0)
	{
		printed=1;
		for(i=0;i<MAXNUMCONNECTIONS;i++)
			close_coppia(i);

		if(signo==SIGINT)		printf("SIGINT\n");
		else if(signo==SIGHUP)	printf("SIGHUP\n");
		else if(signo==SIGTERM)	printf("SIGTERM\n");
		else					printf("other signo\n");
		printf("\n");
		/*
		for(i=0;i<MAXNUMCONNECTIONS;i++)
		{
			printf("canale %d: sent %d received %d  tot %d\n",
					i, Ricevuti[i][0],Ricevuti[i][1], Ricevuti[i][0]+Ricevuti[i][1] );
		}
		*/
		printf("PKT - numspediti %ld  numscartati %ld  numscartatiSENZAICMP %ld\n", numspediti, numscartati, numscartatiSENZAICMP );
		printf("BYTES - bytespediti %lu  bytescartati %lu\n", bytespediti, bytescartati );

		fflush(stdout);
	}
	exit(0);
	return;
}

void stampa_fd_set(char *str, fd_set *pset)
{
	int i;
	printf("%s ",str);
	for(i=0;i<100;i++) if(FD_ISSET(i,pset)) printf("%d ", i);
;	printf("\n");
	fflush(stdout);
}

int get_local_port(int socketfd)
{
	int ris;
	struct sockaddr_in Local;
	int addr_size;

	addr_size=sizeof(Local);
	memset( (char*)&Local,0,sizeof(Local));
	Local.sin_family		=	AF_INET;
	ris=getsockname(socketfd,(struct sockaddr*)&Local,&addr_size);
	if(ris<0) { perror("getsockname() failed: "); return(0); }
	else {
		/*
		fprintf(stderr,"IP %s port %d\n", inet_ntoa(Local.sin_addr), ntohs(Local.sin_port) );
		*/
		return( ntohs(Local.sin_port) );
	}
}


int check_port(uint16_t port_number_local)
{
	int i;
	
	for(i=0;i<MAXNUMCONNECTIONS;i++)	
	{
		if( (coppiafd[i].attivo==1)  &&
			(
				(
					(coppiafd[i].port_number_latosender==port_number_local) &&
					(coppiafd[i].fd_latosender>0) &&
					(coppiafd[i].fd_latoreceiver>0)
				)

				||
				(
					(coppiafd[i].port_number_latoreceiver==port_number_local) &&
					(coppiafd[i].fd_latoreceiver>0) &&
					(coppiafd[i].fd_latosender>0)
				)
			)
		  )
		  return(1);
	}
	return(0);
}

void aggiungi_in_ordine(ELEMENTOLISTA *p)
{
	ELEMENTOLISTA* *pp=&root;

	if(p==NULL) 
		return;
	while(*pp!=NULL) {
		if( minore( &(p->timeout) , &( (*pp)->timeout) ) ) {
			ELEMENTOLISTA *tmp;
			tmp=*pp;
			*pp=p;
			p->next=tmp;
			tmp=NULL;
			return;
		}
		else
			pp = &( (*pp)->next );
	}
	if(*pp==NULL) {
		*pp=p;
		p->next=NULL;
	}
}

void free_pkt(ELEMENTOLISTA* *proot)
{
	ELEMENTOLISTA *tmp;

	if(proot) {
		if(*proot) {
			tmp=*proot;
			*proot = (*proot)->next;
			if(tmp->buf) {
				free(tmp->buf);
				tmp->buf=NULL;
			}
			free(tmp);
		}
	}
}

void close_coppia(int i)
{
	if(coppiafd[i].attivo==1)
	{
		coppiafd[i].attivo=0;
		if(coppiafd[i].fd_latosender>=0) 
		{
			FD_CLR(coppiafd[i].fd_latosender,&all);
			close(coppiafd[i].fd_latosender);
			coppiafd[i].fd_latosender=-1;
		}
		if(coppiafd[i].fd_latoreceiver>=0)
		{
			FD_CLR(coppiafd[i].fd_latoreceiver,&all);
			close(coppiafd[i].fd_latoreceiver);
			coppiafd[i].fd_latoreceiver=-1;
		}
	}
}


int cambia_stato_canale_se_scaduto_burst(int i /* indice canale */ , double percentuale_errore )
{
	struct timeval now;

	if( percentuale_errore <= ((double)0.0) ) /* non cambio stato */
		return(0);	

	gettimeofday(&now,NULL);

	/* controllo se e' necessario cambiare stato di spedizione */
	if( minoreouguale( &(coppiafd[i].ist_prossimo_cambio_stato), &now ) ) {  
		/* scaduto il burst, cambio stato */
		int msec_nuovo_stato;

		if( coppiafd[i].stato_trasmissione==1) {
			coppiafd[i].stato_trasmissione=0; /* perdere da adesso */
			msec_nuovo_stato=random()%3000;
			if(msec_nuovo_stato<2000) msec_nuovo_stato=2000;
			coppiafd[i].ist_prossimo_cambio_stato=now;
			coppiafd[i].ist_prossimo_cambio_stato.tv_sec  += (msec_nuovo_stato/1000);
			coppiafd[i].ist_prossimo_cambio_stato.tv_usec += ((msec_nuovo_stato*1000)%1000000L);
			normalizza( &(coppiafd[i].ist_prossimo_cambio_stato) );

			printf("cambiato stato del canale %s %d in BURST %s\n", ROSSO, i, BIANCO );
		}
		else { /* coppiafd[i].stato_trasmissione==0 */
			coppiafd[i].stato_trasmissione=1; /* spedire da adesso */
			/* sequenze di spediti tra 3 sec e 10 sec */
			msec_nuovo_stato=random()%10000; 
			if(msec_nuovo_stato<3000) msec_nuovo_stato=3000;
			coppiafd[i].ist_prossimo_cambio_stato=now;
			coppiafd[i].ist_prossimo_cambio_stato.tv_sec  += (msec_nuovo_stato/1000);
			coppiafd[i].ist_prossimo_cambio_stato.tv_usec += ((msec_nuovo_stato*1000)%1000000L);
			normalizza( &(coppiafd[i].ist_prossimo_cambio_stato) );

			printf("cambiato stato del canale %s %d in TRASMISSIONE %s\n", VERDE, i, BIANCO );
		}	
		/* cambiato stato canale */
		return(1);
	}
	else {
		/* NON cambiato stato canale */
		/*
		if( coppiafd[i].stato_trasmissione==1)
			printf("NON SCADUTO TIMEOUT RIMANE stato del canale %d in TRASMISSIONE\n", i);
		else
			printf("NON SCADUTO TIMEOUT RIMANE stato del canale %d in BURST\n", i);
		*/
		return(0);	
	}
}


/* 
	restituisce 0 se ricevo niente
	restituisce 1 se ricevo ma scarto senza mandare messaggio ICMP
	restituisce 2 se ricevo ma scarto mandando piu' tardi al mittente un messaggio ICMP
	restituisce 3 se ricevo e inoltro verso la destinazione
*/
int ricevo_inserisco(int i, uint32_t *pidmsg, uint32_t fd_latoricevere, uint32_t fd_latospedire,
					 uint16_t local_port_number_latospedire, uint16_t local_port_number_latoricevere, 
					 uint16_t remote_port_number_lato_spedire, uint16_t remote_port_number_latoricevere, 
					 char *remote_IP_string_latoricevere, char *remote_IP_string_latospedire )
{
	/* leggo, calcolo ritardo e metto in lista da spedire */
	char buf[65536];
	struct sockaddr_in From;
	int Fromlen; int ris;

	/* wait for datagram */
	do {
		memset(&From,0,sizeof(From));
		Fromlen=sizeof(struct sockaddr_in);
		ris = recvfrom ( fd_latoricevere, buf, (int)65536, 0, (struct sockaddr*)&From, &Fromlen);
	} while( (ris<0) && (errno==EINTR) );
	if (ris<0) {
		if(errno==ECONNRESET) {
			perror("recvfrom() failed (ECONNRESET): ");
			fprintf(stderr,"ma non chiudo il socket\n");
			fflush(stderr);
		}
		else {
			perror("recvfrom() failed: "); fflush(stderr);
			close_coppia(i);
		}
		return(0);
	}
	else if(ris>0) {
		int casuale;
		struct timeval now;

		/* VERIFICO CHE SIA UN PACCHETTO DI TIPO BODY il quinto byte e' una "B" */
		if( ris<=sizeof(COMMON_HEADER) ) {
			fprintf(stderr,"ricevuto  pkt troppo piccolo - scarto\n" );
			return(0);
		}
		else if (     ((COMMON_HEADER*)buf)->tipo != 'B'   ) {
			fprintf(stderr,"ricevuto  pkt NON DI TIPO BODY - scarto\n" );
			return(0);
		}
		memcpy( (char*)pidmsg, (char*)&(((COMMON_HEADER*)buf)->id_network_order) , sizeof(uint32_t) );

		fprintf(stderr,"ricevuto  pkt id %lu da port %d\n", ntohl(*pidmsg), get_local_port(fd_latoricevere) );

		/* decido se spedire o scartare */
		gettimeofday(&now,NULL);
		casuale=random()%100 - 3*abs ( sin( (now.tv_sec-coppiafd[i].sec_istcreazione)/8.0 ) );
		/* printf("CASUALE %d\n", casuale); */

		/* modifica */
		if( PERCENTUALE_ERRORE > 0 ) {
			if(casuale<=2  /*PERCENTUALE_ERRORE*/) 
			{		
				/* scarto almeno il 2+3% dei pkt indipendentemente dai burst */
				/* SCARTO SENZA MANDARE UN MESSAGGIO ICMP */
				numscartatiSENZAICMP++;
				bytescartati+=ris;
				return(1);
			}
		}
		
		/* 	scarto ulteriormente 
			burst di perdite di durata tra 100 ms e 1000 ms
			intervallati da sequenze di spediti tra 1 sec e 12 sec
		*/
		if( coppiafd[i].stato_trasmissione==0) {

			/* SCARTO MANDANDO UN MESSAGGIO ICMP solo 4 volte su 5 */
			casuale=random()%100;

			if( casuale <20 ) {
				/* scarto ma non mando ICMP */
				numscartatiSENZAICMP++;
				bytescartati+=ris;
				return(1);
			}
			else {
				uint32_t local_counter_network_order;

				/* alloco spazio per il pacchetto ICMP */
				ELEMENTOLISTA *p;
				struct timeval delay;

				p=(ELEMENTOLISTA *)malloc(sizeof(ELEMENTOLISTA));
				if(p==NULL) { perror("malloc failed: "); exit(9); }
				p->buf=(char*)malloc( sizeof(ICMP) );
				if(p->buf==NULL) { perror("malloc failed: "); exit(9); }

				p->cmd=CMD_SEND;
				p->len=sizeof(ICMP); /* dimensione del pacchetto ICMP */

				/* NOTARE CHE SPEDISCO DALLO STESSO LATO DA CUI HO RICEVUTO */
				p->fd=fd_latoricevere;
				p->port_number_local=local_port_number_latoricevere;
				p->port_number_dest=remote_port_number_latoricevere;
				strcpy( p->IP_dest, remote_IP_string_latoricevere );

				/* inserisco il tipo 'I? e l'identificatore del messaggio ricevuto e scartato */
				local_counter_network_order=htonl(local_counter);
				local_counter++;
				memcpy( (char*)&( ((ICMP*)(p->buf))->id_network_order), (char*)&local_counter_network_order, sizeof(uint32_t) ); 
				((ICMP*)(p->buf))->tipo='I';
				memcpy( (char*)&( ((ICMP*)(p->buf))->packet_lost_id_network_order) , (char*)pidmsg, sizeof(uint32_t) ); 


				/* calcolo il ritardo da aggiungere e calcolo il timeout */
				delay.tv_sec=0;
				/* tra 50 e 140 ms */
				delay.tv_usec=	50000 /* BASE */
								+ (random()%20000) /* VARIAZIONE CASUALE */
								+ abs(50000*sin( (now.tv_sec-coppiafd[i].sec_istcreazione)/8.0 )); /* ANDAMENTO */
				/* printf("DELAY %d\n", delay.tv_usec); */
;
				gettimeofday(&(p->timeout),NULL);
				somma(p->timeout,delay,&(p->timeout));
				/* metto in lista */
				aggiungi_in_ordine(p);
				p=NULL;
				numscartati++;
				bytescartati+=ris;

				return(2);
			}
		}
		else {
			/* alloco spazio per il pacchetto dati */
			ELEMENTOLISTA *p;
			struct timeval delay;

			p=(ELEMENTOLISTA *)malloc(sizeof(ELEMENTOLISTA));
			if(p==NULL) { perror("malloc failed: "); exit(9); }
			p->buf=(char*)malloc(ris);
			if(p->buf==NULL) { perror("malloc failed: "); exit(9); }

			p->cmd=CMD_SEND;
			p->len=ris; /* dimensione del pacchetto */
			p->fd=fd_latospedire;
			p->port_number_local=local_port_number_latospedire;
			p->port_number_dest=remote_port_number_lato_spedire;
			strcpy( p->IP_dest, remote_IP_string_latospedire );

			/* copio il messaggio ricevuto */
			memcpy(p->buf,buf,ris);
			/* calcolo il ritardo da aggiungere e calcolo il timeout */
			delay.tv_sec=0;
			/* tra 50 e 140 ms */
			delay.tv_usec=	30000 /* BASE */
							+ (random()%20000) /* VARIAZIONE CASUALE */
							+ abs(50000*sin( (now.tv_sec-coppiafd[i].sec_istcreazione)/8.0 )); /* ANDAMENTO */
			/* printf("DELAY %d\n", delay.tv_usec); */
;
			gettimeofday(&(p->timeout),NULL);
			somma(p->timeout,delay,&(p->timeout));
			/* metto in lista */
			aggiungi_in_ordine(p);
			p=NULL;
			numspediti++;
			bytespediti+=ris;

			return(3);
		}
	}
	return(0);
}

void schedula_creazione_nuova_porta(void)
{
	/* alloco spazio per il pacchetto dati */
	ELEMENTOLISTA *p;
	struct timeval delay;

	p=(ELEMENTOLISTA *)malloc(sizeof(ELEMENTOLISTA));
	if(p==NULL) { perror("malloc failed: "); exit(9); }
	p->buf=NULL;
	p->cmd=CMD_ADDPORT;
	/* calcolo il ritardo da aggiungere e calcolo il timeout */
	delay.tv_sec=0;
	delay.tv_usec=500000; /* mezzo secondo */
	gettimeofday(&(p->timeout),NULL);
	somma(p->timeout,delay,&(p->timeout));
	/* metto in lista */
	aggiungi_in_ordine(p);
	p=NULL;
}


void creazione_nuova_coppia_porte(int sec_prossimo_cambio_stato_trasmissione)
{
	int i, ris;

	for(i=0;i<MAXNUMCONNECTIONS;i++)
	{
		if(coppiafd[i].attivo==0) /* vuoto */
		{
			struct timeval now;

			ris=UDP_setup_socket_bound( &(coppiafd[i].fd_latosender), 
										first_local_port_number_sender_side+counter_localport_sender_side, 65535, 65535 );
			if (!ris)
			{	printf ("UDP_setup_socket_bound() failed\n");
				exit(1);
			}
			coppiafd[i].port_number_latosender=first_local_port_number_sender_side+counter_localport_sender_side;
			counter_localport_sender_side++;

			ris=UDP_setup_socket_bound( &(coppiafd[i].fd_latoreceiver), first_local_port_number_receiver_side+counter_localport_receiver_side, 65535, 65535 );
			if (!ris)
			{	printf ("UDP_setup_socket_bound() failed\n");
				exit(1);
			}
			coppiafd[i].port_number_latoreceiver=first_local_port_number_receiver_side+counter_localport_receiver_side;
			counter_localport_receiver_side++;

			coppiafd[i].attivo=1;

			FD_SET(coppiafd[i].fd_latosender,&all);
			if(coppiafd[i].fd_latosender>maxfd)
				maxfd=coppiafd[i].fd_latosender;
			FD_SET(coppiafd[i].fd_latoreceiver,&all);
			if(coppiafd[i].fd_latoreceiver>maxfd)
				maxfd=coppiafd[i].fd_latoreceiver;
			gettimeofday(&now,NULL);
			coppiafd[i].sec_istcreazione=now.tv_sec;

			/* modifica per burst di perdite */
			coppiafd[i].stato_trasmissione=1; /* 0 perdere, 1 spedire */
			coppiafd[i].ist_prossimo_cambio_stato=now;
			coppiafd[i].ist_prossimo_cambio_stato.tv_sec += sec_prossimo_cambio_stato_trasmissione;
			/* fine modifica per burst di perdite */

			break;
		}
	}
}

void stampa_coppie_porte(void)
{
	int i;

	for(i=0;i<MAXNUMCONNECTIONS;i++)
	{
		if(coppiafd[i].attivo==1)
		{
			fprintf(stderr,"coppia %d: latomobile fd %lu port %d - latofixed fd %lu port %d \n",
					i, coppiafd[i].fd_latosender, get_local_port(coppiafd[i].fd_latosender), 
					coppiafd[i].fd_latoreceiver, get_local_port(coppiafd[i].fd_latoreceiver) );
		}
	}
}

int scaduto_timeout(struct timeval *ptimeout)
{
	struct timeval now;
	gettimeofday(&now,NULL);
	if( minoreouguale(ptimeout,&now) )
		return(1);
	else
	{
#ifdef VICDEBUG
		fprintf(stderr,"scaduto_timeout: ptime %d s %d us    now %d s %d us\n",
			(*ptimeout).tv_sec,(*ptimeout).tv_usec,now.tv_sec,now.tv_usec	);
#endif
		return(0);
	}
}

struct timeval compute_timeout_first_pkt(void)
{
	struct timeval now, attesa;

	gettimeofday(&now,NULL);
	attesa=differenza(root->timeout,now);
	return(attesa);
}

int send_udp(uint32_t socketfd, char *buf, uint32_t len, uint16_t port_number_local, char *IPaddr, uint16_t port_number_dest)
{
	int ris;
	struct sockaddr_in To;
	int addr_size;

	/* assign our destination address */
	memset( (char*)&To,0,sizeof(To));
	To.sin_family		=	AF_INET;
	To.sin_addr.s_addr  =	inet_addr(IPaddr);
	To.sin_port			=	htons(port_number_dest);

#ifdef VICDEBUG
	fprintf(stderr,"send_udp sending to %d\n", port_number_dest);
#endif

	addr_size = sizeof(struct sockaddr_in);
	/* send to the address */
	ris = sendto(socketfd, buf, len , MSG_NOSIGNAL, (struct sockaddr*)&To, addr_size);
	if (ris < 0) {
		printf ("sendto() failed, Error: %d \"%s\"\n", errno,strerror(errno));
		return(0);
	}
	return(1);
}


#define PARAMETRIDEFAULT "./EmulatoreRete.exe 127.0.0.1 60000 61000 62000 127.0.0.1 63000 15 -1 1"
void usage(void) 
{  printf ("usage: ./EmulatoreRete.exe SENDERIP SENDERPORT FIRSTSENDERSIDEPORT FIRSTRECEIVERSIDEPORT RECEIVERIP RECEIVERPORT"
		   " PACKETLOSSPERC SEME  DEBUG{y,n}\n"
				"esempio: "  PARAMETRIDEFAULT "\n" );
}

int main(int argc, char *argv[])
{
	uint16_t remote_port_number_sender, remote_port_number_receiver;
	char remote_sender_IP_string[ADDRESSSTRINGLEN], remote_receiver_IP_string[ADDRESSSTRINGLEN];
	int seme=-1;
	char debug='n';
	int i, ris;

	if(argc==1) { 
		printf ("uso i 9 parametri di default \n%s \n", PARAMETRIDEFAULT);
		strcpy( remote_sender_IP_string, "127.0.0.1" );
		remote_port_number_sender = 60000;
		first_local_port_number_sender_side = 61000;
		first_local_port_number_receiver_side = 62000;
		strcpy( remote_receiver_IP_string, "127.0.0.1" );
		remote_port_number_receiver = 63000;
		PERCENTUALE_ERRORE=PERC_ERR;
		seme=-1; /* genera random in base al tempo */
		debug='y';
	}
	else if(argc!=10) { printf ("necessari 9 parametri\n"); usage(); exit(1);  }
	else { /* leggo parametri da linea di comando */
		strcpy( remote_sender_IP_string, argv[1] );
		remote_port_number_sender = atoi(argv[2]);
		first_local_port_number_sender_side = atoi(argv[3]);
		first_local_port_number_receiver_side = atoi(argv[4]);
		strcpy( remote_receiver_IP_string, argv[5] );
		remote_port_number_receiver = atoi(argv[6]);
		PERCENTUALE_ERRORE=atoi(argv[7]);
		seme=atoi(argv[8]);
		debug=argv[9][0];
	}

	for(i=0;i<MAXNUMCONNECTIONS;i++) {
		coppiafd[i].fd_latosender=-1;
		coppiafd[i].fd_latoreceiver=-1;
		coppiafd[i].attivo=0;
	}
	root=NULL;
	init_random(seme);
	if ((signal (SIGHUP, sig_print)) == SIG_ERR) { perror("signal (SIGHUP) failed: "); exit(2); }
	if ((signal (SIGINT, sig_print)) == SIG_ERR) { perror("signal (SIGINT) failed: "); exit(2); }
	if ((signal (SIGTERM, sig_print)) == SIG_ERR) { perror("signal (SIGTERM) failed: "); exit(2); }

	/* setup dei socket UDP	*/

	FD_ZERO(&all);
	maxfd=-1;
	for(i=0;i<MAXNUMCONNECTIONS;i++) creazione_nuova_coppia_porte( 5+(i*10) /*una perde dopo 5 sec, l'altra dopo 10 sec */  );
	/* differenzio le due coppie */
	coppiafd[0].sec_istcreazione-=10;

#ifdef VICDEBUG
	stampa_coppie_porte();
#endif

	for(;;)
	{
		do {
			struct timeval timeout;

			rdset=all;
#ifdef VICDEBUG
			stampa_fd_set("rdset prima",&rdset);
#endif
			if(root!=NULL) {
				timeout=compute_timeout_first_pkt();
#ifdef VICDEBUG
				fprintf(stderr,"set timeout %d sec %d usec\n", timeout.tv_sec,timeout.tv_usec );
#endif
				ris=select(maxfd+1,&rdset,NULL,NULL,&timeout);
			}
			else {
#ifdef VICDEBUG
				fprintf(stderr,"set timeout per verificare necessità di modifica stato canale\n");
#endif
				timeout.tv_sec=0;
				timeout.tv_usec=100000;
				ris=select(maxfd+1,&rdset,NULL,NULL,&timeout);
			}
		} while( (ris<0) && (errno==EINTR) && (printed==0) );
		if(ris<0) {
			perror("select failed: ");
			exit(1);
		}
		if(printed!=0) {
			fprintf(stderr,"esco da select dopo avere gia' segnalato la chiusura, TERMINO\n");
			exit(1);
		}
#ifdef VICDEBUG
		stampa_fd_set("rdset dopo",&rdset);
#endif


		/* spedisco o elimino i pkt da spedire */
		while( (root!=NULL) && scaduto_timeout( &(root->timeout) )   ) {
			if(root->cmd==CMD_SEND) {
				if( check_port(root->port_number_local) ) {
					ris=send_udp(root->fd,root->buf,root->len,root->port_number_local, root->IP_dest ,root->port_number_dest);
					fprintf(stderr,"pkt id %lu tipo %c sent %d from %d to %d\n", 
													ntohl(((COMMON_HEADER*)(root->buf))->id_network_order),
													((COMMON_HEADER*)(root->buf))->tipo,
													ris, root->port_number_local, root->port_number_dest );
					fflush(stderr);
				}
				free_pkt(&root);
			}
		}

		/* gestione burst nei canali */
		for(i=0;i<MAXNUMCONNECTIONS;i++)	
		{
			if(coppiafd[i].attivo==1)
			{
				int ris;
				ris=cambia_stato_canale_se_scaduto_burst(i,PERCENTUALE_ERRORE);
			}
		}

		/* gestione pacchetti in arrivo */
		for(i=0;i<MAXNUMCONNECTIONS;i++)	
		{
			uint32_t idmsg;
			if(coppiafd[i].attivo==1)
			{
				if( FD_ISSET(coppiafd[i].fd_latosender,&rdset) )
				{
					#ifdef VICDEBUG
					fprintf(stderr,"leggo da lato sender\n");
					#endif
					/* leggo, calcolo ritardo e metto in lista da spedire verso il fixed */
					ris=ricevo_inserisco(	i, &idmsg, coppiafd[i].fd_latosender, coppiafd[i].fd_latoreceiver,
											coppiafd[i].port_number_latoreceiver, coppiafd[i].port_number_latosender, 
											remote_port_number_receiver, remote_port_number_sender,
											remote_sender_IP_string, remote_receiver_IP_string );
					if(ris==0)		{ 
						P("NULLAsender");  /* non ricevuto niente, o errore sul socket */
						; 
					}
					else if(ris==1)	{ 
						#ifdef VICDEBUG
						fprintf(stderr,"SCARTO idmsg %u da latosender SENZA spedire msg ICMP al mittente\n", idmsg);
						#endif
					}
					else if(ris==2)	{ 
						#ifdef VICDEBUG
						fprintf(stderr,"SCARTO idmsg %u da latosender, piu' tardi spediro' msg ICMP al mittente\n", idmsg);
						#endif
					}
					else if(ris==3)	{ 
						#ifdef VICDEBUG
						P("SPEDISCOsender"); 
						#endif
					}
					else { P("CHEE'sender "); ;}
				}
			}

			if(coppiafd[i].attivo==1)
			{
				if( FD_ISSET(coppiafd[i].fd_latoreceiver,&rdset) )
				{
					#ifdef VICDEBUG
					fprintf(stderr,"leggo da lato receiver\n");
					#endif
					/* leggo, calcolo ritardo e metto in lista da spedire */
					ris=ricevo_inserisco(	i, &idmsg, coppiafd[i].fd_latoreceiver, coppiafd[i].fd_latosender, 
											coppiafd[i].port_number_latosender, coppiafd[i].port_number_latoreceiver, 
											remote_port_number_sender, remote_port_number_receiver, 
											remote_receiver_IP_string, remote_sender_IP_string );
					if(ris==0)		{ 
						P("NULLAreceiver");  /* non ricevuto niente, o errore sul socket */
						; 
					}
					else if(ris==1)	{ 
						#ifdef VICDEBUG
						fprintf(stderr,"SCARTO idmsg %u da latoreceiver SENZA spedire msg ICMP al mittente\n", idmsg);
						#endif
					}
					else if(ris==2)	{ 
						#ifdef VICDEBUG
						fprintf(stderr,"SCARTO idmsg %u da latoreceiver, piu' tardi spediro' msg ICMP al mittente\n", idmsg);
						#endif
					}
					else if(ris==3)	{ 
						#ifdef VICDEBUG
						P("SPEDISCOreceiver"); 
						#endif
					}
					else { P("CHEE'receiver "); ;}
				}

			}
		} /* fine for i */

	} /* fine for ;; */

	for(i=0;i<MAXNUMCONNECTIONS;i++)
		if(coppiafd[i].attivo==1)
			close_coppia(i);
	return(0);
}

