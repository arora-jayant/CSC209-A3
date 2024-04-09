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
#include <time.h>

#ifndef PORT
    #define PORT 57521
#endif

#define MAX_BUF 128
# define SECONDS 10

struct client {
    int fd;
    int in_turn;
    struct in_addr ipaddr;
    char name[128];
    struct client *next;
    struct client *last_opponent;
    struct client *match_with;
    int hitpoints;
    int powermoves;
};

int login_user(int client_fd, struct client *p, struct client * head);
static struct client *addclient(struct client *top, int fd, struct in_addr addr);
static struct client *removeclient(struct client *top, int fd);
static void broadcast(struct client *top, char *s, int size);
int handleclient(struct client *p, struct client *top);
struct client * findclient(int fd, struct client * head);
int startmatch(struct client * p1, struct client * p2, struct client * head);
static void move_to_end(struct client *top, struct client *client);
int read_message(char * message, int fd);

int read_a_move(int fd);
int write_all(int fd, const char *buf, size_t count);


int bindandlisten(void);

int main(void) {
    srand(time(NULL));
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
            printf("connection from %s\n", inet_ntoa(q.sin_addr));
            
            head = addclient(head, clientfd, q.sin_addr);
        }

        for(i = 0; i <= maxfd; i++) {
            if (FD_ISSET(i, &rset)) {
                printf("yep, my fd is %d\n", i);
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
    int num_written;
    // matching user
    if ((p->name)[0] == '\0') { // creating new user
        printf("creating a new user!");
        if (login_user(p->fd, p, top) == 1) {
            printf("Login failed");
            return -1;
        }
    }
    struct client * c;
    printf("Current client is: %s\n", p->name);
    // if there are two players already in a match with each other
    if (p->match_with == NULL) {
        for (c = p; c != NULL; c = c->next) {
            // matching players
            if ((p->match_with == NULL && c->match_with == NULL) && (c->last_opponent != p && p->last_opponent != c) && p->fd != c->fd) {

                c->in_turn = 0;
                p->in_turn = 1;
                p->match_with = c;
                c->match_with = p;
                p->hitpoints = (rand() % 11) + 20;
                p->powermoves = (rand() % 3) + 1;
                c->hitpoints = (rand() % 11) + 20;
                c->powermoves = (rand() % 3) + 1;


                char outbuf[512];
                sprintf(outbuf, "Player (%s) will begin a match with player (%s)\n", p->name, c->name);
                broadcast(top, outbuf, strlen(outbuf));

                printf("reached here\n");
                sprintf(outbuf, "You have %d hitpoints\n You have %d powermoves\n Your opponent has %d hitpoints\r\n", p->hitpoints, p->powermoves, c->hitpoints);
                num_written = write_all(p->fd, outbuf, strlen(outbuf)); // writing the message
                if (num_written == -1) {
                    sprintf(outbuf, "(%s) has left\n", p->name);
                    broadcast(top, outbuf, strlen(outbuf));
                    return -1;
                }
                sprintf(outbuf, "Please enter your move (%s): \n(1) attack\n(2) powermove\n(3) item\r\n", p->name); // null terminator exists
                num_written = write_all(p->fd, outbuf, strlen(outbuf)); // writing the message
                if (num_written == -1) {
                    sprintf(outbuf, "(%s) has left\n", p->name);
                    broadcast(top, outbuf, strlen(outbuf));
                    return -1;
                }
                break;
            }
        }
    }

    // starting games between players
    if (p->match_with != NULL) {
        printf("Player is in a match -> %s with %s\n", p->name, p->match_with->name);
        int valid = startmatch(p, p->match_with, top);
        if (valid == -1) {
            return -1;
        }
    }
    return 0;
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

int login_user(int clientfd, struct client *p, struct client * head) {
    // login
    char name[128]; 
    char outbuf[512];

    // reading name
    int len = read(clientfd, name, sizeof(name) - 1); // reading the name the user sent
    name[len] = '\0';

    while (strstr(name, "\r\n") == NULL) {
        len += read(clientfd, &name[len], MAX_BUF-len); // fetching any additional reads
        name[len]='\0';
    }
    name[len-2] = '\0'; // replacing \r
    printf("name is : %s\n", name);
    if (len > 0) {
        sprintf(outbuf, "\r\n**%s joined the area.**\r\n", name);
        broadcast(head, outbuf, strlen(outbuf));
        sprintf(outbuf, "Thanks for joining (%s), Awaiting opponent...\r\n", name);
        write_all(clientfd, outbuf, strlen(outbuf));
        strcpy(head->name, name);
        return 0;
    } else if (len <= 0) {
        // socket is closed
        printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
        sprintf(outbuf, "(%s) has left\n", p->name);
        broadcast(head, outbuf, strlen(outbuf));
        return -1;
    } else {
        // printf("Should not reach here\n");
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

static void move_to_end(struct client *top, struct client *client) {
    struct client *rep;
    for(rep = top; rep != NULL; rep = rep->next){
        if(rep->next == client){
            rep->next = client->next;
            client->next = NULL;
        }
        if(rep->next == NULL){
            rep->next = client;
        }
    }
}

int write_all(int fd, const char *buf, size_t count) {
    ssize_t num_written_total = 0;
    ssize_t num_written;

    while (num_written_total < count) {
        num_written = write(fd, buf + num_written_total, count - num_written_total);
        if (num_written == -1) {
            return -1; // Error occurred
        }
        num_written_total += num_written;
    }

    return 0; // Success
}

int startmatch(struct client * p1, struct client * p2, struct client * head) {
    // printf("currently reached here with: %s\n", p1->name);
    int num_written;
    if (p1->in_turn % 2 == 1) {
        char outbuf[512];
        int move = read_a_move(p1->fd);

        if (move == 1) {
            int damage = (rand() % 5) + 2;
            p2->hitpoints = p2->hitpoints - damage;
            sprintf(outbuf, "Player (%s) attacks player (%s) for %d damage\r\n", p1->name, p2->name, damage);
            num_written = write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message
            if (num_written == -1) {
                sprintf(outbuf, "(%s) has left\n", p1->name);
                broadcast(head, outbuf, strlen(outbuf));
                return -1;
            }
            num_written = write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message
            if (num_written == -1) {
                sprintf(outbuf, "(%s) has left\n", p2->name);
                broadcast(head, outbuf, strlen(outbuf));
                return -1;
            }

            if (p2->hitpoints <= 0) {
                sprintf(outbuf, "Player (%s) wins!\r\n", p1->name);
                write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message

                sprintf(outbuf, "You scurry away... you stand no match for %s...\r\n", p1->name);
                write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message

                sprintf(outbuf, "Awaiting new opponent...\r\n");
                write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message

                sprintf(outbuf, "Awaiting new opponent...\r\n");
                write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message

                p1->match_with = NULL;
                p1->last_opponent = p2;
                p2->match_with = NULL;
                p2->match_with = p1;
                move_to_end(head, p1);
                move_to_end(head, p2);
            }
        } else if (move == 2) {
            if (p1->powermoves == 0) { // player doesn't have powermoves
                return startmatch(p1, p2, head); // calling function again, invalid input
            }
            int damage = ((rand() % 5) + 2) * 3;
            p1->powermoves = p1->powermoves - 1; // decrementing powermoves
            if (rand() % 2 == 0) {
                
                sprintf(outbuf, "Player (%s) misses the powermove!\r\n", p1->name);
                num_written = write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message
                if (num_written == -1) {
                    sprintf(outbuf, "(%s) has left\n", p1->name);
                    broadcast(head, outbuf, strlen(outbuf));
                    return -1;
                }
                num_written = write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message
                if (num_written == -1) {
                    sprintf(outbuf, "(%s) has left\n", p2->name);
                    broadcast(head, outbuf, strlen(outbuf));
                    return -1;
                }
            } else {
                p2->hitpoints = p2->hitpoints - damage;
                sprintf(outbuf, "Player (%s) uses a powermove for %d damage!\r\n", p1->name, damage);
                num_written = write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message
                if (num_written == -1) {
                    sprintf(outbuf, "(%s) has left\n", p1->name);
                    broadcast(head, outbuf, strlen(outbuf));
                    return -1;
                }
                num_written = write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message
                if (num_written == -1) {
                    sprintf(outbuf, "(%s) has left\n", p2->name);
                    broadcast(head, outbuf, strlen(outbuf));
                    return -1;
                }

                if (p2->hitpoints <= 0) {
                    sprintf(outbuf, "Player (%s) wins!\r\n", p1->name);
                    write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message

                    sprintf(outbuf, "You scurry away... you stand no match for %s...\r\n", p1->name);
                    write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message

                    sprintf(outbuf, "Awaiting new opponent...\r\n");
                    write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message

                    sprintf(outbuf, "Awaiting new opponent...\r\n");
                    write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message

                    p1->match_with = NULL;
                    p1->last_opponent = p2;
                    p2->match_with = NULL;
                    p2->match_with = p1;
                    move_to_end(head, p1);
                    move_to_end(head, p2);
                }
            }
        } else {
            char * message = NULL;
            num_written = read_message(message, p1->fd);
            if (num_written != -1) {
                sprintf(outbuf, "Player (%s) says: \n%s\r\n", p1->name, message);
                num_written = write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message
                if (num_written == -1) {
                    sprintf(outbuf, "(%s) has left\n", p1->name);
                    broadcast(head, outbuf, strlen(outbuf));
                    return -1;
                }
                num_written = write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message
                if (num_written == -1) {
                    sprintf(outbuf, "(%s) has left\n", p2->name);
                    broadcast(head, outbuf, strlen(outbuf));
                    return -1;
                }
            }
            sprintf(outbuf, "You have %d hitpoints\n You have %d powermoves\n Your opponent has %d hitpoints\r\n", p1->hitpoints, p1->powermoves, p2->hitpoints);
            num_written = write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message
            if (num_written == -1) {
                sprintf(outbuf, "(%s) has left\n", p1->name);
                broadcast(head, outbuf, strlen(outbuf));
                return -1;
            }
            if (p2->powermoves > 0) {
                sprintf(outbuf, "Please enter your move (%s): \n(1) attack\n(2) powermove\n(3) speak\r\n", p1->name); // null terminator exists
                int num_written = write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message
                if (num_written == -1) {
                    sprintf(outbuf, "(%s) has left\n", p1->name);
                    broadcast(head, outbuf, strlen(outbuf));
                    return -1;
                }
            } else {
                sprintf(outbuf, "Please enter your move (%s): \n(1) attack\n(3) speak\r\n", p1->name); // null terminator exists
                int num_written = write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message
                if (num_written == -1) {
                    sprintf(outbuf, "(%s) has left\n", p1->name);
                    broadcast(head, outbuf, strlen(outbuf));
                    return -1;
                }
            }
            return startmatch(p1, p2, head);
        }
        p1->in_turn = 0;
        p2->in_turn = 1;

        sprintf(outbuf, "You have %d hitpoints\n You have %d powermoves\n Your opponent has %d hitpoints\r\n", p2->hitpoints, p2->powermoves, p1->hitpoints);
        num_written = write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message
        if (num_written == -1) {
            sprintf(outbuf, "(%s) has left\n", p2->name);
            broadcast(head, outbuf, strlen(outbuf));
            return -1;
        }
        if (p2->powermoves > 0) {
            sprintf(outbuf, "Please enter your move (%s): \n(1) attack\n(2) powermove\n(3) speak\r\n", p2->name); // null terminator exists
            int num_written = write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message
            if (num_written == -1) {
                sprintf(outbuf, "(%s) has left\n", p2->name);
                broadcast(head, outbuf, strlen(outbuf));
                return -1;
            }
        } else {
            sprintf(outbuf, "Please enter your move (%s): \n(1) attack\n(3) speak\r\n", p2->name); // null terminator exists
            int num_written = write_all(p2->fd, outbuf, strlen(outbuf)); // writing the message
            if (num_written == -1) {
                sprintf(outbuf, "(%s) has left\n", p2->name);
                broadcast(head, outbuf, strlen(outbuf));
                return -1;
            }
        }
        sprintf(outbuf, "Awaiting opponent's move...\r\n"); // null terminator exists
        num_written = write_all(p1->fd, outbuf, strlen(outbuf)); // writing the message
        if (num_written == -1) {
            sprintf(outbuf, "(%s) has left\n", p1->name);
            broadcast(head, outbuf, strlen(outbuf));
            return -1;
        }

    } else {
        return -1;
    }
    return 0;
}

int read_message(char * message, int fd) {
    write_all(fd, "Speak now: ", 11);
    char line_buf[MAX_BUF];
    int num_chars = read(fd, line_buf, MAX_BUF);
    if (num_chars <= 0) {
        return -1;
    }
    line_buf[num_chars] = '\0';

    while (strstr(line_buf, "\r\n") == NULL) {
        num_chars += read(fd, &line_buf[num_chars], MAX_BUF-num_chars); // fetching any additional reads
        line_buf[num_chars]='\0';
    }
    line_buf[num_chars-2] = '\0'; // replacing \r
    strncpy(message, line_buf, num_chars);
    return 0;
}

int read_a_move(int fd) {
    int move = -1;

    while (move < 1 || move > 3) {
        char line_buf[MAX_BUF];
        int num_chars = read(fd, line_buf, MAX_BUF);
        if (num_chars <= 0) {
            return -1;
        }
        line_buf[num_chars] = '\0';

        while (strstr(line_buf, "\r\n") == NULL) {
            num_chars += read(fd, &line_buf[num_chars], MAX_BUF-num_chars); // fetching any additional reads
            line_buf[num_chars]='\0';
        }
        line_buf[num_chars-2] = '\0'; // replacing \r

        move = strtol(line_buf, NULL, 10);

        if (move < 1 || move > 3) {
            char *msg = "Your move is not allowed. Please enter a move between 1-3.\r\n";
            write(fd, msg, strlen(msg));
        }
    }
    return move;
}

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
    p->in_turn = 0;
    p->last_opponent = NULL;
    p->match_with = NULL;
    p->hitpoints = 0;
    p->powermoves = 0;
    (p->name)[0] = '\0';
    top = p;
    char outbuf[512];
    sprintf(outbuf, "What is your name?");
    write(p->fd, outbuf, strlen(outbuf)); // writing the message
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