#ifndef ASS4_2310DEPOT_H
#define ASS4_2310DEPOT_H

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>

#define BUFFER_SIZE 255
#define MIN_ARGS 2
#define CONNECTION_LIMIT 10
#define NOT_COMPLETED 0
#define COMPLETED 1
#define NOT_STARTED 2
#define REMOVED 3
#define NO_FLAGS 0
#define NEW_CONNECTION 1
#define LISTENING_THREAD 1
#define READING_THREAD 2

// enum for status of program on exit
typedef enum {
    OK = 0,
    BADARGNUM = 1,
    BADNAME = 2,
    BADQUANTITY = 3
} Status;

// FILE* streams for read & write to other connections
typedef struct {
    FILE* read;
    FILE* write;
} Streams;

// stores the data for a resource
typedef struct {
    char* name;
    int quantity;
} Resource;

// stores the data for the current depot (self)
typedef struct {
    char* name;
    int port;
    Resource* resources;
    int count;
    pthread_mutex_t lock;
} Depot;

// stores data for another depot / neighbour
typedef struct {
    char* name;
    int port;
    Streams* streams;
} OtherDepot;

// stores the data for all neighbours
typedef struct {
    OtherDepot* neighbours;
    int len;
    pthread_mutex_t lock;
} NeighbourList;

// instruction queue for defer'd instructions
typedef struct {
    char** values;
    int len;
    pthread_mutex_t lock;
} Queue;

// args struct for passing to get_instruction through pthread_create()
typedef struct {
    Streams* streams;
    Queue* queue;
    Streams* result;
} GetInstructionArgs;

// args struct for passing to listen_for_connections through pthread_create()
typedef struct {
    struct addrinfo** comAddr;
    int sockfd;
    Streams* result;
} Listen4ConnectionArgs;

// base struct data for one thread
typedef struct {
    pthread_t threadID;
    pthread_mutex_t lock;
    int threadCompleted;
    int type; // reading or listening (listed below)
} ThreadData;

// expantion of ThreadData for threads that listen for connections
typedef struct {
    ThreadData threadData;
    Listen4ConnectionArgs* args;
} Listening;

// expansion of ThreadData for threads that read instructions
typedef struct {
    ThreadData threadData;
    GetInstructionArgs* args;
    int flag;
} Reading;

/**
 * Creates a deopt with default values. This is to ensure append_resource does
 * not cause a segmentation fault.
 *
 * @return  depot with default values
 */
Depot default_depot();

/**
 * Creates a queue with default values.
 *
 * @return  queue with default values
 */
Queue default_queue();

/**
 * Creates a neighbourList with default values.
 *
 * @return  neighbourList with default values
 */
NeighbourList default_neighbour_list();

/**
 * swaps the values of the two given void*
 *
 * @param pointer one
 * @param pointer two
 */
void swap(void* a, void* b, int size);

/**
 * Sorts resources in list by .name using a bubble algorithm.
 *
 * @param list  list to sort
 * @param count number of entries in list
 */
void bubble_sort_resources(Resource* list, int count);

/**
 * Sorts neighbours in list by .name using a bubble algorithm.
 *
 * @param list  list to sort
 * @param count number of entries in list
 */
void bubble_sort_neighbours(OtherDepot* list, int count);

/**
 * Thread-safe queue push. Uses mutex lock.
 *
 * @param value string to push to queue
 * @param queue queue to push string to
 */
void queue_push(char* value, Queue* queue);

/**
 * Thread-safe queue pop. Uses mutex lock
 *
 * @param queue queue to pop value from
 * @return      value popped from queue
 */
char* queue_pop(Queue* queue);

/**
 * Loads commandline arguments into a depot struct.
 *
 * @param argc  number of commandline arguments
 * @param argv  commandline argument values
 * @param depot pointer to depot struct to load values into
 */
void init_depot(int argc, char** argv, Depot* depot);

/**
 * initialises global variables/functions
 */
void init_signal_handling();

/**
 * Creates and connects the socket for the emphemeral port (command port) and
 * waits for a connect to it. The connection is returned as an open file which
 * can only be read from.
 * Takes an empty struct addrinfo as an argument
 *
 * @param ephai addrinfo that data from the connection will be loaded to
 * @return      opened file that can be read from
 */
int init_command_port(struct addrinfo** comAddr);

/**
 * Waits for an IM instruction for a given stream
 *
 * @param args  void pointer to a GetInstructionArgs struct
 * @return      returns nothing. However, writes data into given struct
 */
void* wait_for_IM(void* args);

/**
 * Reads instructions from the FILE* stream given int he GetInstructionArgs
 * passed to the function through the (void*)
 *
 * @param args  void pointer to a GetInstructionArgs struct
 * @return      returns nothing. However, writes data into given struct
 */
void* get_instruction(void* args);

/**
 * Listens for a connection on the socket given in the Listen4Args struct
 *
 * @param args  void pointer to a Listen4Args struct
 * @return      returns nothing. However, writes data into given struct
 */
void* listen_for_connection(void* args);

/**
 * Attempts to connect to a depot for the given port
 *
 * @param port  port to connect to
 * @return      NULL, or {"NEW DEPORT", sockfd of accepted connection}
 */
char** connect_to_depot(char* port);

/**
 * Sends IM message to given FILE* stream.
 *
 * @param port      port of self
 * @param stream    stream to send IM to
 */
void send_IM(uint16_t port, FILE* stream);

/**
 * Appends a new resource onto the list of resources contained in the Depot
 * struct.
 * The depot struct MUST ALREADY BE ALLOCATED IN MEMORY.
 *
 * @param newResource   resources to append to depot
 * @param depot         depot containing the list
 */
void append_resource(Resource resource, Depot* depot);

/**
 * Handles instruction received from other depot
 *
 * @param message   message received
 * @param queue     instruction queue to push defer'd instruction to
 * @return          char array of information for get_instruction to follow
 *                  up on instruction correctly
 */
char** handle_instruction(char* message, Queue* queue);

/**
 * Modifies the depot resrouces
 *
 * @param action    type of operation to make on resources
 * @param quantity  quantity of stock to handle
 * @param type      the resource to manipulate
 * @param dest      destination for transfer instruction
 *                  or NULL if not transfer
 * @return          NULL, or {dest, message for other depot}
 */
char** modify_stock(char* action, char* quantity, char* type, char* dest);

int main(int argc, char** argv);

/**
 * prints depot to stdout
 */
void print_depot();

/**
 * prints neighbours to stdout
 */
void print_neighbours();

/**
 * Handler for SIGHUP signal
 *
 * @param sig   signal number
 */
static void handle_sighup(int sig);

/**
 * End function due to error. Prints corresponding error message to stderr
 *
 * @param s status on exit; error type
 */
void quit_on_error(Status s);

/**
 * Checks if the given name conforms to the requirements and returns a bool
 * accordingly.
 *  Name cannot be empty.
 *  Name cannot contain; spaces, '\n', '\r', colons.
 *
 * @param name  name to be checked
 * @return      bool; true if name is valid, false if not
 */
bool valid_name(char* name);

/**
 * Checks if the given quantity conforms to the requirements and returns a bool
 * accordingly.
 *  Quantity must be a number.
 *  Quantity must be greater than 0.
 *
 * @param quantity  quantity to the checked
 * @return          bool; true if the quantity is valid, false otherwise.
 */
bool valid_quantity(char* quantity);

/**
 * Checks if a message received from another port is a valid message. Used to
 * remove cases that would cause handle_instruction to error.
 *
 * @param message   message received from other depot
 * @return          bool; true if the message is okay, false otherwise
 */
bool valid_message(char* message);

#endif //ASS4_2310DEPOT_H