/*
 * socket demonstrations:
 * This is the server side of an "internet domain" socket connection, for
 * communicating over the network.
 *
 * In this case we are willing to wait for chatter from the client
 * _or_ for a new connection.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef PORT
    #define PORT 57521
#endif

# define SECONDS 10

struct client {
    int fd;
    struct in_addr ipaddr;
    char name[256];
    struct client *next;
    int in_match;
    struct client *last_opponent;
};

int login_user(int client_fd, struct client *p, struct client * head, struct sockaddr_in q);
static struct client *addclient(struct client *top, int fd, struct in_addr addr);
static struct client *removeclient(struct client *top, int fd);
static void broadcast(struct client *top, char *s, int size);
int handleclient(struct client *p, struct client *top);
struct client * findclient(int fd, struct client * head);


int bindandlisten(void);

int main(void) {
    int clientfd, maxfd, nready;
    struct client *p;
    struct client *head = NULL;
    socklen_t len;
    struct sockaddr_in q;
    struct timeval tv;
    fd_set allset;
    fd_set rset;

    int i;


    int listenfd = bindandlisten();
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;

    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;
        /* timeout in seconds (You may not need to use a timeout for
        * your assignment)*/
        tv.tv_sec = SECONDS;
        tv.tv_usec = 0;  /* and microseconds */

        nready = select(maxfd + 1, &rset, NULL, NULL, &tv);
        if (nready == 0) {
            // printf("No response from clients in %d seconds\n", SECONDS);
            continue;
        }

        if (nready == -1) {
            perror("select");
            continue;
        }

        if (FD_ISSET(listenfd, &rset)){
            printf("a new client is connecting\n");
            len = sizeof(q);
            if ((clientfd = accept(listenfd, (struct sockaddr *)&q, &len)) < 0) {
                perror("accept");
                exit(1);
            }
            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }

            head = addclient(head, clientfd, q.sin_addr);
            if (login_user(clientfd, p, head, q) == 1) {
                printf("Login failed");
            }

            // matching user
            // 1. if someone new joins the game, match them with another person that is ready, or block until someone arrives
            struct client * p1 = findclient(clientfd, head);
            for (p = head; p != NULL; p = p->next) {
                if (FD_ISSET(p->fd, &allset)) {
                    if ((!p->in_match && !p1->in_match) && (p1->last_opponent != p && p->last_opponent != p1) && p->fd != clientfd ) {
                        
                        struct client * p2 = p;

                        p1->in_match = 1;
                        p2->in_match = 1;
                        p1->last_opponent = p2;
                        p2->last_opponent = p1;
                        
                        printf("Player %s will begin a match with player %s\n", p1->name, p2->name);
                    }
                }
            }
        }

        for(i = 0; i <= maxfd; i++) {
            if (FD_ISSET(i, &rset)) {
                // printf("yep, i am in the set %d\n", i);
                for (p = head; p != NULL; p = p->next) {
                    if (p->fd == i) {
                        int result = handleclient(p, head);
                        if (result == -1) {
                            int tmp_fd = p->fd;
                            head = removeclient(head, p->fd);
                            FD_CLR(tmp_fd, &allset);
                            close(tmp_fd);
                        }
                        break;
                    }
                }
            }
        }
    }
    return 0;
}

int handleclient(struct client *p, struct client *top) {
    char buf[256];
    char outbuf[512];
    int len = read(p->fd, buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        printf("Received %d bytes: %s", len, buf);
        sprintf(outbuf, "%s says: %s", inet_ntoa(p->ipaddr), buf);
        broadcast(top, outbuf, strlen(outbuf));
        return 0;
    } else if (len <= 0) {
        // socket is closed
        printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
        sprintf(outbuf, "Goodbye %s\r\n", inet_ntoa(p->ipaddr));
        broadcast(top, outbuf, strlen(outbuf));
        return -1;
    } else {
        printf("Should not reach here.\n");
        return -1;
    }
}

 /* bind and listen, abort on error
  * returns FD of listening socket
  */
int bindandlisten(void) {
    struct sockaddr_in r;
    int listenfd;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    int yes = 1;
    if ((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
        perror("setsockopt");
    }
    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(PORT);

    if (bind(listenfd, (struct sockaddr *)&r, sizeof r)) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
    return listenfd;
}

int login_user(int clientfd, struct client *p, struct client * head, struct sockaddr_in q) {
    // login
    write(clientfd, "What is your name?", 18); // writing the message
    char name[256]; 
    char outbuf[512];
    int len = read(clientfd, name, sizeof(name) - 1); // reading the name the user sent
    name[len] = '\0';
    
    if (len > 0) {
        char * end = strstr(name, "\n");
        if (end != NULL) {
            end[0] = '\0';
        } else {
            return 1;
        }
        sprintf(outbuf, "(%s) has joined the game\r\n", name);
        broadcast(head, outbuf, strlen(outbuf));
        strcpy(head->name, name);
        head->in_match = 0; 
        head->last_opponent = NULL;
        return 0;
    } else if (len <= 0) {
        // socket is closed
        printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
        sprintf(outbuf, "(%s) has left\r\n", p->name);
        broadcast(head, outbuf, strlen(outbuf));
        return -1;
    } else {
        return -1; // shouldnt reach here
    }
}

struct client * findclient(int fd, struct client * head) {
    struct client *p;
    for (p = head; p != NULL; p = p->next) {
        if (p->fd == fd) {
            return p;
        }
    }
    return NULL;
}

// static int can_be_matched(struct client *client1, struct client *client2) {
//     // Check if either client is already in a match
//     if (client1->in_match || client2->in_match)
//         return 0;
    
//     // Check if they were last opponents
//     if (client1->last_opponent == client2 && client2->last_opponent == client1)
//         return 0;
    
//     return 1;  // Clients can be matched
// }

// static void move_to_end(struct client **top, struct client *client) {
//     struct client **p;
//     for (p = top; *p && (*p)->next; p = &(*p)->next);
//     if (*p) {
//         (*p)->next = client;
//         client->next = NULL;
//     }
// }

static struct client *addclient(struct client *top, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));
    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));

    p->fd = fd;
    p->ipaddr = addr;
    p->next = top;
    top = p;
    return top;
}


static struct client *removeclient(struct client *top, int fd) {
    struct client **p;

    for (p = &top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        free(*p);
        *p = t;
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                 fd);
    }
    return top;
}



static void broadcast(struct client *top, char *s, int size) {
    struct client *p;
    for (p = top; p; p = p->next) {
        write(p->fd, s, size);
    }
    /* should probably check write() return value and perhaps remove client */
}