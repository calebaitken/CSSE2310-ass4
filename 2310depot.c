// @author  Caleb Aitken

#include "2310depot.h"

// global depot struct, contains resources and depot name.
// this is a global because of SIGHUP
Depot depot;

// global depot list of neighbouring depots / connected depots
// this global because of SIGHUP again :'(
NeighbourList otherDepots;

/**
 * Creates a deopt with default values. This is to ensure append_resource does
 * not cause a segmentation fault.
 *
 * @return  depot with default values
 */
Depot default_depot() {
    Depot defaultDepot;
    defaultDepot.name = "";
    defaultDepot.count = 0;
    defaultDepot.resources = calloc(0, sizeof(Resource));
    pthread_mutex_init(&defaultDepot.lock, NULL);
    return defaultDepot;
}

/**
 * Creates a queue with default values.
 *
 * @return  queue with default values
 */
Queue default_queue() {
    Queue defaultQueue;
    defaultQueue.len = 0;
    defaultQueue.values = calloc(0, sizeof(char*));
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&defaultQueue.lock, &attr);
    return defaultQueue;
}

/**
 * Creates a neighbourList with default values.
 *
 * @return  neighbourList with default values
 */
NeighbourList default_neighbour_list() {
    NeighbourList defaultList;
    defaultList.len = 0;
    defaultList.neighbours = calloc(CONNECTION_LIMIT, sizeof(OtherDepot));
    for (int i = 0; i < CONNECTION_LIMIT; i++) {
        defaultList.neighbours[i].streams = malloc(sizeof(Streams));
    }
    pthread_mutex_init(&defaultList.lock, NULL);
    return defaultList;
}

/**
 * swaps the values of the two given void*
 *
 * @param pointer one
 * @param pointer two
 */
void swap(void* a, void* b, int size) {
    unsigned char* p = a;
    unsigned char* q = b;
    unsigned char tmp;

    for (int i = 0; i != size; ++i) {
        tmp = p[i];
        p[i] = q[i];
        q[i] = tmp;
    }
}

/**
 * Sorts resources in list by .name using a bubble algorithm.
 *
 * @param list  list to sort
 * @param count number of entries in list
 */
void bubble_sort_resources(Resource* list, int count) {
    int x = -1;
    bool skipToEnd;

    do {
        skipToEnd = true;
        x += 1;
        for (int i = 1; i < (count - x); i++) {
            if (strcmp(list[i - 1].name, list[i].name) > 0) {
                swap(&list[i - 1], &list[i], sizeof(Resource));
                skipToEnd = false;
            }
        }
    } while (skipToEnd == false);
}

/**
 * Sorts neighbours in list by .name using a bubble algorithm.
 *
 * @param list  list to sort
 * @param count number of entries in list
 */
void bubble_sort_neighbours(OtherDepot* list, int count) {
    int x = -1;
    bool skipToEnd;

    do {
        skipToEnd = true;
        x += 1;
        for (int i = 1; i < (count - x); i++) {
            if (strcmp(list[i - 1].name, list[i].name) > 0) {
                swap(&list[i - 1], &list[i], sizeof(OtherDepot));
                skipToEnd = false;
            }
        }
    } while (skipToEnd == false);
}

/**
 * Thread-safe queue push. Uses mutex lock.
 *
 * @param value string to push to queue
 * @param queue queue to push string to
 */
void queue_push(char* value, Queue* queue) {
    pthread_mutex_lock(&queue->lock);
    char** newValues;

    newValues = calloc(queue->len + 1, sizeof(char*));

    newValues[0] = strdup(value);

    for (int i = 1; i < queue->len + 1; i++) {
        newValues[i] = strdup(queue->values[i - 1]);
    }

    queue->values = malloc(0);
    free(queue->values);

    queue->values = newValues;
    queue->len += 1;
    pthread_mutex_unlock(&queue->lock);
}

/**
 * Thread-safe queue pop. Uses mutex lock
 *
 * @param queue queue to pop value from
 * @return      value popped from queue
 */
char* queue_pop(Queue* queue) {
    pthread_mutex_lock(&queue->lock);

    if (queue->len == 0) {
        return NULL;
    }

    queue->len -= 1;

    pthread_mutex_unlock(&queue->lock);

    return queue->values[queue->len];
}

/**
 * initialises global variables/functions
 */
void init_signal_handling() {
    struct sigaction saHangup;
    saHangup.sa_handler = handle_sighup;
    saHangup.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &saHangup, NULL);

    signal(SIGPIPE, SIG_IGN);
}

/**
 * Loads commandline arguments into a depot struct.
 *
 * @param argc  number of commandline arguments
 * @param argv  commandline argument values
 * @param depot pointer to depot struct to load values into
 */
void init_depot(int argc, char** argv, Depot* depot) {
    // check for appropriate number of args
    if (argc < MIN_ARGS || (argc - MIN_ARGS) % 2 != 0) {
        quit_on_error(BADARGNUM);
    }

    // check depot name is legal
    if (!valid_name(argv[1])) {
        quit_on_error(BADNAME);
    }

    depot->name = argv[1];

    // creates and appends each of the resources
    // listed in the commandline arguments
    for (int i = 0; i < (argc - 2) / 2; i++) {
        Resource newResource;
        if (!valid_name(argv[(i * 2) + 2])) {
            quit_on_error(BADNAME);
        }

        newResource.name = argv[(i * 2) + 2];

        if (!valid_quantity(argv[(i * 2) + 3])) {
            quit_on_error(BADQUANTITY);
        }

        newResource.quantity = strtol(argv[(i * 2) + 3], NULL, 10);
        append_resource(newResource, depot);
    }
}

/**
 * Creates and connects the socket for the emphemeral port (command port) and
 * waits for a connect to it. The connection is returned as an open file which
 * can only be read from.
 * Takes an empty struct addrinfo as an argument
 *
 * @param comAddr addrinfo that data from the connection will be loaded to
 * @return      opened file that can be read from
 */
int init_command_port(struct addrinfo** comAddr) {
    int status, sockfd;
    struct addrinfo hints;
    char buffer[BUFFER_SIZE];

    memset(&buffer, 0, sizeof(buffer));
    memset(&hints, 0, sizeof(hints));
    memset(comAddr, 0, sizeof(*comAddr));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    if ((status = getaddrinfo("localhost", "0", &hints, comAddr)) < 0) {
        return status;
    }

    if ((sockfd = socket((*comAddr)->ai_family, (*comAddr)->ai_socktype,
            (*comAddr)->ai_protocol)) < 0) {
        return sockfd;
    }

    if ((status = bind(sockfd, (*comAddr)->ai_addr, (*comAddr)->ai_addrlen)) <
            0) {
        return status;
    }

    if ((status = getsockname(sockfd, (*comAddr)->ai_addr,
            &((*comAddr)->ai_addrlen))) < 0) {
        return status;
    }

    if ((status = listen(sockfd, CONNECTION_LIMIT)) < 0) {
        return status;
    }

    if ((inet_ntop(AF_INET,
            &((struct sockaddr_in*) (*comAddr)->ai_addr)->sin_addr,
            buffer, sizeof(buffer))) == NULL) {
        return -1;
    }

    if ((status = getsockname(sockfd, (*comAddr)->ai_addr,
            &((*comAddr)->ai_addrlen))) < 0) {
        return status;
    }

    depot.port = ntohs(((struct sockaddr_in*) (*comAddr)->ai_addr)->sin_port);
    fprintf(stdout, "%d\n", depot.port);
    fflush(stdout);
    return sockfd;
}

/**
 * Waits for an IM instruction for a given stream
 *
 * @param args  void pointer to a GetInstructionArgs struct
 * @return      returns nothing. However, writes data into given struct
 */
void* wait_for_IM(void* args) {
    pthread_mutex_lock(&((ThreadData*) args)->lock);
    pthread_mutex_lock(&otherDepots.lock);
    pthread_detach(pthread_self());
    char message[BUFFER_SIZE];
    char* pmsg = message;
    char buffer[BUFFER_SIZE];
    memset(&message, 0, sizeof(message));
    memset(&buffer, 0, sizeof(buffer));
    GetInstructionArgs* argv = ((Reading*) args)->args;


    if (fgets(message, sizeof(message), argv->streams->read) == NULL) {
        ((Reading*) args)->threadData.threadCompleted = REMOVED;
        pthread_mutex_unlock(&((ThreadData*) args)->lock);
        pthread_exit(NULL);
    }

    if (message[strlen(message) - 1] == '\n') {
        message[strlen(message) - 1] = 0;
    }

    if (message[0] != 'I' || message[1] != 'M' || message[2] != ':') {
        ((Reading*) args)->threadData.threadCompleted = REMOVED;
        pthread_mutex_unlock(&((ThreadData*) args)->lock);
        pthread_exit(NULL);
    }

    pmsg += 3;
    while (isdigit(pmsg[0])) {
        buffer[strlen(buffer)] = pmsg[0];
        pmsg += 1;
    }

    otherDepots.neighbours[otherDepots.len - 1].port =
            strtol(buffer, NULL, 10);
    otherDepots.neighbours[otherDepots.len - 1].streams =
            malloc(sizeof(Streams));
    memcpy(otherDepots.neighbours[otherDepots.len - 1].streams,
            argv->streams, sizeof(*argv->streams));
    otherDepots.neighbours[otherDepots.len - 1].name = malloc(
            strlen(pmsg) + 1);
    strcpy(otherDepots.neighbours[otherDepots.len - 1].name, (pmsg + 1));
    pthread_mutex_unlock(&otherDepots.lock);

    ((Reading*) args)->flag = NO_FLAGS;
    ((Reading*) args)->threadData.threadCompleted = COMPLETED;
    pthread_mutex_unlock(&((ThreadData*) args)->lock);
    pthread_exit(NULL);
}

/**
 * Reads instructions from the FILE* stream given int he GetInstructionArgs
 * passed to the function through the (void*)
 *
 * @param args  void pointer to a GetInstructionArgs struct
 * @return      returns nothing. However, writes data into given struct
 */
void* get_instruction(void* args) {
    pthread_mutex_lock(&((ThreadData*) args)->lock);
    pthread_detach(pthread_self());
    char message[BUFFER_SIZE];
    memset(&message, 0, sizeof(message));
    GetInstructionArgs* argv = ((Reading*) args)->args;

    if (fgets(message, sizeof(message), argv->streams->read) == NULL) {
        ((Reading*) args)->threadData.threadCompleted = REMOVED;
        pthread_mutex_unlock(&((ThreadData*) args)->lock);
        pthread_exit(NULL);
    }

    if (message[strlen(message) - 1] == '\n') {
        message[strlen(message) - 1] = 0;
    }

    ((Reading*) args)->flag = NO_FLAGS;

    char** result;
    result = handle_instruction(message, argv->queue);
    if (result != NULL && result[0] != NULL) {
        if (strcmp(result[0], "NEW DEPOT") == 0) {
            pthread_mutex_lock(&otherDepots.lock);
            ((Reading*) args)->args->result->write = fdopen(
                    strtol(result[1], NULL, 0), "w");
            ((Reading*) args)->args->result->read = fdopen(
                    strtol(result[1], NULL, 0), "r");
            fprintf(((Reading*) args)->args->result->write, "IM:%d:%s\n",
                    depot.port, depot.name);
            fflush(((Reading*) args)->args->result->write);
            ((Reading*) args)->flag = NEW_CONNECTION;
            pthread_mutex_unlock(&otherDepots.lock);
        } else {
            pthread_mutex_lock(&otherDepots.lock);
            for (int j = 0; result[j * 2] != NULL; j++) {
                for (int i = 0; i < otherDepots.len; i++) {
                    if (strcmp(result[j * 2],
                            otherDepots.neighbours[i].name) == 0) {
                        fputs(result[(j * 2) + 1],
                                otherDepots.neighbours[i].streams->write);
                        fflush(otherDepots.neighbours[i].streams->write);
                    }
                }
            }

            pthread_mutex_unlock(&otherDepots.lock);
        }
    }

    ((Reading*) args)->threadData.threadCompleted = COMPLETED;
    pthread_mutex_unlock(&((ThreadData*) args)->lock);
    pthread_exit(NULL);
}

/**
 * Listens for a connection on the socket given in the Listen4Args struct
 *
 * @param args  void pointer to a Listen4Args struct
 * @return      returns nothing. However, writes data into given struct
 */
void* listen_for_connection(void* args) {
    //pthread_detach(((ThreadData*) args)->threadID);
    pthread_mutex_lock(&((ThreadData*) args)->lock);
    pthread_detach(pthread_self());
    Listen4ConnectionArgs* argv = ((Listening*) args)->args;
    int newfd;
    struct sockaddr_storage connectedAddr;

    socklen_t addrSize = sizeof(connectedAddr);
    if ((newfd = accept(argv->sockfd, (struct sockaddr*) &connectedAddr,
            &addrSize)) < 0) {
        ((Listening*) args)->threadData.threadCompleted = COMPLETED;
        //pthread_detach(((ThreadData*) args)->threadID);
        pthread_mutex_unlock(&((ThreadData*) args)->lock);
        pthread_exit(NULL);
    }


    pthread_mutex_lock(&otherDepots.lock);
    otherDepots.neighbours[otherDepots.len].port = ntohs(
            ((struct sockaddr_in*) &connectedAddr)->sin_port);
    otherDepots.len += 1;
    pthread_mutex_unlock(&otherDepots.lock);

    argv->result->read = fdopen(newfd, "r");
    argv->result->write = fdopen(newfd, "w");

    send_IM(ntohs(((struct sockaddr_in*) (*argv->comAddr)->ai_addr)->sin_port),
            argv->result->write);

    ((Listening*) args)->threadData.threadCompleted = COMPLETED;
    //pthread_detach(((ThreadData*) args)->threadID);
    pthread_mutex_unlock(&((ThreadData*) args)->lock);
    pthread_exit(NULL);
}

/**
 * Attempts to connect to a depot for the given port
 *
 * @param port  port to connect to
 * @return      NULL, or {"NEW DEPORT", sockfd of accepted connection}
 */
char** connect_to_depot(char* port) {
    for (int i = 0; i < strlen(port); i++) {
        if (!isdigit(port[i])) {
            return NULL;
        }

    }

    pthread_mutex_lock(&otherDepots.lock);
    for (int i = 0; i < otherDepots.len; i++) {
        if (otherDepots.neighbours[i].port == strtol(port, NULL, 10)) {
            pthread_mutex_unlock(&otherDepots.lock);
            return NULL;
        }
    }
    otherDepots.neighbours[otherDepots.len].port = strtol(port, NULL, 10);
    otherDepots.neighbours[otherDepots.len].name = strdup("NO IM");
    otherDepots.len += 1;
    pthread_mutex_unlock(&otherDepots.lock);

    int status, sockfd;
    struct addrinfo hints, * depotAddr;
    char buffer[BUFFER_SIZE];
    char** result;

    memset(&buffer, 0, sizeof(buffer));
    memset(&hints, 0, sizeof(hints));
    memset(&depotAddr, 0, sizeof(depotAddr));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    if ((status = getaddrinfo("localhost", port, &hints, &depotAddr)) < 0) {
        otherDepots.len -= 1;
        return NULL;
    }

    if ((sockfd = socket(depotAddr->ai_family, depotAddr->ai_socktype,
            depotAddr->ai_protocol)) < 0) {
        otherDepots.len -= 1;
        return NULL;
    }

    if ((status = connect(sockfd, depotAddr->ai_addr, depotAddr->ai_addrlen)) <
            0) {
        otherDepots.len -= 1;
        return NULL;
    }

    sprintf(buffer, "%d", sockfd);
    result = calloc(2, sizeof(char*));
    result[0] = malloc(strlen("NEW DEPOT") + 1);
    strcpy(result[0], "NEW DEPOT");
    result[1] = malloc(strlen(buffer) + 1);
    strcpy(result[1], buffer);
    return result;
}

/**
 * Sends IM message to given FILE* stream.
 *
 * @param port      port of self
 * @param stream    stream to send IM to
 */
void send_IM(uint16_t port, FILE* stream) {
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE];

    sprintf(buffer, "%d", port);

    strcpy(message, "IM:");
    strcat(message, buffer);
    strcat(message, ":");
    strcat(message, depot.name);
    strcat(message, "\n");

    fputs(message, stream);
    fflush(stream);
}

/**
 * Appends a new resource onto the list of resources contained in the Depot
 * struct.
 * The depot struct MUST ALREADY BE INITIALISED.
 *
 * @param newResource   resources to append to depot
 * @param depot         depot containing the list
 */
void append_resource(Resource newResource, Depot* depot) {
    Resource* newList;

    newList = calloc(depot->count + 1, sizeof(Resource));
    for (int i = 0; i < depot->count; i++) {
        newList[i] = depot->resources[i];
    }

    newList[depot->count] = newResource;

    free(depot->resources);

    depot->resources = newList;
    depot->count += 1;
}

/**
 * Handles instruction received from other depot
 *
 * @param message   message received
 * @param queue     instruction queue to push defer'd instruction to
 * @return          char array of information for get_instruction to follow
 *                  up on instruction correctly
 */
char** handle_instruction(char* message, Queue* queue) {
    //message = queue_pop(queue);
    //fputs(message, stdout);
    //fflush(stdout);

    // TODO: strtok is causing the original to toekn as well :'(

    char delim[] = ":";
    char* original = malloc(strlen(message) + 1);
    strcpy(original, message);
    char* instruction;
    char** status = calloc((queue->len + 1) * 2, sizeof(char*));
    char* pntr = strtok(message, delim);
    instruction = malloc(strlen(pntr) + 1);
    strcpy(instruction, pntr);

    if (strcmp(instruction, "Connect") == 0) {
        pntr = strtok(NULL, delim);
        if (pntr == NULL) {
            return NULL;
        }
        status = connect_to_depot(pntr);
    }
    if (strcmp(instruction, "Deliver") == 0 ||
            strcmp(instruction, "Withdraw") == 0 ||
            strcmp(instruction, "Transfer") == 0) {
        char* quantity;
        char* type;
        char* dest;

        pntr = strtok(NULL, delim); // quant
        quantity = malloc(strlen(pntr) + 1);
        strcpy(quantity, pntr);

        pntr = strtok(NULL, delim); // type
        type = malloc(strlen(pntr) + 1);
        strcpy(type, pntr);

        pntr = strtok(NULL, delim); // null or dest
        if (pntr == NULL) {
            dest = NULL;
        } else {
            dest = malloc(strlen(pntr) + 1);
            strcpy(dest, pntr);
            pntr = strtok(NULL, delim); // null
            if (pntr != NULL) {
                return NULL;
            }
        }

        status = modify_stock(instruction, quantity, type, dest);

        free(dest);
        free(type);
    } else if (strcmp(instruction, "Defer") == 0) {
        pntr = strtok(NULL, delim); // key
        if (pntr == NULL) {
            return NULL;
        }

        pntr = strtok(NULL, delim); // instruction
        if (pntr == NULL) {
            return NULL;
        }

        pntr = strtok(NULL, delim); // quantity
        if (pntr == NULL || !valid_quantity(pntr)) {
            return NULL;
        }

        pntr = strtok(NULL, delim); // type
        if (pntr == NULL || !valid_name(pntr)) {
            return NULL;
        }

        pntr = strtok(NULL, delim); // null or dest
        if (pntr != NULL) {
            if (!valid_name(pntr)) {
                return NULL;
            }
            pntr = strtok(NULL, delim); // null
            if (pntr == NULL) {
                queue_push(original, queue);
            }
        } else {
            queue_push(original, queue);
        }

        status = NULL;
    } else if (strcmp(instruction, "Execute") == 0) {
        char** statusBuilder;
        int j = 0;
        pthread_mutex_lock(&queue->lock);
        char* key;

        pntr = strtok(NULL, delim);
        key = malloc(strlen(pntr) + 1);
        strcpy(key, pntr);
        int loops = queue->len;
        for (int i = 0; i < loops; i++) {
            char nextMessage[BUFFER_SIZE];
            memset(nextMessage, 0, sizeof(nextMessage));
            char* originalNextMessage;
            strcpy(nextMessage, queue_pop(queue));
            originalNextMessage = malloc(strlen(nextMessage) + 1);
            strcpy(originalNextMessage, nextMessage);
            pntr = strtok(nextMessage, delim);
            pntr = strtok(NULL, delim);
            if (strcmp(pntr, key) == 0) {
                originalNextMessage += strlen(key) + strlen("Defer::");
                statusBuilder = handle_instruction(originalNextMessage, queue);
                if (statusBuilder != NULL) {
                    status[j * 2] = malloc(strlen(statusBuilder[0]) + 1);
                    strcpy(status[j * 2], statusBuilder[0]);
                    status[(j * 2) + 1] = malloc(strlen(statusBuilder[1]) + 1);
                    strcpy(status[(j * 2) + 1], statusBuilder[1]);
                    statusBuilder = NULL;
                    j++;
                }
            } else {
                queue_push(originalNextMessage, queue);
            }
        }
        pthread_mutex_unlock(&queue->lock);
    }

    free(instruction);
    return status;
}

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
char** modify_stock(char* action, char* quantity, char* type, char* dest) {
    if (!valid_quantity(quantity)) {
        return NULL;
    }

    pthread_mutex_lock(&depot.lock);
    if (dest != NULL) {
        int i;
        for (i = 0; i < otherDepots.len; i++) {
            if (strcmp(otherDepots.neighbours[i].name, dest) == 0) {
                break;
            }
        }
        if (i >= otherDepots.len) {
            pthread_mutex_unlock(&depot.lock);
            return NULL;
        }
    }

    int quant = strtol(quantity, NULL, 10);

    for (int i = 0; i < depot.count; i++) {
        if (strcmp(depot.resources[i].name, type) == 0) {
            if (strcmp(action, "Deliver") == 0) {
                depot.resources[i].quantity += quant;
                pthread_mutex_unlock(&depot.lock);
                return NULL;
            } else if (strcmp(action, "Withdraw") == 0) {
                depot.resources[i].quantity -= quant;
                pthread_mutex_unlock(&depot.lock);
                return NULL;
            } else if (strcmp(action, "Transfer") == 0) {
                if (dest == NULL) {
                    pthread_mutex_unlock(&depot.lock);
                    return NULL;
                }

                depot.resources[i].quantity -= quant;
                pthread_mutex_unlock(&depot.lock);
                char** messageToReturn = calloc(2, sizeof(char*));
                messageToReturn[0] = malloc(strlen(dest) + 1);
                strcpy(messageToReturn[0], dest);
                messageToReturn[1] = calloc(BUFFER_SIZE, sizeof(char));
                strcpy(messageToReturn[1], "Deliver:");
                strcat(messageToReturn[1], quantity);
                strcat(messageToReturn[1], ":");
                strcat(messageToReturn[1], type);
                messageToReturn[1][strlen(messageToReturn[1])] = '\n';
                return messageToReturn;
            }
        }
    }

    if (valid_name(type)) {
        Resource newResource;
        newResource.name = malloc(strlen(type) + 1);
        strcpy(newResource.name, type);
        if (strcmp(action, "Deliver") == 0) {
            newResource.quantity = quant;
        } else if (strcmp(action, "Transfer") == 0) {
            newResource.quantity = -1 * quant;
            append_resource(newResource, &depot);
            pthread_mutex_unlock(&depot.lock);
            char** messageToReturn = calloc(2, sizeof(char*));
            messageToReturn[0] = malloc(strlen(dest) + 1);
            strcpy(messageToReturn[0], dest);
            messageToReturn[1] = calloc(BUFFER_SIZE, sizeof(char));
            strcpy(messageToReturn[1], "Deliver:");
            strcat(messageToReturn[1], quantity);
            strcat(messageToReturn[1], ":");
            strcat(messageToReturn[1], type);
            messageToReturn[1][strlen(messageToReturn[1])] = '\n';
            return messageToReturn;
        } else {
            newResource.quantity = -1 * quant;
        }
        append_resource(newResource, &depot);
    }

    pthread_mutex_unlock(&depot.lock);
    return NULL;
}

int main(int argc, char** argv) {
    int ephfd;
    depot = default_depot();
    Queue instructionQueue = default_queue();
    otherDepots = default_neighbour_list();
    init_depot(argc, argv, &depot);
    init_signal_handling();

    struct addrinfo* ephai;
    do {
        ephfd = init_command_port(&ephai);
    } while (ephfd < 0);

    ThreadData** threads = calloc(CONNECTION_LIMIT + 1, sizeof(ThreadData*));
    for (int i = 0; i < CONNECTION_LIMIT + 1; i++) {
        threads[i] = calloc(1, sizeof(ThreadData));
        pthread_mutex_init(&threads[i]->lock, NULL);
    }

    threads[0]->type = LISTENING_THREAD;
    threads[0]->threadCompleted = NOT_STARTED;
    ((Listening*) threads[0])->args = malloc(sizeof(Listen4ConnectionArgs));
    ((Listening*) threads[0])->args->result = malloc(sizeof(Streams));
    ((Listening*) threads[0])->args->comAddr = &ephai;
    ((Listening*) threads[0])->args->sockfd = ephfd;

    int nextThreadToInit = 1;
    while (true) {
        for (int i = 0; i < CONNECTION_LIMIT + 1; i++) {
            if (threads[i]->threadCompleted ==
                    NOT_STARTED) {   // first time the thread is started
                pthread_mutex_lock(&threads[i]->lock);
                threads[i]->threadCompleted = NOT_COMPLETED;
                if (threads[i]->type == LISTENING_THREAD) {
                    pthread_create(
                            &((Listening*) threads[i])->threadData.threadID,
                            NULL,
                            listen_for_connection,
                            (void*) ((Listening*) threads[i]));
                } else if (threads[i]->type == READING_THREAD) {
                    pthread_create(
                            &((Reading*) threads[i])->threadData.threadID,
                            NULL,
                            wait_for_IM, (void*) ((Reading*) threads[i]));
                }

                pthread_mutex_unlock(&threads[i]->lock);
            } else if (threads[i]->threadCompleted ==
                    COMPLETED) {  // every other time the thread is started
                pthread_mutex_lock(&threads[i]->lock);
                threads[i]->threadCompleted = NOT_COMPLETED;
                if (threads[i]->type == LISTENING_THREAD) {
                    ((Reading*) threads[nextThreadToInit])->threadData.type =
                            READING_THREAD;
                    ((Reading*) threads[nextThreadToInit])->
                            threadData.threadCompleted = NOT_STARTED;
                    ((Reading*) threads[nextThreadToInit])->args = malloc(
                            sizeof(GetInstructionArgs));
                    ((Reading*) threads[nextThreadToInit])->args->queue =
                            &instructionQueue;
                    ((Reading*) threads[nextThreadToInit])->args->streams =
                            malloc(sizeof(Streams));
                    ((Reading*) threads[nextThreadToInit])->args->result =
                            malloc(sizeof(Streams));
                    memcpy(
                            ((Reading*) threads[nextThreadToInit])->
                            args->streams,
                            ((Listening*) threads[i])->args->result,
                            sizeof(*((Listening*) threads[i])->args->result));
                    nextThreadToInit += 1;
                    pthread_create(
                            &((Listening*) threads[i])->threadData.threadID,
                            NULL,
                            listen_for_connection,
                            (void*) ((Listening*) threads[i]));
                } else if (threads[i]->type == READING_THREAD) {
                    if (((Reading*) threads[i])->flag == NEW_CONNECTION) {
                        ((Reading*) threads[i])->flag = NO_FLAGS;
                        ((Reading*) threads[nextThreadToInit])->
                                threadData.type = READING_THREAD;
                        ((Reading*) threads[nextThreadToInit])->
                                threadData.threadCompleted = NOT_STARTED;
                        ((Reading*) threads[nextThreadToInit])->args = malloc(
                                sizeof(GetInstructionArgs));
                        ((Reading*) threads[nextThreadToInit])->args->queue =
                                &instructionQueue;
                        ((Reading*) threads[nextThreadToInit])->args->streams =
                                malloc(sizeof(Streams));
                        ((Reading*) threads[nextThreadToInit])->args->result =
                                malloc(sizeof(Streams));
                        memcpy(
                                ((Reading*) threads[nextThreadToInit])->
                                args->streams,
                                ((Reading*) threads[i])->args->result,
                                sizeof(*((Reading*) threads[i])->
                                args->result));
                        nextThreadToInit += 1;
                    }

                    pthread_create(
                            &((Reading*) threads[i])->threadData.threadID,
                            NULL,
                            get_instruction, (void*) ((Reading*) threads[i]));
                }

                pthread_mutex_unlock(&threads[i]->lock);
            }
        }
    }

    free(depot.resources);
    pthread_mutex_destroy(&instructionQueue.lock);
    return OK;
}

/**
 * prints depot to stdout
 */
void print_depot() {
    //pthread_mutex_lock(&depot.lock);
    fputs("Goods:\n", stdout);
    fflush(stdout);
    for (int i = 0; i < depot.count; i++) {
        if (depot.resources[i].quantity != 0) {
            fprintf(stdout, "%s %d\n",
                    depot.resources[i].name, depot.resources[i].quantity);
            fflush(stdout);
        }
    }
    //pthread_mutex_unlock(&depot.lock);
}

/**
 * pritns neighbours to stdout
 */
void print_neighbours() {
    //pthread_mutex_lock(&otherDepots.lock);
    fputs("Neighbours:\n", stdout);
    fflush(stdout);
    for (int i = 0; i < otherDepots.len; i++) {
        fprintf(stdout, "%s\n", otherDepots.neighbours[i].name);
        fflush(stdout);
    }
    //pthread_mutex_unlock(&otherDepots.lock);
}

/**
 * Handler for SIGHUP signal
 *
 * @param sig   signal number
 */
static void handle_sighup(int sig) {
    pthread_mutex_lock(&depot.lock);
    pthread_mutex_lock(&otherDepots.lock);
    bubble_sort_resources(depot.resources, depot.count);
    bubble_sort_neighbours(otherDepots.neighbours, otherDepots.len);
    print_depot(depot);
    print_neighbours(otherDepots);
    pthread_mutex_unlock(&otherDepots.lock);
    pthread_mutex_unlock(&depot.lock);
}

/**
 * End function due to error. Prints corresponding error message to stderr
 *
 * @param s status on exit; error type
 */
void quit_on_error(Status s) {
    const char* messages[] = {
        "",
        "Usage: 2310depot name {goods qty}\n",
        "Invalid name(s)\n",
        "Invalid quantity\n"
    };
    fputs(messages[s], stderr);
    fflush(stderr);
    exit(s);
}

/**
 * Checks if the given name conforms to the requirements and returns a bool
 * accordingly.
 *  Name cannot be empty.
 *  Name cannot contain; spaces, '\n', '\r', colons.
 *
 * @param name  name to be checked
 * @return      bool; true if name is valid, false if not
 */
bool valid_name(char* name) {
    if (strcmp(name, "") == 0) {
        return false;
    }

    for (int i = 0; i < strlen(name); i++) {
        if (name[i] == ' ' || name[i] == '\n' ||
                name[i] == '\r' || name[i] == ':') {
            return false;
        }
    }

    return true;
}

/**
 * Checks if the given quantity conforms to the requirements and returns a bool
 * accordingly.
 *  Quantity must be a number.
 *  Quantity must be greater than 0.
 *
 * @param quantity  quantity to the checked
 * @return          bool; true if the quantity is valid, false otherwise.
 */
bool valid_quantity(char* quantity) {
    if (strcmp(quantity, "") == 0) {
        return false;
    }

    for (int i = 0; i < strlen(quantity); i++) {
        if (!isdigit(quantity[i])) {
            return false;
        }
    }

    if (strtol(quantity, NULL, 10) < 0) {
        return false;
    }

    return true;
}

/**
 * Checks if a message received from another port is a valid message. Used to
 * remove cases that would cause handle_instruction to error.
 *
 * @param message   message received from other depot
 * @return          bool; true if the message is okay, false otherwise
 */
bool valid_message(char* message) {
    if (message == NULL) {
        return false;
    }

    if (strcmp(message, "") == 0) {
        return false;
    }

    if (message[0] == 'I' && message[1] == 'M') {
        return false;
    }

    return true;
}