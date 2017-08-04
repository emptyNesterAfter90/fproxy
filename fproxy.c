#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

bool resolv_host(char *req, int req_len, struct in_addr *addr, unsigned short int *port);
void http_proxy(int conn);

int main(void){
    signal(SIGCHLD, SIG_IGN);

    int listenfd;
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    int on = 1;
    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0){
        perror("setsockopt error");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(80);

    if(bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        perror("bind error");
        exit(EXIT_FAILURE);
    }

    if(listen(listenfd, SOMAXCONN) < 0){
        perror("listen error");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in peeraddr;
    socklen_t peerlen = sizeof(peeraddr);
    int conn;
    pid_t pid;
    while(1){
        if((conn = accept(listenfd, (struct sockaddr *)&peeraddr, &peerlen)) < 0){
            perror("accept error");
            exit(EXIT_FAILURE);
        }
        printf("\033[32maccept connect: \033[36m%s:%d\033[0m\n", inet_ntoa(peeraddr.sin_addr), ntohs(peeraddr.sin_port));

        pid = fork();
        if(pid < 0){
            perror("fork error");
            exit(EXIT_FAILURE);
        }else if(pid == 0){
            close(listenfd);
            http_proxy(conn);
            exit(EXIT_SUCCESS);
        }else{
            close(conn);
        }
    }

    close(listenfd);
    return 0;
}

void http_proxy(int conn){
    char reqbuf[1024*8], resbuf[1024*8], _reqbuf[1024];
    int nbytes, req_len = 0;

    int remote_fd;
    if((remote_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));

    while(1){
        nbytes = recv(conn, _reqbuf, sizeof(_reqbuf), 0);
        for(int i=0; i<nbytes; i++, req_len++){
            reqbuf[req_len] = _reqbuf[i];
        }
        if(nbytes < 0){
            perror("recv error");
            exit(EXIT_FAILURE);
        }else if(nbytes == 0){
            printf("\033[31mclient close\033[0m\n");
            close(conn);
        }else{
            if(resolv_host(reqbuf, req_len, &remote_addr.sin_addr, &remote_addr.sin_port)){
                remote_addr.sin_family = AF_INET;
                break;
            }
        }
    }

    if(connect(remote_fd, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0){
        perror("connect error");
        exit(EXIT_FAILURE);
    }

    send(remote_fd, reqbuf, req_len, 0);

    while(1){
        nbytes = recv(remote_fd, resbuf, sizeof(resbuf), 0);
        if(nbytes < 0){
            perror("recv error");
            exit(EXIT_FAILURE);
        }else if(nbytes == 0){
            printf("\033[31mremote_server close\033[0m\n");
            close(remote_fd);
            break;
        }else{
            send(conn, resbuf, nbytes, 0);
        }
    }
}

bool resolv_host(char *req, int req_len, struct in_addr *addr, unsigned short int *port){
    req[req_len] = 0;

    char *_p = strstr(req, "Host: ");
    if(_p == NULL){
        return false;
    }

    char _host[128] = {0};
    for(char *chr=_p, *_p_host=_host; *chr!='\r'; chr++, _p_host++){
        *_p_host = *chr;
    }

    char *_p1 = strstr(_host, ": ");
    char _host1[128] = {0};
    strcpy(_host1, _p1+2);

    char *_p2 = strstr(_host1, ":");
    if(_p2 == NULL){
        *port = htons(80);
        struct hostent *resolv = gethostbyname(_host1);
        if(resolv == NULL){
            perror("gethostbyname error");
            exit(EXIT_FAILURE);
        }
        *addr = *(struct in_addr *)resolv->h_addr;
        return true;
    }else{
        char *_p3 = NULL, *_p4 = NULL;
        char _host2[128] = {0};
        char _port[6] = {0};
        for(_p3=_host1, _p4=_host2; *_p3!=':'; _p3++, _p4++){
            *_p4 = *_p3;
        }
        strcpy(_port, _p3+1);

        struct hostent *resolv = gethostbyname(_host2);
        if(resolv == NULL){
            perror("gethostbyname error");
            exit(EXIT_FAILURE);
        }
        *addr = *(struct in_addr *)resolv->h_addr;
        *port = htons(atoi(_port));
        return true;
    }
}
