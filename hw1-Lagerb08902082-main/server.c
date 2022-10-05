#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
    int id;
    int wait_for_write;  // used by handle_read to know if the header is read or not.
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_read_header = "ACCEPT_FROM_READ";
const char* accept_write_header = "ACCEPT_FROM_WRITE";
const char* ask_id = "Please enter the id (to check how many masks can be ordered):\n";
const char* locks = "Locked.\n";
const char* order_code = "Please enter the mask type (adult or children) and number of mask you would like to order:\n";
const char* order_fail = "Operation failed.\n";

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

typedef struct {
    int id; //customer id
    int adultMask;
    int childrenMask;
} Order;

int handle_read(request* reqP) {
    char buf[512];
    read(reqP->conn_fd, buf, sizeof(buf));
    memcpy(reqP->buf, buf, strlen(buf));
    return 0;
}
Order find_order(int id, int fd)
{
    Order order;
    off_t off_set = id * sizeof(Order);
    lseek(fd, off_set, SEEK_SET);
    read(fd, &order, sizeof(Order));
    // fprintf(stderr, "%d %d %d", order.id, order.adultMask, order.childrenMask);
    return order;
}
struct flock ptr(int id, int type)
{
    struct flock result;
    result.l_start = id * sizeof(Order);
    result.l_whence = SEEK_SET;
    result.l_type = type;
    result.l_len = sizeof(Order);
    return result;
} 
void pushing_order(int id, Order *order, int fd)
{
    off_t off_set = id * sizeof(Order);
    lseek(fd, off_set, SEEK_SET);
    write(fd, order, sizeof(Order));
    return;
}
int main(int argc, char** argv) {

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));
    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 5000;
    maxfd = 1024; // getdtablesize();
    
    fd_set listen;
    FD_ZERO(&listen);

    fd_set read_id;
    FD_ZERO(&read_id);
    fd_set ready_read_id;
    FD_ZERO(&ready_read_id);
    
    fd_set tell_;
    FD_ZERO(&tell_);
    fd_set ready_tell_;
    FD_ZERO(&ready_tell_);
    Order order[1024];

    fd_set order_read;
    FD_ZERO(&order_read);
    fd_set ready_order_read;
    FD_ZERO(&ready_order_read);
    int fd = open("preorderRecord", O_RDWR);
    while (1) {
    // TODO: Add IO multiplexing
    clilen = sizeof(cliaddr);
    FD_SET(svr.listen_fd, &listen);
    tv.tv_sec = 1;
    tv.tv_usec = 5000;
    if(select(maxfd + 1, &listen, NULL, NULL, &tv) > 0)
    {
        // add new connection
        conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
        if (conn_fd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;  // try again
            if (errno == ENFILE) {
                (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                continue;
            }
            ERR_EXIT("accept");
        }
        requestP[conn_fd].conn_fd = conn_fd;
        FD_SET(conn_fd, &read_id);
        strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
        fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
        write(conn_fd, ask_id, strlen(ask_id));
    }
    ready_read_id = read_id;
    int ret;
    if(select(maxfd + 1, &ready_read_id, NULL, NULL, &tv) > 0)
    {
        for(int i = 0; i < maxfd + 1; i++)
        {
            if(FD_ISSET(i, &ready_read_id))
            {
                ret = handle_read(&requestP[i]); // parse data from client to requestP[conn_fd].buf
                requestP[i].id = atoi(requestP[i].buf) - 902001;
                FD_SET(i, &tell_);
                FD_CLR(i, &read_id);
            }
        }
    }
    FD_ZERO(&ready_read_id);
    #ifdef READ_SERVER  
        ready_tell_ = tell_;
        if(select(maxfd + 1, NULL, &ready_tell_, NULL, &tv) > 0)
        {
            for(int i = 0; i < maxfd + 1; i++)
            {
                if(FD_ISSET(i, &ready_tell_))
                {
                    if(requestP[i].id < 0 || requestP[i].id > 20)
                    {
                        write(i, order_fail, strlen(order_fail));
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                        continue;
                    }
                    struct flock file_lock = ptr(requestP[i].id, F_WRLCK); 
                    if(fcntl(fd, F_SETLK, &file_lock) != -1)
                    {
                        order[i] = find_order(requestP[i].id, fd);
                        char mask_infor[100 * sizeof(Order)];
                        sprintf(mask_infor, "You can order %d adult mask(s) and %d children mask(s).\n", order[i].adultMask, order[i].childrenMask);
                        write(i, mask_infor, strlen(mask_infor)); 
                    }
                    else
                    {
                        write(i, locks, strlen(locks));
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                    }
                    FD_CLR(i, &tell_);
                    close(requestP[i].conn_fd);
                    free_request(&requestP[conn_fd]);
                }
            }
        }
        FD_ZERO(&ready_tell_);
    #else
        ready_tell_ = tell_;
        if(select(maxfd + 1, NULL, &ready_tell_, NULL, &tv) > 0)
        {
            for(int i = 0; i < maxfd + 1; i++)
            {
                if(FD_ISSET(i, &ready_tell_))
                {
                    if(requestP[i].id < 0 || requestP[i].id > 20)
                    {
                        write(i, order_fail, strlen(order_fail));
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                        continue;
                    }
                    struct flock file_lock = ptr(requestP[i].id, F_WRLCK); 
                    if(fcntl(fd, F_SETLK, &file_lock) != -1)
                    {
                        order[i] = find_order(requestP[i].id, fd);
                        char mask_infor[100 * sizeof(Order)];
                        sprintf(mask_infor, "You can order %d adult mask(s) and %d children mask(s).\n", order[i].adultMask, order[i].childrenMask);
                        write(i, mask_infor, strlen(mask_infor)); 
                        write(i, order_code, strlen(order_code));
                    }
                    else
                    {
                        write(i, locks, strlen(locks));
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                    }
                    FD_CLR(i, &tell_);
                    FD_SET(i, &order_read);
                }
            }
        }
        FD_ZERO(&ready_tell_);

        ready_order_read = order_read;
        if(select(maxfd + 1, &ready_order_read, NULL, NULL, &tv) > 0)
        {
            for(int i = 0; i < maxfd + 1; i++)
            {
                if(FD_ISSET(i, &ready_order_read))
                {
                    ret = handle_read(&requestP[i]);
                    char t[10];
                    int number;
                    sscanf(requestP[i].buf, "%s%d", t, &number);
                    // fprintf(stderr, "%s %d\n", t, number);
                    if(strcmp(t, "adult") == 0)
                    {
                        if(order[i].adultMask < number || number < 0)
                        {
                            write(i, order_fail, strlen(order_fail));
                        }
                        else
                        {
                            order[i].adultMask -= number;
                            struct flock file_lock = ptr(requestP[i].id, F_WRLCK); 
                            if(fcntl(fd, F_SETLK, &file_lock) != -1)
                            {
                                off_t off_set = requestP[i].id * sizeof(Order);
                                lseek(fd, off_set, SEEK_SET);
                                write(fd, &order[i], sizeof(Order));
                                char temp_char[100 * sizeof(Order)];
                                sprintf(temp_char, "Pre-order for %d successed, %d adult mask(s) ordered.\n", requestP[i].id + 902001, number);
                                write(requestP[i].conn_fd, temp_char, strlen(temp_char));
                                file_lock = ptr(requestP[i].id, F_UNLCK); 
                                fcntl(fd, F_SETLK, &file_lock);
                            }
                            else
                            {
                                write(i, locks, strlen(locks));
                            }
                        }
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                    }
                    else if(strcmp(t, "children") == 0)
                    {
                        if(order[i].childrenMask < number || number < 0)
                        {
                            write(i, order_fail, strlen(order_fail));
                        }
                        else
                        {
                            order[i].childrenMask  -= number;
                            struct flock file_lock = ptr(requestP[i].id, F_WRLCK); 
                            if(fcntl(fd, F_SETLK, &file_lock) != -1)
                            {

                                off_t off_set = requestP[i].id * sizeof(Order);
                                lseek(fd, off_set, SEEK_SET);
                                write(fd, &order[i], sizeof(Order));
                                
                                char temp_char[100 * sizeof(Order)];
                                sprintf(temp_char, "Pre-order for %d successed, %d children mask(s) ordered.\n", requestP[i].id + 902001, number);
                                write(requestP[i].conn_fd, temp_char, strlen(temp_char));
                                file_lock = ptr(requestP[i].id, F_UNLCK); 
                                fcntl(fd, F_SETLK, &file_lock);
                            }
                            else
                            {
                                write(i, locks, strlen(locks));
                            }
                            // close(fd);
                        }
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                    }
                    else
                    {
                        write(i, order_fail, strlen(order_fail));
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                    }
                    close(requestP[i].conn_fd);
                    free_request(&requestP[conn_fd]);
                }
            }
        }
        FD_ZERO(&ready_order_read);
    #endif
        if (ret < 0) {
            fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
            continue;
        }

    // TODO: handle requests from clients
// #ifdef READ_SERVER      
//         fprintf(stderr, "%s", requestP[conn_fd].buf);
//         sprintf(buf,"%s : %s",accept_read_header,requestP[conn_fd].buf);
//         write(requestP[conn_fd].conn_fd, buf, strlen(buf));    
// #else 
//         fprintf(stderr, "%s", requestP[conn_fd].buf);
//         sprintf(buf,"%s : %s",accept_write_header,requestP[conn_fd].buf);
//         write(requestP[conn_fd].conn_fd, buf, strlen(buf));    
// #endif

//         close(requestP[conn_fd].conn_fd);
//         free_request(&requestP[conn_fd]);
    }
    free(requestP);
    return 0;
}

// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->id = 0;
}

static void free_request(request* reqP) {
    /*if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }*/
    init_request(reqP);
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }

    // Get file descripter table size and initize request table
    maxfd = 1024; // getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}