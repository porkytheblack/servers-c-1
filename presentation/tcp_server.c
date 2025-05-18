#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#define PORT 8081
#define BUFFER_SIZE 1024
#define FILENAME "accounts.dat"
#define MAX_TRANSACTIONS 5
#define MIN_DEPOSIT 1000.00
#define MAX_CLIENTS 10

struct date {
    int day, month, year;
};

struct transaction {
    struct date when;
    char type[10];
    float amount;
};

struct account {
    char name[60];
    int acc_no, pin;
    char acc_type[10];
    float balance;
    struct date created;
    struct transaction history[MAX_TRANSACTIONS];
    int transaction_count;
};

// Function prototypes
void handle_client(int client_sock);
int authenticate_account(int acc_no, int pin);
int create_account(char *name, char *acc_type, float deposit, int *new_acc_no, int *new_pin);
int process_deposit(int acc_no, float amount);
int process_withdraw(int acc_no, float amount);
int delete_account(int acc_no);
struct account* get_account_info(int acc_no);
int add_transaction(struct account *acc, const char *type, float amount);
int generate_statement(struct account *acc, char *output, size_t output_size);
int save_accounts();
int load_accounts();
void cleanup();
void sigchld_handler(int sig);

// Global variables
struct account *accounts = NULL;
size_t account_count = 0;

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    pid_t pid;

    // Set up signal handler for zombie processes
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Load existing accounts
    if (!load_accounts()) {
        fprintf(stderr, "Failed to load accounts\n");
        return EXIT_FAILURE;
    }

    // Create TCP socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        cleanup();
        return EXIT_FAILURE;
    }

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_sock);
        cleanup();
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind the socket
    if (bind(server_sock, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_sock);
        cleanup();
        return EXIT_FAILURE;
    }

    // Listen for connections
    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("listen failed");
        close(server_sock);
        cleanup();
        return EXIT_FAILURE;
    }

    printf("Server is running on port %d...\n", PORT);

    while (1) {
        // Accept new connection
        if ((client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len)) < 0) {
            if (errno == EINTR) {
                continue; // Interrupted system call, try again
            }
            perror("accept failed");
            continue;
        }

        printf("New connection from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Fork a new process to handle the client
        pid = fork();
        if (pid < 0) {
            perror("fork failed");
            close(client_sock);
            continue;
        }

        if (pid == 0) { // Child process
            close(server_sock); // Child doesn't need the listener
            handle_client(client_sock);
            close(client_sock);
            exit(EXIT_SUCCESS);
        } else { // Parent process
            close(client_sock); // Parent doesn't need the client socket
        }
    }

    close(server_sock);
    save_accounts();
    cleanup();
    return EXIT_SUCCESS;
}


void sigchld_handler(int sig) {
    // Wait for all dead processes
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE] = {0};
    char response[BUFFER_SIZE] = {0};
    int bytes_read;

    while ((bytes_read = read(client_sock, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("Received from client: %s\n", buffer);

        char command[20] = {0};
        char params[BUFFER_SIZE - 20] = {0};
        int acc_no, pin, new_pin;
        float amount;
        char name[60] = {0};
        char acc_type[10] = {0};
        struct account *acc;

        // Parse command and parameters
        if (sscanf(buffer, "%19s %[^\n]", command, params) < 1) {
            snprintf(response, BUFFER_SIZE, "ERROR Invalid command format");
            goto send_response;
        }

        if (strcmp(command, "LOGIN") == 0) {
            if (sscanf(params, "%d %d", &acc_no, &pin) != 2) {
                snprintf(response, BUFFER_SIZE, "ERROR Invalid LOGIN format");
            } else if (authenticate_account(acc_no, pin)) {
                snprintf(response, BUFFER_SIZE, "SUCCESS Login successful");
            } else {
                snprintf(response, BUFFER_SIZE, "ERROR Invalid credentials");
            }
        }
        else if (strcmp(command, "CREATE") == 0) {
            if (sscanf(params, "%59s %9s %f", name, acc_type, &amount) != 3) {
                snprintf(response, BUFFER_SIZE, "ERROR Invalid CREATE format");
            } else if (amount < MIN_DEPOSIT) {
                snprintf(response, BUFFER_SIZE, "ERROR Minimum deposit is %.2f", MIN_DEPOSIT);
            } else if (create_account(name, acc_type, amount, &acc_no, &new_pin)) {
                snprintf(response, BUFFER_SIZE, 
                       "SUCCESS Account created\nAccount Number: %d\nPIN: %04d\n",
                       acc_no, new_pin);
            } else {
                snprintf(response, BUFFER_SIZE, "ERROR Account creation failed");
            }
        }
        else if (strcmp(command, "DEPOSIT") == 0) {
            if (sscanf(params, "%d %f", &acc_no, &amount) != 2) {
                snprintf(response, BUFFER_SIZE, "ERROR Invalid DEPOSIT format");
            } else if (amount <= 0) {
                snprintf(response, BUFFER_SIZE, "ERROR Amount must be positive");
            } else if (process_deposit(acc_no, amount)) {
                acc = get_account_info(acc_no);
                snprintf(response, BUFFER_SIZE, "SUCCESS %.2f", acc->balance);
            } else {
                snprintf(response, BUFFER_SIZE, "ERROR Deposit failed");
            }
        }
        else if (strcmp(command, "WITHDRAW") == 0) {
            if (sscanf(params, "%d %f", &acc_no, &amount) != 2) {
                snprintf(response, BUFFER_SIZE, "ERROR Invalid WITHDRAW format");
            } else if (amount <= 0) {
                snprintf(response, BUFFER_SIZE, "ERROR Amount must be positive");
            } else if (process_withdraw(acc_no, amount)) {
                acc = get_account_info(acc_no);
                snprintf(response, BUFFER_SIZE, "SUCCESS %.2f", acc->balance);
            } else {
                snprintf(response, BUFFER_SIZE, "ERROR Insufficient funds");
            }
        }
        else if (strcmp(command, "BALANCE") == 0) {
            if (sscanf(params, "%d", &acc_no) != 1) {
                snprintf(response, BUFFER_SIZE, "ERROR Invalid BALANCE format");
            } else if ((acc = get_account_info(acc_no))) {
                snprintf(response, BUFFER_SIZE, "SUCCESS %.2f", acc->balance);
            } else {
                snprintf(response, BUFFER_SIZE, "ERROR Account not found");
            }
        }
        else if (strcmp(command, "DELETE") == 0) {
            if (sscanf(params, "%d", &acc_no) != 1) {
                snprintf(response, BUFFER_SIZE, "ERROR Invalid DELETE format");
            } else if (delete_account(acc_no)) {
                snprintf(response, BUFFER_SIZE, "SUCCESS Account deleted");
            } else {
                snprintf(response, BUFFER_SIZE, "ERROR Deletion failed");
            }
        }
        else if (strcmp(command, "STATEMENT") == 0) {
            if (sscanf(params, "%d", &acc_no) != 1) {
                snprintf(response, BUFFER_SIZE, "ERROR Invalid STATEMENT format");
            } else if ((acc = get_account_info(acc_no))) {
                char stmt[512];
                if (generate_statement(acc, stmt, sizeof(stmt))) {
                    snprintf(response, BUFFER_SIZE, "SUCCESS %s", stmt);
                } else {
                    snprintf(response, BUFFER_SIZE, "ERROR Failed to generate statement");
                }
            } else {
                snprintf(response, BUFFER_SIZE, "ERROR Account not found");
            }
        }
        else {
            snprintf(response, BUFFER_SIZE, "ERROR Unknown command");
        }

send_response:
        printf("Sending response: %s\n", response);
        if (write(client_sock, response, strlen(response)) < 0) {
            perror("write failed");
            break;
        }
        memset(buffer, 0, BUFFER_SIZE);
        memset(response, 0, BUFFER_SIZE);
    }

    if (bytes_read < 0) {
        perror("read");
    }

    printf("Client disconnected\n");
}

void cleanup() {
    if (accounts) {
        free(accounts);
        accounts = NULL;
    }
    account_count = 0;
}

int load_accounts() {
    FILE *fp = fopen(FILENAME, "rb");
    if (!fp) {
        accounts = NULL;
        account_count = 0;
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size == 0) {
        fclose(fp);
        accounts = NULL;
        account_count = 0;
        return 1;
    }

    rewind(fp);
    account_count = size / sizeof(struct account);
    accounts = malloc(size);
    if (!accounts) {
        fclose(fp);
        account_count = 0;
        return 0;
    }

    if (fread(accounts, sizeof(struct account), account_count, fp) != account_count) {
        free(accounts);
        accounts = NULL;
        account_count = 0;
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

int save_accounts() {
    FILE *fp = fopen(FILENAME, "wb");
    if (!fp) {
        perror("Failed to open accounts file");
        return 0;
    }

    size_t written = fwrite(accounts, sizeof(struct account), account_count, fp);
    fclose(fp);

    if (written != account_count) {
        fprintf(stderr, "Error: Only wrote %zu of %ld accounts\n", written, account_count);
        return 0;
    }
    return 1;
}

int authenticate_account(int acc_no, int pin) {
    for (int i = 0; i < account_count; i++) {
        if (accounts[i].acc_no == acc_no && accounts[i].pin == pin) {
            return 1;
        }
    }
    return 0;
}

int create_account(char *name, char *acc_type, float deposit, int *new_acc_no, int *new_pin) {
    // Validate inputs
    if (!name || !acc_type || !new_acc_no || !new_pin) return 0;
    if (deposit < MIN_DEPOSIT) return 0;
    if (strcmp(acc_type, "savings") != 0 && strcmp(acc_type, "current") != 0) return 0;

    // Allocate space
    struct account *temp = realloc(accounts, (account_count + 1) * sizeof(struct account));
    if (!temp) {
        fprintf(stderr, "Memory allocation failed\n");
        return 0;
    }
    accounts = temp;

    // Initialize account
    struct account *new_acc = &accounts[account_count];
    memset(new_acc, 0, sizeof(struct account));

    // Generate unique account number
    srand(time(NULL));
    do {
        *new_acc_no = 100000 + rand() % 900000;
    } while (get_account_info(*new_acc_no) != NULL);

    // Generate PIN (ensure 4 digits)
    *new_pin = 1000 + rand() % 9000;
    
    // Set account details
    strncpy(new_acc->name, name, sizeof(new_acc->name) - 1);
    strncpy(new_acc->acc_type, acc_type, sizeof(new_acc->acc_type) - 1);
    new_acc->balance = deposit;
    new_acc->acc_no = *new_acc_no;
    new_acc->pin = *new_pin;

    // Set creation date
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    new_acc->created.day = tm->tm_mday;
    new_acc->created.month = tm->tm_mon + 1;
    new_acc->created.year = tm->tm_year + 1900;

    // Add transaction
    if (!add_transaction(new_acc, "Deposit", deposit)) {
        fprintf(stderr, "Failed to add initial transaction\n");
    }

    account_count++;
    return save_accounts();
}

int process_deposit(int acc_no, float amount) {
    if (amount <= 0) return 0;

    struct account *acc = get_account_info(acc_no);
    if (!acc) return 0;

    acc->balance += amount;
    if (!add_transaction(acc, "Deposit", amount)) {
        fprintf(stderr, "Failed to record deposit transaction\n");
    }

    return save_accounts();
}

int process_withdraw(int acc_no, float amount) {
    if (amount <= 0) return 0;

    struct account *acc = get_account_info(acc_no);
    if (!acc || acc->balance < amount) return 0;

    acc->balance -= amount;
    if (!add_transaction(acc, "Withdraw", amount)) {
        fprintf(stderr, "Failed to record withdrawal transaction\n");
    }

    return save_accounts();
}

int delete_account(int acc_no) {
    int index = -1;
    for (int i = 0; i < account_count; i++) {
        if (accounts[i].acc_no == acc_no) {
            index = i;
            break;
        }
    }

    if (index == -1) return 0;

    // Shift remaining accounts
    memmove(&accounts[index], &accounts[index + 1],
           (account_count - index - 1) * sizeof(struct account));
    account_count--;

    // Reallocate memory
    struct account *temp = realloc(accounts, account_count * sizeof(struct account));
    if (account_count > 0 && !temp) {
        fprintf(stderr, "Warning: Memory reallocation failed after deletion\n");
    } else {
        accounts = temp;
    }

    return save_accounts();
}

int add_transaction(struct account *acc, const char *type, float amount) {
    if (!acc || !type || amount <= 0) return 0;

    // Shift transactions if full
    if (acc->transaction_count >= MAX_TRANSACTIONS) {
        memmove(&acc->history[0], &acc->history[1],
               (MAX_TRANSACTIONS - 1) * sizeof(struct transaction));
        acc->transaction_count = MAX_TRANSACTIONS - 1;
    }

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    int idx = acc->transaction_count++;

    acc->history[idx].when.day = tm->tm_mday;
    acc->history[idx].when.month = tm->tm_mon + 1;
    acc->history[idx].when.year = tm->tm_year + 1900;
    strncpy(acc->history[idx].type, type, sizeof(acc->history[idx].type) - 1);
    acc->history[idx].amount = amount;

    return 1;
}

int generate_statement(struct account *acc, char *output, size_t output_size) {
    if (!acc || !output || output_size < 100) return 0;

    char temp[512];
    int written = snprintf(temp, sizeof(temp),
                         "Account: %d\nName: %s\nType: %s\nBalance: %.2f\n\nTransactions:\n",
                         acc->acc_no, acc->name, acc->acc_type, acc->balance);

    for (size_t i = 0; i < acc->transaction_count && written < sizeof(temp); i++) {
        written += snprintf(temp + written, sizeof(temp) - written,
                          "%02d/%02d/%04d %-8s %.2f\n",
                          acc->history[i].when.day,
                          acc->history[i].when.month,
                          acc->history[i].when.year,
                          acc->history[i].type,
                          acc->history[i].amount);
    }

    if (written >= output_size) {
        return 0;
    }

    strncpy(output, temp, output_size);
    return 1;
}

struct account* get_account_info(int acc_no) {
    for (int i = 0; i < account_count; i++) {
        if (accounts[i].acc_no == acc_no) {
            return &accounts[i];
        }
    }
    return NULL;
}
