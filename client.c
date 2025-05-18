#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <time.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8081
#define BUFFER_SIZE 1024
#define MIN_BALANCE 1000
#define MIN_TRANSACTION 500

void clear_screen();
void send_command(int sockfd, struct sockaddr_in *servaddr, char *command, char *response);

int main() {
    int sockfd;
    struct sockaddr_in servaddr;
    int choice, acc_no = 0, pin = 0;
    char command[BUFFER_SIZE], response[BUFFER_SIZE];
    char name[60], acc_type[10];
    float amount;

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    while (1) {
        clear_screen();
        printf("\n=== BANKING SYSTEM ===\n\n");
        
        if (acc_no == 0) {
            printf("NOT LOGGED IN\n\n");
        } else {
            printf("Logged in as account: %d\n\n", acc_no);
        }
        
        printf("1. Create Account\n");
        printf("2. Deposit\n");
        printf("3. Withdraw\n");
        printf("4. View Statement\n");
        printf("5. View Balance\n");
        printf("6. Delete account\n");
        printf("7. Exit\n\n");
        printf("Enter choice: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1: // Create Account
                clear_screen();
                printf("=== CREATE ACCOUNT ===\n\n");
                
                printf("Enter your name: ");
                scanf(" %[^\n]", name);

                do {
                    printf("Account type (savings/current): ");
                    scanf("%9s", acc_type);
                    if (strcmp(acc_type, "savings") != 0 && strcmp(acc_type, "current") != 0) {
                        printf("Invalid account type. Please enter 'savings' or 'current'\n");
                    }
                } while (strcmp(acc_type, "savings") != 0 && strcmp(acc_type, "current") != 0);

                do {
                    printf("Initial deposit (minimum %d): ", MIN_BALANCE);
                    scanf("%f", &amount);
                    if (amount < MIN_BALANCE) {
                        printf("Minimum deposit is %d\n", MIN_BALANCE);
                    }
                } while (amount < MIN_BALANCE);

                snprintf(command, BUFFER_SIZE, "CREATE %s %s %.2f", name, acc_type, amount);
                send_command(sockfd, &servaddr, command, response);

                if (strncmp(response, "SUCCESS", 7) == 0) {
				// Parse both numbers from the formatted response
				if (sscanf(response, "SUCCESS Account created\nAccount Number: %d\nPIN: %d", &acc_no, &pin) == 2) {
					printf("\nAccount created successfully!\n");
					printf("Your account number is: %d\n", acc_no);
					printf("Your PIN is: %04d (Keep this secure)\n", pin);  // %04d ensures 4-digit format
					} else {
					printf("\nError parsing account details. Full response:\n%s\n", response);
					}
				} else {
				// Error case
				printf("\n%s\n", response + (strncmp(response, "ERROR", 5) == 0 ? 6 : 0));
				}
                break;

            case 2: // Deposit
                if (acc_no == 0) {
                    printf("\nPlease login first (use option 1 to create an account)\n");
                    break;
                }
                
                clear_screen();
                printf("=== DEPOSIT ===\n\n");

                do {
                    printf("Enter amount to deposit (min %d): ", MIN_TRANSACTION);
                    scanf("%f", &amount);
                    if (amount < MIN_TRANSACTION) {
                        printf("Amount must be at least %d\n", MIN_TRANSACTION);
                    }
                } while (amount < MIN_TRANSACTION);

                snprintf(command, BUFFER_SIZE, "DEPOSIT %d %.2f", acc_no, amount);
                send_command(sockfd, &servaddr, command, response);

                if (strncmp(response, "SUCCESS", 7) == 0) {
                    float balance;
                    sscanf(response + 8, "%f", &balance);
                    printf("\nDeposit successful! New balance: %.2f\n", balance);
                } else {
                    printf("\n%s\n", response + 6);
                }
                break;

            case 3: // Withdraw
                if (acc_no == 0) {
                    printf("\nPlease login first (use option 1 to create an account)\n");
                    break;
                }
                
                clear_screen();
                printf("=== WITHDRAW ===\n\n");

                do {
                    printf("Enter amount to withdraw (min %d): ", MIN_TRANSACTION);
                    scanf("%f", &amount);
                    if (amount < MIN_TRANSACTION) {
                        printf("Amount must be at least %d\n", MIN_TRANSACTION);
                    }
                } while (amount < MIN_TRANSACTION);

                snprintf(command, BUFFER_SIZE, "WITHDRAW %d %.2f", acc_no, amount);
                send_command(sockfd, &servaddr, command, response);

                if (strncmp(response, "SUCCESS", 7) == 0) {
                    float balance;
                    sscanf(response + 8, "%f", &balance);
                    printf("\nWithdrawal successful! New balance: %.2f\n", balance);
                } else {
                    printf("\n%s\n", response + 6);
                }
                break;

            case 4: // View Statement
                if (acc_no == 0) {
                    printf("\nPlease login first (use option 1 to create an account)\n");
                    break;
                }
                
                clear_screen();
                printf("=== ACCOUNT STATEMENT ===\n\n");

                snprintf(command, BUFFER_SIZE, "STATEMENT %d", acc_no);
                send_command(sockfd, &servaddr, command, response);

                if (strncmp(response, "SUCCESS", 7) == 0) {
                    printf("Account Statement:\n%s\n", response + 8);
                } else {
                    printf("\n%s\n", response + 6);
                }
                break;

            case 5: // View Balance
                if (acc_no == 0) {
                    printf("\nPlease login first (use option 1 to create an account)\n");
                    break;
                }
                
                clear_screen();
                printf("=== ACCOUNT BALANCE ===\n\n");

                snprintf(command, BUFFER_SIZE, "BALANCE %d", acc_no);
                send_command(sockfd, &servaddr, command, response);

                if (strncmp(response, "SUCCESS", 7) == 0) {
                    printf("Account Balance: %s\n", response + 8);
                } else {
                    printf("\n%s\n", response + 6);
                }
                break;

            case 6: // Delete Account
                if (acc_no == 0) {
                    printf("\nPlease login first (use option 1 to create an account)\n");
                    break;
                }
                
                clear_screen();
                printf("=== DELETE ACCOUNT ===\n\n");
                printf("Are you sure you want to delete your account? (y/n): ");
                
                char confirm;
                scanf(" %c", &confirm);
                if (confirm == 'y' || confirm == 'Y') {
                    snprintf(command, BUFFER_SIZE, "DELETE %d", acc_no);
                    send_command(sockfd, &servaddr, command, response);

                    if (strncmp(response, "SUCCESS", 7) == 0) {
                        printf("\n%s\n", response + 8);
                        acc_no = 0; // Log out after deletion
                    } else {
                        printf("\n%s\n", response + 6);
                    }
                } else {
                    printf("\nAccount deletion cancelled.\n");
                }
                break;

            case 7: // Exit
                close(sockfd);
                exit(0);

            default:
                printf("Invalid choice!\n");
        }

        // Pause before returning to menu
        printf("\nPress Enter to continue...");
        while (getchar() != '\n');
        getchar();
    }
}

void clear_screen() {
    system("clear || cls");
}

void send_command(int sockfd, struct sockaddr_in *servaddr, char *command, char *response) {
    socklen_t serv_len = sizeof(*servaddr);
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Send the command to the server
    if (sendto(sockfd, command, strlen(command), 0,
               (const struct sockaddr *)servaddr, serv_len) < 0) {
        perror("sendto");
        strcpy(response, "ERROR: Failed to send command to server");
        return;
    }

    // Wait for the response
    int n = recvfrom(sockfd, response, BUFFER_SIZE - 1, 0,
                     (struct sockaddr *)&from_addr, &from_len);
    if (n < 0) {
        perror("recvfrom");
        strcpy(response, "ERROR: Timeout or failed to receive server response");
        return;
    }

    // Null-terminate the response
    response[n] = '\0';
}
