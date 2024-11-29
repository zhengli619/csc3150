#include <pthread.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>
#include <time.h>
#include <string.h>
#include <map>

#define ROW 17
#define COLUMN 49
#define HORI_LINE '-'
#define VERT_LINE '|'
#define CORNER '+'
#define PLAYER '0'
#define WALL '='
#define GOLD '$'

/* Global variables */
int player_x;  // Current x-coordinate of the player
int player_y;  // Current y-coordinate of the player
char map[ROW][COLUMN + 1];  // Map array, storing the current state of the game map

// 在代码中定义char map[ROW][COLUMN + 1]时，使用了COLUMN + 1而不是COLUMN，其原因主要是为了在每一行的末尾添加一个额外的字符位置，以存储字符串结束符 \0，
// 从而确保每一行的字符数组可以被正确地当作字符串处理。

// 具体原因如下：
// 字符串的结尾标识：在 C 语言中，字符串需要以 \0（null 终止符）结尾，才能被 puts()、printf() 等字符串处理函数正确识别和显示。
// 代码中的 map_print() 函数使用 puts(map[i]) 来逐行显示地图内容。puts()函数会读取字符串直到 \0为止。因此，确保每一行都以 \0结尾可以让 puts() 准确地按行输出地图。

// 在 C 语言中，如果直接用字符串字面量（例如 "helloworld"）来定义字符串，编译器会自动在末尾添加 \0 作为字符串的终止符，因此你不需要手动添加 \0。
// 例如：char str[] = "helloworld";
// 如果定义一个字符数组后逐个字符赋值，如 char str[11] = {'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd', '\0'};，则需要手动添加 \0。

// 对于字符串操作函数，如 puts()、printf()，它们会一直读取字符直到遇到 \0。没有 \0 会导致读取超出预期范围，可能输出其他内存中的内容，甚至导致程序崩溃。

//C++ 的 std::string 类更加强大且易于使用，不需要手动处理字符串大小，也自动管理字符串终止符。因此，使用 std::string 更符合现代 C++ 编程习惯。例如：
//#include <iostream>
//#include <string>
//int main() {
//     std::string str = "helloworld";  // 使用 std::string 定义字符串
//     std::cout << str << std::endl;   // 直接输出字符串
//     return 0;
// }



int wall_rows[6] = {2, 4, 6, 10, 12, 14}; // Row positions of the walls
int walls_positions[6];  // Current column positions of the walls

std::map<int, bool> gold_rows; 
// Row positions and status of the gold pieces, if certain gold pieces is got by player, the corresponding bool becomes false
int gold_positions[6];  // Current column positions of the gold pieces

int collected_gold = 0;  // Number of gold pieces collected by the player

enum GameStatus {
    ONGOING, 
    WIN,      
    LOSE,     
    QUIT      
};
GameStatus game_status; // To determine the status of game
pthread_mutex_t mtx;  // Mutex for thread synchronization

/* Function declarations */
int kbhit();
void map_print();
void* wall_thread_f(void* arg);
void* gold_thread_f(void* arg);
void* player_thread_f(void* arg);
int if_player_bump_into_wall();
int if_player_meet_gold();

/* Check if a key has been pressed */
int kbhit() {
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);  // Set terminal mode to no-buffering and no-echo
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

/* Print the map to the screen */
void map_print() {
    printf("\033[H\033[2J");  // Clear screen
    for (int i = 0; i <= ROW - 1; i++) {
        puts(map[i]);
    }
}

/* Wall thread, responsible for moving all walls */
void* wall_thread_f(void* arg) {
    int wall_rows[6] = {2, 4, 6, 10, 12, 14};
    int direction[6] = {1, -1, 1, -1, 1, -1};  // Determine movement direction of each wall

    while (game_status==ONGOING) {
        pthread_mutex_lock(&mtx);  // Lock to ensure thread safety
        for (int i = 0; i < 6; i++) {
            int row = wall_rows[i];  // Get wall row position from fixed array

            // Clear old wall position
            int start_col = walls_positions[i];
            for (int j = 0; j < 15; j++) {
                
                int col = (start_col + j) % COLUMN;
                if ((col != COLUMN-1 ) && (col != 0 )) {  // If col is at the boundary, do not clear it
                    map[row][col] = ' ';
                }
            }

            
            // Update wall position
            walls_positions[i] = (walls_positions[i] + direction[i] + COLUMN) % COLUMN;

            // Draw new wall position
            start_col = walls_positions[i];
            for (int j = 0; j < 15; j++) {
                int col = (start_col + j) % COLUMN;
                if ((col != COLUMN-1 ) && (col != 0 )) { // Do not draw element if exactly at boundary
                    map[row][col] = WALL;
                }
            }
        }
        map_print();
        pthread_mutex_unlock(&mtx);  // Unlock
        usleep(200000);  // Wall movement speed
    }
    return NULL;
}

/* Gold thread, responsible for moving all gold pieces */
void* gold_thread_f(void* arg) {
    int direction[6];
    for (int i = 0; i < 6; i++) {
        direction[i] = (rand() % 2 - 0.5) * 2;  // Randomly pick 1 or -1
    }
    while (game_status==ONGOING) {
        pthread_mutex_lock(&mtx);  // Lock to ensure thread safety
        int i = 0;
        for (std::map<int, bool>::iterator it = gold_rows.begin(); it != gold_rows.end(); ++it, ++i) {
            int row = it->first;
            if (!it->second) continue;  // Skip if gold piece has been collected by player

            // Clear old gold position
            if ((gold_positions[i] != COLUMN-1 ) && (gold_positions[i] != 0 )) {  // Do not clear if exactly at boundary, to avoid clearing boundary itself
                map[row][gold_positions[i]] = ' ';
            }

            // Update gold position
            gold_positions[i] = ((gold_positions[i] + direction[i]) + COLUMN) % COLUMN;

            // Draw new gold position
            if ((gold_positions[i] != COLUMN-1 ) && (gold_positions[i] != 0 )) {  // Do not draw new gold piece if at boundary
                map[row][gold_positions[i]] = GOLD;
            }
        }
        map_print();
        pthread_mutex_unlock(&mtx);  // Unlock
        usleep(200000);  // Gold movement speed
    }
    return NULL;
}

/* Player thread, responsible for handling player input and movement */
void* player_thread_f(void* arg) {
    while (game_status==ONGOING) {
        pthread_mutex_lock(&mtx);  // Lock to ensure thread safety

        if (kbhit()) {
            char move = getchar();
            printf("\033[1A\033[2K");  // Clear the input character
            map[player_x][player_y] = ' ';  // Clear the player's old position

            if (move == 'w' && player_x > 1) {
                player_x--;  // Move up (within boundary)
            }
            if (move == 's' && player_x < ROW - 2) {
                player_x++;  // Move down (within boundary)
            }
            if (move == 'a' && player_y > 1) {
                player_y--;  // Move left (within boundary)
            }
            if (move == 'd' && player_y < COLUMN - 2) {
                player_y++;  // Move right (within boundary)
            }
            if (move == 'q') {
                game_status = QUIT;
                
            }  // Exit game
        }
       
        if(if_player_bump_into_wall()){
            map[player_x][player_y] = PLAYER;  // Update the player's new position
            map_print();  // Print map
            printf("You lose the game\n");
        }

        else if (game_status==QUIT)
        {
            printf("You quit the game\n");
        }
        
        else{
            if (if_player_meet_gold()){
            
                collected_gold++;
                gold_rows[player_x] = false;
      
            }
            map[player_x][player_y] = PLAYER;  // Update the player's new position
            map_print();  // Print map

            if (collected_gold == 6) {  // Win the game if all gold pieces are collected
                    printf("You win the game\n");
                    game_status = WIN;
                    
                }
        }

        pthread_mutex_unlock(&mtx);  // Unlock
        usleep(10000);  // Control refresh rate of player movement
    }
    return NULL;
}



/* Function to check if player has collided with a wall */
int if_player_bump_into_wall() {
    for (int i = 0; i < 6; i++) {
        int wall_row = wall_rows[i];  // Get row of the wall
        int wall_start_col = walls_positions[i];
        for (int j = 0; j < 15; j++) {
            int wall_col = (wall_start_col + j) % COLUMN;
            if (player_x == wall_row && player_y == wall_col) {  // Player overlaps with wall
               
                game_status = LOSE;
                return 1;
            }
            else{
                
            }
        }
        
    }
    return 0;
}

int if_player_meet_gold() {
    // Iterate over all gold piece positions to check if player overlaps with any
    int i = 0;
    for (std::map<int, bool>::iterator it = gold_rows.begin(); it != gold_rows.end(); ++it, ++i) {
        int gold_row = it->first;
        int gold_col = gold_positions[i];
        
        // Check if gold piece is still on map (i.e., not collected by player)
        if (it->second) {  // If gold piece exists
            // If player position is same as gold piece
            if (player_x == gold_row && player_y == gold_col) {
                return gold_row;  // Return the row of the gold piece
            }
        }
    }
    return 0;  // No overlap with any gold piece
}

/* Main function to initialize the map and start threads */
int main() {
    //initialize the std::map<int,bool> gold_rows
    gold_rows[1] = true;
    gold_rows[3] = true;
    gold_rows[5] = true;
    gold_rows[11] = true;
    gold_rows[13] = true;
    gold_rows[15] = true;

    GameStatus game_status = ONGOING;  // initialliza the game_status 

    if (pthread_mutex_init(&mtx, NULL) != 0) {
        printf("Mutex init failed\n");
        return 1;
    }
    srand(time(0));  // Initialize random seed

    // Initialize map
    memset(map, ' ', sizeof(map));
    for (int j = 1; j <= COLUMN - 2; j++) {
        map[0][j] = HORI_LINE;  // Top boundary
        map[ROW - 1][j] = HORI_LINE;  // Bottom boundary
    }
    for (int i = 1; i <= ROW - 2; i++) {
        map[i][0] = VERT_LINE;  // Left boundary
        map[i][COLUMN - 1] = VERT_LINE;  // Right boundary
    }
    map[0][0] = CORNER;  // Top left corner
    map[0][COLUMN - 1] = CORNER;  // Top right corner
    map[ROW - 1][0] = CORNER;  // Bottom left corner
    map[ROW - 1][COLUMN - 1] = CORNER;  // Bottom right corner
    for (int i = 0; i < ROW; ++i) {
        map[i][COLUMN] = '\0';  // Ensure each row ends with a null character
    }

    player_x = 8;  // Player's initial row
    player_y = 24;  // Player's initial column
    map[player_x][player_y] = PLAYER;  // Mark player position on the map
    map_print(); // Build the initial map with only boundaries, no gold, walls, or player

    // Initialize positions of walls and gold pieces
    for (int i = 0; i < 6; i++) {
        walls_positions[i] = rand() % COLUMN;
        gold_positions[i] = rand() % COLUMN;
    }

    // Create and start threads
    pthread_t wall_thread;  // Wall thread
    pthread_create(&wall_thread, NULL, wall_thread_f, NULL);
    pthread_t gold_thread;  // Gold thread
    pthread_create(&gold_thread, NULL, gold_thread_f, NULL);
    pthread_t player_thread;  // Player thread
    pthread_create(&player_thread, NULL, player_thread_f, NULL);

    pthread_join(player_thread, NULL);  // Wait for player thread to end

    
    pthread_join(wall_thread, NULL);  // Wait for wall thread to end
    pthread_join(gold_thread, NULL);  // Wait for gold thread to end

    pthread_mutex_destroy(&mtx);  // Destroy the mutex
    pthread_exit(NULL);
}
