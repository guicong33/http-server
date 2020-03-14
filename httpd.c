#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <pool.c>


#define LISTENQ 1024
#define PORT 6666
#define EPOLL_SIZE 65535
#define SERVER_STRING "Server: myTinyhttpd/0.1.0\r\n"

void error_die(char*);
int tcp_listen(int);
void accept_request(int);
int get_line(int,char*,int);
void unimplement(int);
void bad_request(int);
void serve_file(int,const char*);
void cat(int,FILE*);
void headers(int,const char*);
void not_found(int);

int main(int argc,char* argv[])
{
	pool_init(10);
	int listenfd,clifd,efd;
	int port=PORT;
	pthread_t newthread;
	struct sockaddr_in cliaddr;
	size_t len=sizeof(cliaddr);
	struct epoll_event event,*peventwait;
	size_t event_cnt;

	if(argc>2)
	{
		printf("usage:Tinyhttpd [<port>]\n");
		return -1;
	}
	else if(argc==2)
	{
		port=atoi(argv[1]);
	}

	if((listenfd=tcp_listen(port))==-1)
		error_die("tcp_listen");
	printf("port %d is listening\n",port);

	int on=1;
	setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));

	efd=epoll_create(EPOLL_SIZE);
	if(efd==-1)
		error_die("epoll_create");
	peventwait=malloc(EPOLL_SIZE*sizeof(struct epoll_event));
	
	event.events=EPOLLIN;
	event.data.fd=listenfd;
	if(epoll_ctl(efd,EPOLL_CTL_ADD,listenfd,&event)==-1)
		error_die("epoll_ctl");	

	while(1)
	{
		event_cnt=epoll_wait(efd,peventwait,EPOLL_SIZE,-1);
		if(event_cnt==-1)
			error_die("epoll_wait");
		for(int i=0;i<event_cnt;++i)
		{
			if(peventwait[i].data.fd==listenfd)
			{
				
				
				clifd=accept(listenfd,(struct sockaddr*)&cliaddr,&len);
				if(clifd==-1)
					error_die("accept");
				printf("a new client %s:%d has connetced with server\n",inet_ntoa(cliaddr.sin_addr),ntohs(cliaddr.sin_port));

				event.events=EPOLLIN;
				event.data.fd=clifd;
				if(epoll_ctl(efd,EPOLL_CTL_ADD,clifd,&event)==-1)
					error_die("epoll_ctl(add client)");
			}
			else
			{
				pool_add_worker(accept_request, &peventwait[i].data.fd);
				//accept_request(peventwait[i].data.fd);
				shutdown(peventwait[i].data.fd,SHUT_WR);
				if(epoll_ctl(efd,EPOLL_CTL_DEL,peventwait[i].data.fd,NULL)==-1)
					error_die("epoll_ctl(delete)");				
					
			}
		}
	}
	pool_destroy();
	printf("process terminated!\n");
	free(peventwait);
	close(efd);
	close(listenfd);
	return 0;
}


/******************************************
 * deal with the request
 * close the socket file description int the end 
 * arg is the cosket file description which is connected
 ** *****************************************/
void accept_request(int fd)
{

	char method[255];
	char url[255];
	char path[512];
	char buf[1024];
	struct stat st;
	size_t index_scan=0;
	size_t nread;
	size_t i;


	nread=get_line(fd,buf,sizeof(buf));

	
	if(nread<0)
		return;
	//get the method
	while((index_scan<sizeof(method)-1)&&(!isspace(buf[index_scan])))
	{
		method[index_scan]=buf[index_scan];
		++index_scan;
	}
	method[index_scan]='\0';
	
	printf("method:%s\n",method);

	if(strcmp(method,"GET")!=0)
	{
		unimplement(fd);
		return;
	}	
	//get the url
	while(index_scan<nread&&isspace(buf[index_scan]))
	{
		index_scan++;
	}
	i=0;
	while((index_scan<nread&&i<sizeof(url)-1)&&(!isspace(buf[index_scan])))
	{
		url[i++]=buf[index_scan++];
	}
	url[i]='\0';

	printf("URL:%s\n",url);

	//get the URL parameter and delete it from url
	char* query_string=url;
	while((*query_string!='\0')&&(*query_string!='?'))
		query_string++;
	if(*query_string=='?')
	{
		*query_string='\0';
		++query_string;
	}
	//get the path
	snprintf(path,sizeof(path),"htdocs%s",url);
	if(path[strlen(path)-1]=='/')
		strcat(path,"index.html");

	printf("path:%s\n",path);
	
	if(stat(path,&st)==-1)// did not found the request file
	{
		not_found(fd);
		return;
	}
	//if the path is a path instead of a file
	if(S_ISDIR(st.st_mode))
	{
		strcat(path,"/index.html");
	}
	
	printf("path:%s\n",path);

	//delete  message header
	
	serve_file(fd,path);
}

void serve_file(int fd,const char* filename)
{
	printf("serve the client with:%s\n",filename);
	FILE* resource=fopen(filename,"r");
	if(resource==NULL)
		not_found(fd);
	else
	{
		printf("have found the file:%s\n",filename);
		headers(fd,filename);
		cat(fd,resource);
	}
	fclose(resource);
}
/******************************************
 * sent the reponse method is unimplement to the client
 * *******************************************/
void unimplement(int fd)
{
	printf("sending unimplement to client\n");
	char buf[1024];

	strcpy(buf,"HTTP/1.0 501 Method Not Implemented\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,SERVER_STRING);
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"Content-Type: text/html\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"<HTML><HEAD><TITLE>Method Not Implemented\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"</TITLE></HEAD>\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"<BODY><P>HTTP request method not supported. \r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"</BODY></HTML>\r\n");
	send(fd,buf,strlen(buf),0);
}

/******************************************
 * sent the reponse request file is not found  to the client
 * *******************************************/
void not_found(int fd)
{
	printf("sending not_found to client\n");
	char buf[1024];

	strcpy(buf,"HTTP/1.0 404 Not Found\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,SERVER_STRING);
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"Content-Type: text/html\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"<HTML><HEAD><TITLE>Request is not Found\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"</TITLE></HEAD>\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"<BODY><P>HTTP request Not Found. \r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"</BODY></HTML>\r\n");
	send(fd,buf,strlen(buf),0);
}



/********************************
* sent the reponse a bad request to the client
 ** **************************/
void bad_request(int fd)
{
	printf("sending bad_request to client\n");
	char buf[1024];

	strcpy(buf,"HTTP/1.0 400 BAD REQUEST\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,SERVER_STRING);
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"Content-Type: text/html\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"<HTML><HEAD><TITLE>Your browser sent a bad request, \r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"such as a POST without a Content-Length. </TITLE></HEAD>\r\n");
	
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"<BODY><P>Bad request. \r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"</BODY></HTML>\r\n");
	send(fd,buf,strlen(buf),0);
}

/********************************
* sent the 200 header to the client
* filename could be used to determine the file type
 ** **************************/
void headers(int fd,const char* filename)
{
	printf("sending OK headers to client\n");
	char buf[1024];

	strcpy(buf,"HTTP/1.0 200 OK\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,SERVER_STRING);
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"Content-Type: text/html\r\n");
	send(fd,buf,strlen(buf),0);
	strcpy(buf,"\r\n");
	send(fd,buf,strlen(buf),0);

	printf("sending OK headers to client finished\n");
}

/******************************
 * write the message from the FILE
 ** ***************************/
void cat(int fd,FILE* source)
{
	printf("sending cat to clients\n");
	char buf[1024];
	fgets(buf,sizeof(buf),source);
	while(!feof(source))
	{
		send(fd,buf,strlen(buf),0);
		fgets(buf,sizeof(buf),source);
	}
	printf("sending cat to clients finished\n");
}

/******************************
 * get a line from the file description
 * ***********************************/
int get_line(int fd,char buf[],int len)
{
	char c='a';
	int count=0;
	int nread;
	while(c!='\n'&&count<len-1)
	{
		nread=recv(fd,&c,1,0);
		if(nread<0)
		{
			perror("get_line");
			return -1;
		}
		else if(nread>0)
		{
			if(c=='\r')
			{
				nread=recv(fd,&c,1,MSG_PEEK);
				if(nread<0)
				{
					perror("get_line");
					return -1;
				}
				else if(nread>0&&c=='\n')
					nread=recv(fd,&c,1,MSG_PEEK);
				else
					c='\n';
			}
			buf[count++]=c;
		}
		else
			c='\n';
	}
	buf[count]='\0';
	printf("receive %d byte from client: %s\n",count,buf);
	return count;
}

void error_die(char* prefix)
{
	perror(prefix);
	exit(-1);
}

/***************************************
 * init a socket and bind it with INADDR_ANY:port
 * and transfrom it to a listening file description
 * ***********************************/
int tcp_listen(int port)
{
	struct sockaddr_in seraddr;
	int res;
	int fd=socket(PF_INET,SOCK_STREAM,0);
	if(fd==-1)
		return -1;

	memset(&seraddr,0,sizeof(seraddr));
	seraddr.sin_family=AF_INET;
	seraddr.sin_addr.s_addr=htonl(INADDR_ANY);
	seraddr.sin_port=htons(port);
	if((bind(fd,(struct sockaddr*)(&seraddr),sizeof(struct sockaddr)))==-1)
		return -1;

	if((listen(fd,LISTENQ))==-1)
		return -1;	
	return fd;
}
