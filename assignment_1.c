#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>


//#define PASSWORD "assignment"
#define FILENAME "accounts.dat"
#define MIN_BALANCE 1000
#define MIN_TRANSACTION 500
#define MAX_TRANSACTIONS 5

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

FILE *fp;


void clear_screen();
void delay(int sec);
int is_unique_account(int acc_no);
int generate_account_number();
int generate_pin();
void add_transaction(struct account *acc, const char *type, float amount);
void create_account();
void delete_account();
int authenticate();
void deposit();
void withdraw();
void view_statement();
void view_balance();
void main_menu();


int main() {
   
    srand(time(0));

    fp = fopen(FILENAME, "rb+");
    if (fp == NULL) {
        fp = fopen(FILENAME, "wb+");
        if (fp == NULL) {
            printf("Cannot open data file!\n");
            return 1;
        }
    }


    clear_screen();
     main_menu();

}

/* MAIN MENU */
void main_menu() {
    int choice;
    do {
        clear_screen();
        printf("\n=== BANKING SYSTEM ===\n\n");
        printf("1. Create Account\n");
        printf("2. Deposit\n");
        printf("3. Withdraw\n");
        printf("4. View Statement\n");
        printf("5. View Balance\n");
        printf("6. Delete account\n");
        printf("7. Exit\n\n");
        printf("Enter choice: ");
        scanf("%d", &choice);

        switch(choice) {
            case 1: create_account(); break;
            case 2: deposit(); break;
            case 3: withdraw(); break;
            case 4: view_statement(); break;
            case 5: view_balance(); break;
            case 6: delete_account(); break;
            case 7: break;
            default: printf("Invalid choice!\n"); delay(1);
        }
    } while (choice != 5);
}

/* ACCOUNT OPERATIONS */
void create_account() {
    struct account new_acc;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);



        clear_screen();
    printf("\n=== CREATE NEW ACCOUNT ===\n\n");

    printf("Enter customer name: ");
    scanf(" %[^\n]", new_acc.name);

    printf("Account type (savings/current): ");
    scanf("%9s", new_acc.acc_type);

    printf("Initial deposit (minimum %d): ", MIN_BALANCE);
    float deposit_amount;
    do {
        scanf("%f", &deposit_amount);
        if (deposit_amount < MIN_BALANCE) {
            printf("Minimum deposit is %d. Enter again: ", MIN_BALANCE);
        }
    } while (deposit_amount < MIN_BALANCE);

    new_acc.acc_no = generate_account_number();
    new_acc.pin = generate_pin();
    new_acc.balance = deposit_amount;
    new_acc.created.day = tm->tm_mday;
    new_acc.created.month = tm->tm_mon + 1;
    new_acc.created.year = tm->tm_year + 1900;
    new_acc.transaction_count = 0;

    add_transaction(&new_acc, "Deposit", deposit_amount);

    fseek(fp, 0, SEEK_END);
    fwrite(&new_acc, sizeof(new_acc), 1, fp);

    printf("\nAccount created successfully!\n");
    printf("Account Number: %d\n", new_acc.acc_no);
    printf("PIN: %d (Keep this secure)\n", new_acc.pin);

    printf("\nPress Enter to continue...");
    while (getchar() != '\n');
    getchar();


}

void delete_account() {
    struct account del_acc;
    int acc_no = authenticate();

    if (acc_no == 0) {
        printf("\nInvalid account number or PIN!\n");
        delay(2);
        return;
    }

    clear_screen();
    printf("\n========DELETING ACCOUNT========\n");
    printf("Are you sure you want to delete account number %d? (y/n): ", acc_no);
    char confirmation;
    scanf(" %c", &confirmation);

    if (confirmation != 'y' && confirmation != 'Y') {
        printf("\nAccount deletion cancelled.\n");
        delay(1);
        return;
    }

    FILE *temp_fp = fopen("temp_accounts.dat", "wb");
    if (temp_fp == NULL) {
        perror("Error creating temporary file");
        delay(2);
        return;
    }

    fseek(fp, 0, SEEK_SET);
    int found = 0;
    while (fread(&del_acc, sizeof(struct account), 1, fp) == 1) {
        if (acc_no != del_acc.acc_no) {
            fwrite(&del_acc, sizeof(struct account), 1, temp_fp);
        } else {
            found = 1;
            printf("\nAccount number %d deleted successfully.\n", acc_no);
        }
    }

    fclose(fp);
    fclose(temp_fp);


    if (rename("temp_accounts.dat", "accounts.dat") == 0) {
        fp = fopen("accounts.dat", "r+b");
        if (fp == NULL) {
            perror("Error re-opening accounts file");

        }
    } else {
        perror("Error renaming temporary file");
    }

    if (!found) {
        printf("\nAccount number %d not found.\n", acc_no);
    }

    delay(2);
}


/* AUTHENTICATION */
int authenticate() {
    int acc_no, pin;
    clear_screen();
    printf("\n=== ACCOUNT LOGIN ===\n\n");

    printf("Enter account number: ");
    scanf("%d", &acc_no);

    printf("Enter PIN: ");
    scanf("%d", &pin);

    struct account ac;
    rewind(fp);
    while (fread(&ac, sizeof(ac), 1, fp)) {
        if (ac.acc_no == acc_no && ac.pin == pin) {
            return acc_no;
        }
    }
    return 0;
}


/* TRANSACTION FUNCTIONS */
void deposit() {
    int acc_no = authenticate();
    if (acc_no == 0) {
        printf("\nInvalid account number or PIN!\n");
        delay(2);
        return;
    }

    clear_screen();
    printf("\n=== DEPOSIT ===\n\n");

    struct account ac;
    long pos;
    rewind(fp);
    while (fread(&ac, sizeof(ac), 1, fp)) {
        pos = ftell(fp);
        if (ac.acc_no == acc_no) {
            printf("Current balance: %.2f\n", ac.balance);

            float amount;
            printf("Enter deposit amount (minimum %d): ", MIN_TRANSACTION);
            do {
                scanf("%f", &amount);
                if (amount < MIN_TRANSACTION) {
                    printf("Minimum deposit is %d. Enter again: ", MIN_TRANSACTION);
                }
            } while (amount < MIN_TRANSACTION);

            ac.balance += amount;
            add_transaction(&ac, "Deposit", amount);

            fseek(fp, pos - sizeof(ac), SEEK_SET);
            fwrite(&ac, sizeof(ac), 1, fp);

            printf("\nDeposit successful! New balance: %.2f\n", ac.balance);
            break;
        }
    }

    printf("\nPress Enter to continue...");
    while (getchar() != '\n');
    getchar();
}

void withdraw() {
    int acc_no = authenticate();
    if (acc_no == 0) {
        printf("\nInvalid account number or PIN!\n");
        delay(2);
        return;
    }

    clear_screen();
    printf("\n=== WITHDRAW ===\n\n");

    struct account ac;
    long pos;
    rewind(fp);
    while (fread(&ac, sizeof(ac), 1, fp)) {
        pos = ftell(fp);
        if (ac.acc_no == acc_no) {
            printf("Current balance: %.2f\n", ac.balance);

            float amount;
            printf("Enter withdrawal amount (minimum %d): ", MIN_TRANSACTION);
            do {
                scanf("%f", &amount);
                if (amount < MIN_TRANSACTION) {
                    printf("Minimum withdrawal is %d. Enter again: ", MIN_TRANSACTION);
                } else if (ac.balance - amount < MIN_BALANCE) {
                    printf("Cannot withdraw. Minimum balance of %d must remain. Enter again: ", MIN_BALANCE);
                    amount = -1; // Force retry
                }
            } while (amount < MIN_TRANSACTION || amount == -1);

            ac.balance -= amount;
            add_transaction(&ac, "Withdraw", amount);

            fseek(fp, pos - sizeof(ac), SEEK_SET);
            fwrite(&ac, sizeof(ac), 1, fp);

            printf("\nWithdrawal successful! New balance: %.2f\n", ac.balance);
            break;
        }
    }

    printf("\nPress Enter to continue...");
    while (getchar() != '\n');
    getchar();
}

void view_statement() {
    int acc_no = authenticate();
    if (acc_no == 0) {
        printf("\nInvalid account number or PIN!\n");
        delay(2);
        return;
    }

    clear_screen();
    printf("\n=== ACCOUNT STATEMENT ===\n\n");

    struct account ac;
    rewind(fp);
    while (fread(&ac, sizeof(ac), 1, fp)) {
        if (ac.acc_no == acc_no) {
            printf("Account: %d\n", ac.acc_no);
            printf("Name: %s\n", ac.name);
            printf("Type: %s\n", ac.acc_type);
            printf("Balance: %.2f\n\n", ac.balance);

            printf("Last %d transactions:\n",
                  ac.transaction_count < MAX_TRANSACTIONS ? ac.transaction_count : MAX_TRANSACTIONS);
            printf("----------------------------------------\n");
            printf("Date       | Type     | Amount\n");
            printf("----------------------------------------\n");

            int start = ac.transaction_count >= MAX_TRANSACTIONS ?
                      ac.transaction_count % MAX_TRANSACTIONS : 0;
            int count = 0;

            for (int i = 0; i < MAX_TRANSACTIONS && count < ac.transaction_count; i++) {
                int idx = (start + i) % MAX_TRANSACTIONS;
                if (ac.history[idx].amount > 0) {
                    printf("%02d/%02d/%04d | %-8s | %.2f\n",
                          ac.history[idx].when.day,
                          ac.history[idx].when.month,
                          ac.history[idx].when.year,
                          ac.history[idx].type,
                          ac.history[idx].amount);
                    count++;
                }
            }
            break;
        }
    }

    printf("\nPress Enter to continue...");
    while (getchar() != '\n');
    getchar();
}

void view_balance(){
    int acc_no = authenticate();
    if (acc_no == 0) {
        printf("\nInvalid account number or PIN!\n");
        delay(2);
        return;
    }

    clear_screen();
    printf("=======BALANCE=======\n");

    struct account balance;
    int found = 0;

    fseek(fp, 0, SEEK_SET);
    while (fread(&balance, sizeof(struct account), 1, fp) == 1) {
        if (balance.acc_no == acc_no) {
            printf("\n\nCurrent balance: %.2f\n", balance.balance);
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("\nAccount number %d not found.\n", acc_no);
        delay(2);
    }

    printf("\nPress Enter to continue...");
    while (getchar() != '\n');
    getchar();
}
/* HELPER FUNCTIONS */
void clear_screen() {
    system("clear || cls");
}

void delay(int sec) {
    clock_t start = clock();
    while ((clock() - start) / CLOCKS_PER_SEC < sec);
}

int generate_account_number() {
    int acc_no;
    do {
        acc_no = 100000 + rand() % 900000;
    } while (!is_unique_account(acc_no));
    return acc_no;
}

int generate_pin() {
    return 1000 + rand() % 9000;
}

int is_unique_account(int acc_no) {
    struct account ac;
    rewind(fp);
    while (fread(&ac, sizeof(ac), 1, fp)) {
        if (ac.acc_no == acc_no) return 0;
    }
    return 1;
}


void add_transaction(struct account *acc, const char *type, float amount) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    int idx = acc->transaction_count % MAX_TRANSACTIONS;

    acc->history[idx].when.day = tm->tm_mday;
    acc->history[idx].when.month = tm->tm_mon + 1;
    acc->history[idx].when.year = tm->tm_year + 1900;
    strcpy(acc->history[idx].type, type);
    acc->history[idx].amount = amount;

    acc->transaction_count++;
}
