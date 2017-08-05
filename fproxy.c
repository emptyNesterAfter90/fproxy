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
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

#define BUF_SIZE 8192
#define DOMAINNAME_LEN 128

bool resolv_host(char *req, int req_len, struct in_addr *addr, unsigned short int *port);
void http_proxy(int conn);

int main(void){
    signal(SIGCHLD, SIG_IGN);

    int listenfd;
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("\033[1;35m[ERROR]\033[0m create_proxy_fd");
        exit(EXIT_FAILURE);
    }

    int reuseaddr = 1;
    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) < 0){
        perror("\033[1;35m[ERROR]\033[0m setsockopt_on_proxy_fd");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(80);

    if(bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        perror("\033[1;35m[ERROR]\033[0m bind_address");
        exit(EXIT_FAILURE);
    }

    if(listen(listenfd, SOMAXCONN) < 0){
        perror("\033[1;35m[ERROR]\033[0m listen_proxy_fd");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in peeraddr;
    socklen_t peerlen = sizeof(peeraddr);
    int conn;
    pid_t pid;

    while(1){
        if((conn = accept(listenfd, (struct sockaddr *)&peeraddr, &peerlen)) < 0){
            perror("\033[1;35m[WARNING]\033[0m accept_connect");
            continue;
        }
        printf("\033[1;32m[INFO]\033[0m client connected: \033[36m%s:%d\033[0m\n", inet_ntoa(peeraddr.sin_addr), ntohs(peeraddr.sin_port));

        pid = fork();
        if(pid < 0){
            perror("\033[1;35m[ERROR]\033[0m fork");
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

bool resolv_host(char *req, int req_len, struct in_addr *addr, unsigned short int *port){
    req[req_len] = 0;

    char *_p = strstr(req, "Host: ");
    if(_p == NULL){
        return false;
    }

    char _host[DOMAINNAME_LEN] = {0};
    for(char *chr=_p, *_p_host=_host; *chr!='\r'; chr++, _p_host++){
        *_p_host = *chr;
    }

    char *_p1 = strstr(_host, ": ");
    char _host1[DOMAINNAME_LEN] = {0};
    strcpy(_host1, _p1+2);

    char *_p2 = strstr(_host1, ":");
    if(_p2 == NULL){
        struct hostent *resolv = gethostbyname(_host1);
        if(resolv == NULL){
            fprintf(stderr, "\033[1;35m[WARNING]\033[0m cannot resolv_hostname: \"%s\"\n", _host1);
            return false;
        }else{
            *addr = *(struct in_addr *)resolv->h_addr;
            *port = htons(80);
            return true;
        }
    }else{
        char *_p3 = NULL, *_p4 = NULL;
        char _host2[DOMAINNAME_LEN] = {0};
        char _port[6] = {0};
        for(_p3=_host1, _p4=_host2; *_p3!=':'; _p3++, _p4++){
            *_p4 = *_p3;
        }
        strcpy(_port, _p3+1);

        struct hostent *resolv = gethostbyname(_host2);
        if(resolv == NULL){
            fprintf(stderr, "\033[1;35m[WARNING]\033[0m cannot resolv_hostname: \"%s\"\n", _host1);
            return false;
        }else{
            *addr = *(struct in_addr *)resolv->h_addr;
            *port = htons(atoi(_port));
            return true;
        }
    }
}

void http_proxy(int conn){
    char reqbuf[BUF_SIZE], resbuf[BUF_SIZE];
    int reqlen, reslen;

    bool https_flg = false;

    int server_fd;
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("\033[1;35m[WARNING]\033[0m create_server_fd");
        return;
    }

    int keepalive = 1;
    if(setsockopt(conn, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0 || setsockopt(server_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0){
        perror("\033[1;35m[WARNING]\033[0m setsockopt_keepalive");
    }

    int idle = 30;
    if(setsockopt(conn, SOL_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) < 0 || setsockopt(server_fd, SOL_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) < 0){
        perror("\033[1;35m[WARNING]\033[0m setsockopt_keepidle");
    }

    int interval = 60;
    if(setsockopt(conn, SOL_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) < 0 || setsockopt(server_fd, SOL_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) < 0){
        perror("\033[1;35m[WARNING]\033[0m setsockopt_keepintvl");
    }

    int cnt = 3;
    if(setsockopt(conn, SOL_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)) < 0 || setsockopt(server_fd, SOL_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)) < 0){
        perror("\033[1;35m[WARNING]\033[0m setsockopt_keepcnt");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;

    reqlen = recv(conn, reqbuf, sizeof(reqbuf), 0);
    bool ret;
    if((ret = resolv_host(reqbuf, reqlen, &server_addr.sin_addr, &server_addr.sin_port)) != true){
        fprintf(stderr, "\033[1;35m[WARNING]\033[0m bad http_request_header\n");
        exit(EXIT_FAILURE);
    }else if(ret == true){
        reqbuf[reqlen] = 0;
        char *check_https_conn = strstr(reqbuf, "CONNECT ");
        if(check_https_conn != NULL){
            https_flg = true;
        }
    }

    if(connect(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("\033[1;35m[WARNING]\033[0m connect_to_server");
        exit(EXIT_FAILURE);
    }

    if(!https_flg){
        send(server_fd, reqbuf, reqlen, 0);
        while(1){
            reslen = recv(server_fd, resbuf, sizeof(resbuf), MSG_DONTWAIT);
            reqlen = recv(conn, reqbuf, sizeof(reqbuf), MSG_DONTWAIT);
            if(reslen > 0){
                send(conn, resbuf, reslen, 0);
            }else if(reqlen > 0){
                send(server_fd, reqbuf, reqlen, 0);
            }else if(reslen == 0){
                printf("\033[1;31m[INFO]\033[0m server_connect closed\n");
                shutdown(server_fd, SHUT_RDWR);
                close(server_fd);
                shutdown(conn, SHUT_RDWR);
                close(conn);
                return;
            }else if(reqlen == 0){
                printf("\033[1;31m[INFO]\033[0m client_connect closed\n");
                shutdown(conn, SHUT_RDWR);
                close(conn);
                shutdown(server_fd, SHUT_RDWR);
                close(server_fd);
                return;
            }else if((reqlen < 0 || reslen < 0) && (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)){
                continue;
            }else if(reqlen < 0 || reslen < 0){
                perror("\033[1;35m[WARNING]\033[0m recv_from_client/server");
                exit(EXIT_FAILURE);
            }
        }
    }else{
        char isconn_msg[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
        send(conn, isconn_msg, strlen(isconn_msg), 0);
        while(1){
            reqlen = recv(conn, reqbuf, sizeof(reqbuf), MSG_DONTWAIT);
            reslen = recv(server_fd, resbuf, sizeof(resbuf), MSG_DONTWAIT);
            if(reqlen > 0){
                send(server_fd, reqbuf, reqlen, 0);
            }else if(reslen > 0){
                send(conn, resbuf, reslen, 0);
            }else if(reqlen == 0){
                printf("\033[1;31m[INFO]\033[0m client_connect closed\n");
                shutdown(conn, SHUT_RDWR);
                close(conn);
                shutdown(server_fd, SHUT_RDWR);
                close(server_fd);
                return;
            }else if(reslen == 0){
                printf("\033[1;31m[INFO]\033[0m server_connect closed\n");
                shutdown(server_fd, SHUT_RDWR);
                close(server_fd);
                shutdown(conn, SHUT_RDWR);
                close(conn);
                return;
            }else if((reslen < 0 || reqlen < 0) && (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)){
                continue;
            }else if(reslen < 0 || reqlen < 0){
                perror("\033[1;35m[WARNING]\033[0m recv_from_client/server");
                exit(EXIT_FAILURE);
            }
        }
    }
}
