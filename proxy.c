#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define DATA_BUF_SIZE 4096
#define CACHE_REG_SIZE ((12 + MAXLINE) * 10)
#define MAX_CACHED_OBJ 10

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

static unsigned char *cache;
static unsigned char *cacheReg;
static pthread_rwlock_t lockCache[MAX_CACHED_OBJ];

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
    char hostname[MAXLINE], port[MAXLINE], getpath[MAXLINE];
    char *p, *q;
    int clientfd;
    rio_t rio, riocli;
    int b_hostsent, b_useragentsent, b_connsent, b_proxyconnsent;
    unsigned char databuf[DATA_BUF_SIZE];
    unsigned char *objCache;
    int cachedSize;
    int bCanCache;;
    int i;
    int bCached;
    ssize_t n; 

    objCache = malloc(MAX_OBJECT_SIZE);
    bCanCache = 1;
    bCached = 0;

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

    for(i = 0; i < MAX_CACHED_OBJ; i++) {
        pthread_rwlock_rdlock(lockCache + i);
        printf("[IMPORTANT]\n\turi: %s\n\tkey: %s\n", uri, (char *)&cacheReg[i*(MAXLINE+12)+12]);
        if(!strcmp((char *)&cacheReg[i*(MAXLINE+12)+12], uri)) {
            printf("[GOTCHA]\n\tpos: %s\n", "1");
            pthread_rwlock_unlock(lockCache + i);
            pthread_rwlock_wrlock(lockCache + i);
            *((long long *)&cacheReg[i*(MAXLINE+12)]) = time(NULL);
            pthread_rwlock_unlock(lockCache + i);
            pthread_rwlock_rdlock(lockCache + i);
            cachedSize = *((int *)&cacheReg[i*(MAXLINE+12)+8]);
            printf("[IMPORTANT]\n\tcachedSize: %d\n", cachedSize);
            memcpy(objCache, cache + i*MAX_OBJECT_SIZE, cachedSize);
            bCached = 1;
            printf("[GOTCHA]\n\tpos: %s\n", "2");
        }          
        pthread_rwlock_unlock(lockCache + i);
        if(bCached)
            break;
    }
    if(bCached) {
        ssize_t nWritten, shouldWrite;
        int bFinish;
        nWritten = 0;
        printf("[GOTCHA]\n\tpos: %s\n", "3");
        bFinish = 0;
        while(1) {
            printf("[GOTCHA]\n\tpos: %s\n", "4");
            memset(databuf, 0, sizeof(databuf));
            shouldWrite = sizeof(databuf);
            if(nWritten + shouldWrite > cachedSize) {
                shouldWrite = cachedSize - nWritten;
                bFinish = 1;
            }
            memcpy(databuf, objCache+nWritten, shouldWrite);
            Rio_writen(fd, databuf, shouldWrite);
            printf("[IMPORTANT]\n\tcachedContent: %s\n", (char *)databuf);
            nWritten += shouldWrite;
            printf("[GOTCHA]\n\tpos: %s\n", "5");
            if(bFinish)
                break;
            printf("[GOTCHA]\n\tpos: %s\n", "6");
        }
        free(objCache);
        return;
    }
    cachedSize = 0;

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
        strcpy(getpath, p);
      } else {
        strcpy(port, "80");
        strcpy(getpath, "/");
      }
    } else {
      strcpy(port, "80");
      q = strchr(p, '/');
      if(q) {
        strncpy(hostname, p, q-p);
        hostname[q-p] = '\0';
        strcpy(getpath, q);
      } else {
        strcpy(hostname, p);
        strcpy(getpath, "/");
      }
    }

    printf("[Debug] parsed hostname & port & getpath:\n\t%s\n\t%s\n\t%s\n", hostname, port, getpath);
    clientfd = Open_clientfd(hostname, port);
    printf("[Debug] clientfd:\n\t%d\n", clientfd);

    sprintf(buf, "%s %s %s\r\n", "GET", getpath, "HTTP/1.0");
    n = strlen(buf);
    Rio_writen(clientfd, buf, n); 

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
        n = strlen(buf);
        Rio_writen(clientfd, buf, n);
        printf("[Debug] forwarded header:\n\t%s", buf);
    }
    if(!b_hostsent) {
        sprintf(buf, "%s: %s\r\n", "Host", hostname);
        n = strlen(buf);
        Rio_writen(clientfd, buf, n); 
    }
    if(!b_useragentsent) {
        sprintf(buf, "%s: %s", "User-Agent", user_agent_hdr);
        n = strlen(buf);
        Rio_writen(clientfd, buf, n); 
    }
    if(!b_connsent) {
        sprintf(buf, "%s: %s\r\n", "Connection", "close");
        n = strlen(buf);
        Rio_writen(clientfd, buf, n); 
    }
    if(!b_proxyconnsent) {
        sprintf(buf, "%s: %s\r\n", "Proxy-Connetion", "close");
        n = strlen(buf);
        Rio_writen(clientfd, buf, n); 
    }
    strcpy(buf, "\r\n");
    n = strlen(buf);
    Rio_writen(clientfd, buf, n); 

    Rio_readinitb(&riocli, clientfd);
    /* Deal with response headers */
    while(1) {
        Rio_readlineb(&riocli, buf, MAXLINE);
        if(!strcmp(buf, "\r\n")) 
            break;

        printf("%s", buf);
        Rio_writen(fd, buf, strlen(buf));
    }
    strcpy(buf, "\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Forward response content */
    while(1) {
        memset(databuf, 0, sizeof(databuf));
        if((n = Rio_readnb(&riocli, databuf, sizeof(databuf))) == 0)
            break;

        if(cachedSize + n > MAX_OBJECT_SIZE) 
            bCanCache = 0;
        if(bCanCache) {
            memcpy(objCache + cachedSize, databuf, n);
            cachedSize += n;
        }
        Rio_writen(fd, databuf, n);
    }

    Close(clientfd);

    for(i = 0; i < MAX_CACHED_OBJ; i++) {
        long long useCnt;
        pthread_rwlock_rdlock(lockCache + i);
        useCnt = *((long long *)&cacheReg[i*(MAXLINE+12)]);
        pthread_rwlock_unlock(lockCache + i);
        if(useCnt == -1)
            break;
    }
    if(i == MAX_CACHED_OBJ) {
        /* LRU eviction */
        long long lruCnt;
        int lruIndex;
        pthread_rwlock_rdlock(lockCache);
        lruCnt = *((long long *)&cacheReg[0]);
        pthread_rwlock_unlock(lockCache);
        lruIndex = 0;

        for(i = 0; i < MAX_CACHED_OBJ; i++) {
            long long useCnt;
            pthread_rwlock_rdlock(lockCache + i);
            useCnt = *((long long *)&cacheReg[i*(MAXLINE+12)]);
            pthread_rwlock_unlock(lockCache + i);
            if(useCnt < lruCnt) {
                lruCnt = useCnt;
                lruIndex = i;
            }
        }
        i = lruIndex;
    }
    
    pthread_rwlock_wrlock(lockCache + i);
    *((long long *)&cacheReg[i*(MAXLINE+12)]) = time(NULL);
    printf("[IMPORTANT]\n\tcachedSize original: %d\n", cachedSize);
    *((int *)&cacheReg[i*(MAXLINE+12)+8]) = cachedSize;
    printf("[IMPORTANT]\n\tcachedSize original boxed: %d\n", *((int *)&cacheReg[i*(MAXLINE+12)+8]));
    strcpy((char *)&cacheReg[i*(MAXLINE+12)+12], uri);
    memcpy(cache + i*MAX_OBJECT_SIZE, objCache, cachedSize);
    pthread_rwlock_unlock(lockCache + i);
    free(objCache);
}

int main(int argc, char *argv[])
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    int i;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    cache = mmap(NULL, MAX_CACHE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    cacheReg = mmap(NULL, CACHE_REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    for(i = 0; i < MAX_CACHED_OBJ; i++) {
        *((long long *)&cacheReg[i*(MAXLINE+12)]) = -1;
        cacheReg[i*(MAXLINE+12)+12] = '\0';
        pthread_rwlock_init(lockCache + i, NULL);
    }
    while (1) {
	pid_t pid;
        clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        pid = fork();
        if(pid == 0) {
            /* child */
            setsid(); /* detach child from parent */
            doit(connfd);
            Close(connfd);
            return 0;
        } else if(pid > 0) {
            /* parent */
            Close(connfd);
        }        
    }
    munmap(cache, MAX_CACHE_SIZE);
    munmap(cacheReg, CACHE_REG_SIZE);
    for(i = 0; i < MAX_CACHED_OBJ; i++)
        pthread_rwlock_destroy(lockCache + i);
    return 0;
}

