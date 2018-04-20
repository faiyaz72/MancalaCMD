#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* initial number of pebbles per pit */

int port = 52381;
int listenfd;

//Some constant prompt messages
char* WAIT_MESSAGE = "It is not your move. Please wait!\r\n";
char* REQUEST_MESSAGE = "Your move?\r\n";
char* ERROR_MOVE = "This is not a valid move\r\n";
char* ENDPIT_MESSAGE = "Last move ended in end pit, your move again\r\n";
char* GAME_TITLE = "Current Playing Status\r\n";

// Global flag variable to determine if a turn has been set
int turn_flag = 0;

struct player {
    int length; //stores the length of the client's name for buffer use
    int fd;
    int username_set_flag; //should be set to 0 for no name, 1 if 
                            //name is set
    char name[MAXNAME+1]; 
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits 
                        // pits[NPITS] is the end pit
    int puck;  // Boolean value to indicate whether this is a players turn or not
               // 0 indicates no turn, 1 indicates it is this player's turn
    struct player *next;
};
struct player *playerlist = NULL;

extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s);  /* you need to write this one */

/*
* Wrapper function to error check write function
*/
void Write(int fd, char* message, int size) {

    if (write(fd, message, size) == -1) {
        perror("write");
        exit(1);
    }
}



/*
* Simple helper function that checks whether player name exists 
* or not. Returns 1 if it exists or 0 if it doesn't exist 
*/
int check_name(char* name_to_check, int client_fd) {

    struct player* temp = playerlist;

    while(temp != NULL) {
        if (temp->fd != client_fd) {
            if ((strcmp(temp->name,name_to_check)) == 0) {
                return 1;
            }
        }
        temp = temp->next;
    }

    return 0;
}

/*
* Debugging helper function to print the contents of the 
* LinkedList
*/
void print_list() {
    struct player* temp = playerlist;
    while(temp != NULL) {
        printf("Name is %s and puck status is %d\n", temp->name, temp->puck);
        temp = temp->next; 
    }
}

/* 
* Creates a new player node with fd value of client_fd
* and sets each pit to pebbles value
*/
void add_list(int client_fd, int pebbles) {

    struct player *new = malloc(sizeof(struct player));
    new->fd = client_fd;
    new->username_set_flag = 0;
    new->name[0] = '\0';
    new->length = 0;

    for (int i = 0; i < NPITS; i++) {
        new->pits[i] = pebbles;
    }

    new->next = playerlist;
    playerlist = new;
}


/* 
* Removes client_fd from playerlist in the case of deleting a 
* player
*/
void remove_from_list(int client_fd) {

    struct player *prev = playerlist;

    if (playerlist->fd == client_fd) {

        if (playerlist->next == NULL) {
            playerlist = NULL;
            turn_flag = 0;
        }
        else {
            playerlist = playerlist->next;
            return;
        }

    } else {

        struct player *cur = prev->next;
        while(prev != NULL && cur != NULL) {
            if (cur->fd == client_fd) {
                prev->next = cur->next;
                return;
            }
            prev = cur;
            cur = cur->next;
        }

    }
}

/*
 * Search the first n characters of buf for a network newline (\r\n).
 * Return one plus the index of the '\n' of the first network newline,
 * or -1 if no network newline is found.
 */
int find_network_newline(const char *buf, int n) {

    for(int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            int result = 1 + i;
            return result;
        }
    }

    return -1;
} 

/* 
* Shows the game state to client_fd, used in conjuction with alert function
*/
void game_state(int client_fd) {

    struct player *temp = playerlist;

    Write(client_fd, GAME_TITLE, strlen(GAME_TITLE) + 1);
    while(temp != NULL) {

        char state[MAXMESSAGE];

        if (temp->username_set_flag == 1) {
            sprintf(state, "%s: [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d  [end pit]%d\r\n", temp->name,temp->pits[0],
            temp->pits[1], temp->pits[2], temp->pits[3], temp->pits[4], temp->pits[5],temp->pits[6]);
            Write(client_fd, state, strlen(state) + 1);
        }
        temp = temp->next;
    }

}

/*
* Shows the game state to all clients connected to the server
*/
void alert() {

    struct player* temp = playerlist;

    while (temp != NULL) {
        game_state(temp->fd);
        temp = temp->next;
    }
}

/*
* A buffer to read the name of clients. Returns 3 if name entered is
* too long. Returns 2 if client disconnected
* before full name could be entered. Returns 1 if client has not yet
* entered full name. Returns 0 if full name of client is entered
* Full name is then added to client's player node in playerlist
*/
int buffer(struct player* client) {

    char buf[MAXNAME];

    int num_bytes;
    int where;

    num_bytes = read(client->fd, buf, MAXNAME);
    client->length = client->length + num_bytes;

    if (client->length == MAXNAME) {
        return 3;
    }

    //If the user disconnects before entering their name
    if (num_bytes == 0) {
        return 2;
    }

    buf[num_bytes] = '\0';


    strcat(client->name, buf);

    if ((where = find_network_newline(buf, num_bytes)) > 0) {
        client->name[strlen(client->name) - 1] = '\0';
        return 0;
    } else {
        return 1;
    }
}

/*
* Helper Function which initializes name of client and prompts client
* if name is invalid or is already taken. Calls to buffer function for 
* buffering of name
*/
int initialize_name(struct player* client) {   

    int client_fd = client->fd;
    char* name_taken = "Name already taken or invalid, try again!\r\n";
    int buf = buffer(client);

    //client has disconnected before entered complete name
    if (buf == 2) {
        remove_from_list(client_fd);
        close(client_fd);
        printf("client %d disconnected before name entered\n", client_fd);
        return 0;
    }

    //client has not yet fully entered name,
    else if (buf == 1) {
        return 1;
    }

    //client entered a long name
    else if (buf == 3) {
        Write(client_fd, name_taken, strlen(name_taken) + 1);
        remove_from_list(client_fd);
        close(client_fd);
        printf("client %d entered a long name\n", client_fd);
        return 0;
    }

    //client has fully entered name and name is valid
    else if (check_name(client->name, client_fd) == 0) {

        if (turn_flag == 0) {
            client->puck = 1;
            turn_flag = 1;
        } else {
            client->puck = 0;
        }

        client->username_set_flag = 1;
        char msg[MAXMESSAGE];
        printf("Cient fd %d is now username %s\n", client_fd, client->name);
        sprintf(msg, "%s has joined the game\r\n", client->name);
        broadcast(msg);
        alert();

    //name entered by client is not valid and client must be kicked out of server
    } else {
        Write(client_fd, name_taken, strlen(name_taken) + 1);
        remove_from_list(client_fd);
        close(client_fd);
        printf("client %d disconnected before name entered\n", client_fd);
        return 0;
    }
    return 1;
}

/*
* Helper function which gets the next player node in playerlist. If next is null 
* it returns the head of the playerlist
*/
struct player* get_next(struct player* client) {

    if (client->next == NULL) {
        return playerlist;
    } else {
        return client->next;
    }
}

/*
* Helper function which passes turn to the next player, and 
* requests them to enter a valid move. Designed to find player
* with puck value 1, set that puck value to 0 and request the 
* next player in circle for a move
*/
void pass_puck() {

    struct player* temp = playerlist;

    while(temp != NULL) {

        if (temp->puck == 1) {
            temp->puck = 0;
            temp = get_next(temp);

            if (temp->username_set_flag == 1) {   //Checking if username has been set
                    temp->puck = 1;
                    Write(temp->fd, REQUEST_MESSAGE, strlen(REQUEST_MESSAGE) + 1);
                    return;

            } else { //otherwise loop through to get the next player with a set username.

                temp = get_next(temp);

                while(temp->username_set_flag != 1) {
                    temp = get_next(temp);
                }

                // Request player to enter a move
                temp->puck = 1;
                Write(temp->fd, REQUEST_MESSAGE, strlen(REQUEST_MESSAGE) + 1);
                return;
            }
        }  
            temp = get_next(temp);
    }
}


/* 
* Error check function which returns 1 if there are errors on pit_num
* or 0 if error_check passed with no errors
*/
int error_check(int pit_num, struct player* client) {

    if (pit_num > 5) {
        return 1;

    } else if (client->pits[pit_num] == 0) {
        return 1;

    } else if (pit_num < 0) {
        return 1;

    }
    return 0;
}

/*
* Executes move from client at pit_num, 
*/
void do_move(int pit_num, struct player* client) {

    int track = client->pits[pit_num]; //Keeping a track of original # of pebbles
    client->pits[pit_num] = 0;

    for (int i = pit_num + 1; i < NPITS + 1; i++) {

        if (track != 0) {
            client->pits[i] = client->pits[i] + 1;
            track = track - 1;
        }

        else {
            return;
        }
    }

    //If track != 0, spill pebbles to next player in circle
    if (track != 0) {

        struct player* temp = get_next(client);
        if (temp == NULL) {
            temp = get_next(temp);
        }

        while(track != 0) {

            if (temp->username_set_flag == 1) {

                int limit; //Setting upper limit of pit, only original player can put 
                           // in end pit.
                if (temp->fd == client->fd) {
                    limit = NPITS + 1;
                } else {
                    limit = NPITS;
                }

                for (int j = 0; j < limit; j++) {
                    if (track != 0) {
                        temp->pits[j] = temp->pits[j] + 1;
                        track = track - 1;
                    } else {
                        return;
                    }
                }
                temp = get_next(temp);
            }

            // Finding next player with a set username 
            while(temp->username_set_flag != 1) {
                temp = get_next(temp);
            }
        }
    }

    else {
        return;
    }
}

/*
* Essential helper function which reads data from clients and does appropriate 
* actions as follows:
* 1) If a player disconnects, removes them for playerlist, passes turn to next player
* 2) If it is not the client's turn, ask's the client to wait
* 3) If it is the client's turn, executes do_move 
* 4) If client ends move in end_pit, asks for another move
* 5) Error checks all moves 
* 6) Passes turn to next player
*/
int read_from(struct player* client, int puck_fd) {

    char buf[MAXNAME + 1];
    int cur_pebbles;
    struct player* next_player = get_next(client);
    while(next_player->username_set_flag != 1) {
        next_player = get_next(next_player);
    }

    char* next_player_name = next_player->name;

    char next_msg[MAXMESSAGE];

    sprintf(next_msg, "It is %s's turn\r\n", next_player_name);

    int num_bytes = read(client->fd, &buf, MAXNAME);
    buf[num_bytes] = '\0';

    if (num_bytes == 0) {

        if (client->puck == 1) {
            pass_puck();
            broadcast(next_msg);
            remove_from_list(client->fd);
        }

        else {
            remove_from_list(client->fd);
        }

        return client->fd;
    }

    if (client->fd != puck_fd) {

        Write(client->fd, WAIT_MESSAGE, strlen(WAIT_MESSAGE) + 1);
    } 

    else {

        int num_read = strtol(buf, NULL, 10);
        error_check(num_read, client);

        if ((error_check(num_read, client)) == 0) {
            cur_pebbles = client->pits[num_read];
            printf("error check passed\n");
            do_move(num_read, client);
            alert();

            char msg[MAXMESSAGE];
            sprintf(msg, "%s moved from pit %d\r\n", client->name, num_read);
            printf("%s", msg);
            broadcast(msg);

            if (cur_pebbles + num_read == NPITS) { 
                char re_turn[MAXMESSAGE];
                sprintf(re_turn, "%s ended last move in endpit\r\n", client->name);
                broadcast(re_turn);
                Write(client->fd, ENDPIT_MESSAGE, strlen(ENDPIT_MESSAGE) + 1);
                printf("Player %s ended last move in end pit\n", client->name);
            }

            else {
                broadcast(next_msg);
                pass_puck();
            }

        } else {
            printf("error check failed\n");
            Write(client->fd, ERROR_MOVE, strlen(ERROR_MOVE) + 1);
        }
        
        
    }
    return 0;
}

/*
* Helper function which returns a the playerlist node next 
* in line for turn
*/
struct player* client_turn() {

    struct player* temp = playerlist;
    while(temp != NULL) {
        if (temp->puck == 1) {
            return temp;
        }
        temp = temp->next;
    }
    return 0; //shouldn't come here, guranteed to have one puck holder
}


int main(int argc, char **argv) {
    char msg[MAXMESSAGE];

    parseargs(argc, argv);
    makelistener();


    int max_fd = listenfd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);

    struct player* my_turn;

    while (!game_is_over()) {

        fd_set listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }

        //Incoming new client that want to connect
        if (FD_ISSET(listenfd, &listen_fds)) {

            int client_fd = accept(listenfd, NULL, NULL);
            if (client_fd < 0) {
                perror("server: accept");
                close(listenfd);
                exit(1);
            }

            int pebbles = compute_average_pebbles();
            add_list(client_fd, pebbles);

            char* to_ask = "Welcome to Mancala. What is your name?\r\n";
            Write(client_fd, to_ask, strlen(to_ask) + 1);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }

            FD_SET(client_fd, &all_fds);
            printf("Accepted connection from client %d\n", client_fd);
        }

        struct player* temp = playerlist;
        while(temp != NULL) {

            if (FD_ISSET(temp->fd, &listen_fds)) {

                if (temp->username_set_flag == 0) {
                    int fd = temp->fd;
                    int remove = initialize_name(temp);
                    //Player has disconnected before even entering their name
                    if (remove == 0) {
                        FD_CLR(fd, &all_fds);
                        break;
                    }

                    if (temp->puck == 1) {
                        my_turn = client_turn();
                        Write(temp->fd, REQUEST_MESSAGE, strlen(REQUEST_MESSAGE) + 1);
                    }
                }

                else {

                    int client_closed = read_from(temp, my_turn->fd);

                    if (client_closed > 0) {

                        FD_CLR(client_closed, &all_fds);
                        printf("Username %s disconnected\n", temp->name);
                        char msg[MAXMESSAGE];;
                        sprintf(msg, "%s has left the game\r\n", temp->name);
                        broadcast(msg);
                        alert();
                    }   
                }
            }

            temp = temp->next;
        }

        my_turn = client_turn();
    }

    broadcast("Game over!\r\n");
    printf("Game over!\n");

    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }

        printf("%s has %d points\r\n", p->name, points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg);
    }

    return 0;
}
// All functions below were given in starter code so no documentations were added
// With the exception for broadcast function.

void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);  
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}


void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}


/* call this BEFORE linking the new player in to the list */
int compute_average_pebbles() { 
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


int game_is_over() { /* boolean */
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}

/*
* Helper function that broadcasts a message to all clients 
* connected
*/
void broadcast(char* message) {

    struct player* temp = playerlist;
    while (temp != NULL) {
        Write(temp->fd, message, strlen(message) + 1);
        temp = temp->next;
    }

}