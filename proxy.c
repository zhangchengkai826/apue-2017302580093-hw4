#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void doit(int fd) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE];
    char *p, *q;
    int clientfd;
    rio_t rio, riocli;
    int b_hostsent, b_useragentsent, b_connsent, b_proxyconnsent;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);   
    if (strcasecmp(method, "GET")) {                  
        clienterror(fd, method, "501", "Not Implemented",
                    "This proxy does not handle requests other than GET");
        return;
    }                                                  

    /* Parse hostname & port from uri */
    p = strchr(uri, ':');
    p += 3;
    q = strchr(p, ':');
    if(q) {
      strncpy(hostname, p, q-p);
      hostname[q-p] = '\0';
      q++;
      p = strchr(q, '/');
      if(p) {
        strncpy(port, q, p-q);
        port[p-q] = '\0';
      } else {
        strcpy(port, "80");
      }
    } else {
      strcpy(port, "80");
      q = strchr(p, '/');
      if(q) {
        strncpy(hostname, p, q-p);
        hostname[q-p] = '\0';
      } else {
        strcpy(hostname, p);
      }
    }

    printf("[Debug] parsed hostname & port:\n\t%s\n\t%s\n", hostname, port);
    clientfd = Open_clientfd(hostname, port);
    printf("[Debug] clientfd:\n\t%d\n", clientfd);

    sprintf(buf, "%s %s %s\r\n", "GET", uri, "HTTP/1.0");
    Rio_writen(clientfd, buf, strlen(buf)); 

    b_hostsent = 0;
    b_useragentsent = 0;
    b_connsent = 0;
    b_proxyconnsent = 0;
    /* Deal with headers */
    while(1) {
	char key[MAXLINE], val[MAXLINE];
        char *p;

        Rio_readlineb(&rio, buf, MAXLINE);
        if(!strcmp(buf, "\r\n")) 
            break;

	/* Parse headers */
        printf("[Debug] original header:\n\t%s", buf);
        p = strchr(buf, ':');
        strncpy(key, buf, p-buf);
        key[p-buf] = '\0';
        strcpy(val, p+2);
        printf("[Debug] header:\n\tkey: %s\n\tval: %s", key, val);

        if(!strcasecmp(key, "Host")) {
            b_hostsent = 1;
        } else if(!strcasecmp(key, "User-Agent")) {
            b_useragentsent = 1;
            strcpy(val, user_agent_hdr); 
        } else if(!strcasecmp(key, "Connection")) {
            b_connsent = 1;
            strcpy(val, "close\r\n");
        } else if(!strcasecmp(key, "Proxy-Connection")) {
            b_proxyconnsent = 1;
            strcpy(val, "close\r\n");
        }
        sprintf(buf, "%s: %s", key, val); /* Reconstruct header */
        Rio_writen(clientfd, buf, strlen(buf)); /* Forward it */
        printf("[Debug] forwarded header:\n\t%s", buf);
    }
    if(!b_hostsent) {
        sprintf(buf, "%s: %s\r\n", "Host", hostname);
        Rio_writen(clientfd, buf, strlen(buf)); 
    }
    if(!b_useragentsent) {
        sprintf(buf, "%s: %s", "User-Agent", user_agent_hdr);
        Rio_writen(clientfd, buf, strlen(buf)); 
    }
    if(!b_connsent) {
        sprintf(buf, "%s: %s\r\n", "Connection", "close");
        Rio_writen(clientfd, buf, strlen(buf)); 
    }
    if(!b_proxyconnsent) {
        sprintf(buf, "%s: %s\r\n", "Proxy-Connetion", "close");
        Rio_writen(clientfd, buf, strlen(buf)); 
    }
    strcpy(buf, "\r\n");
    Rio_writen(clientfd, buf, strlen(buf)); /* End of header */

    Rio_readinitb(&riocli, clientfd);
    if(!Rio_readlineb(&riocli, buf, MAXLINE)) {
      fprintf(stderr, "%s\n", "zero respond!");
      Close(clientfd);
      return;
    }
    printf("%s", buf);
    Close(clientfd);
}

int main(int argc, char *argv[])
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
	doit(connfd);                                            
	Close(connfd);                                            
    }

    return 0;
}

