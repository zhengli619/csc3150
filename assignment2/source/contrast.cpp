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

/* 全局变量 */
int player_x;  // 玩家当前的 x 坐标
int player_y;  // 玩家当前的 y 坐标
char map[ROW][COLUMN + 1];  // 地图数组，存储当前游戏地图的状态

int wall_rows[6] = {2, 4, 6, 10, 12, 14}; //墙壁的行位置
int walls_positions[6];  // 墙壁的当前列位置

std::map<int, bool> gold_rows = { {1, true}, {3, true}, {5, true}, \
{11, true}, {13, true}, {15, true} };  // 金块的行位置和状态
int gold_positions[6];  // 金块的当前列位置

int collected_gold = 0;  // 玩家收集到的金块数量
bool game_over = false;  // 游戏是否结束的标志
pthread_mutex_t mtx;  // 互斥锁，用于线程同步

/* 函数声明 */
int kbhit();
void map_print();
void* wall_thread_f(void* arg);
void* gold_thread_f(void* arg);
void* player_thread_f(void* arg);
int if_player_bump_into_wall();
int if_player_meet_gold();

/* 检测键盘是否有按键被按下 */
int kbhit() {
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);  // 设置终端模式为无缓冲和不回显
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

/* 打印地图到屏幕上 */
void map_print() {
    printf("\033[H\033[2J");  // 清屏
    for (int i = 0; i <= ROW - 1; i++) {
        puts(map[i]);
    }
}

/* 墙壁线程，负责移动所有的墙壁 */
void* wall_thread_f(void* arg) {
    int wall_rows[6] = {2, 4, 6, 10, 12, 14};
    int direction[6] = {1, -1, 1, -1, 1, -1};  // 确定每个墙壁的移动方向

    while (!game_over) {
        pthread_mutex_lock(&mtx);  // 加锁，确保线程安全
        for (int i = 0; i < 6; i++) {
            int row = wall_rows[i];  // 从固定行位置数组中获取墙壁的行位置

            // 清除旧的墙壁位置
            int start_col = walls_positions[i];
            for (int j = 0; j < 15; j++) {
                
                int col = (start_col + j) % COLUMN;
                if ((col != COLUMN-1 ) && (col != 0 )){     //如果col在两侧围栏处，则不要清除
                    map[row][col] = ' ';
                }
            }

            
            // 更新墙壁位置
            walls_positions[i] = (walls_positions[i] + direction[i] + COLUMN) % COLUMN;

            // 绘制新的墙壁位置
            start_col = walls_positions[i];
            for (int j = 0; j < 15; j++) {
                int col = (start_col + j) % COLUMN;
                if ((col != COLUMN-1 ) && (col != 0 )){ //如果正好处于两侧围栏处，则不要绘制这个元素
                    map[row][col] = WALL;
                }
            }
        }
        map_print();
        pthread_mutex_unlock(&mtx);  // 解锁
        usleep(200000);  // 墙壁移动的速度
    }
    return NULL;
}

/* 金块线程，负责移动所有的金块 */
void* gold_thread_f(void* arg) {
    int direction[6];
    for (int i = 0; i < 6; i++) {
        direction[i] = (rand() % 2 - 0.5) * 2;  // randomly pick 1 or -1
    }
    while (!game_over) {
        pthread_mutex_lock(&mtx);  // 加锁，确保线程安全
        int i = 0;
        for (std::map<int, bool>::iterator it = gold_rows.begin(); it != gold_rows.end(); ++it, ++i) {
            int row = it->first;
            if (!it->second) continue;  // 如果金块已经被玩家拿走，跳过该行

            // 清除旧的金块位置
            if ((gold_positions[i] != COLUMN-1 ) && (gold_positions[i] != 0 )) {  // 如果正好处于两侧围栏处，则不要清除，否则会把围栏清除
                map[row][gold_positions[i]] = ' ';
            }

            // 更新金块位置
            gold_positions[i] = ((gold_positions[i] + direction[i]) + COLUMN) % COLUMN;

            // 绘制新的金块位置
            if ((gold_positions[i] != COLUMN-1 ) && (gold_positions[i] != 0 )) {  // 如果正好处于两侧围栏处，则不要绘制新的金块
                map[row][gold_positions[i]] = GOLD;
            }
        }
        map_print();
        pthread_mutex_unlock(&mtx);  // 解锁
        usleep(200000);  // 金块移动的速度
    }
    return NULL;
}

/* 玩家线程，负责处理玩家输入并移动玩家 */
void* player_thread_f(void* arg) {
    while (!game_over) {
        pthread_mutex_lock(&mtx);  // 加锁，确保线程安全

        if (kbhit()) {
            char move = getchar();
            printf("\033[1A\033[2K");  // 清除输入的字符
            map[player_x][player_y] = ' ';  // 清除玩家的旧位置

            if (move == 'w' && player_x > 1) {
                player_x--;  // 向上移动（不越过边界）
            }
            if (move == 's' && player_x < ROW - 2) {
                player_x++;  // 向下移动（不越过边界）
            }
            if (move == 'a' && player_y > 1) {
                player_y--;  // 向左移动（不越过边界）
            }
            if (move == 'd' && player_y < COLUMN - 2) {
                player_y++;  // 向右移动（不越过边界）
            }
            if (move == 'q') {
                game_over = true;
                printf("You exit the game\n");
            }  // 退出游戏
        }
       
        if(if_player_bump_into_wall()){
            map[player_x][player_y] = PLAYER;  // 更新玩家的新位置
            map_print();  // 打印地图
            printf("You lose the game\n");
        }
        else{
            if (if_player_meet_gold()){
            
                collected_gold++;
                gold_rows[player_x] = false;
      
            }
            map[player_x][player_y] = PLAYER;  // 更新玩家的新位置
            map_print();  // 打印地图

            if (collected_gold == 6) {  // 收集所有金块，游戏胜利
                    printf("You win the game\n");
                    game_over = true;
                    
                }
        }

        pthread_mutex_unlock(&mtx);  // 解锁
        usleep(1000);  // 控制玩家移动的刷新速度
    }
    return NULL;
}



/* 检查玩家是否与墙壁接触的函数 */
int if_player_bump_into_wall() {
    for (int i = 0; i < 6; i++) {
        int wall_row = wall_rows[i];  // 获取墙壁所在的行
        int wall_start_col = walls_positions[i];
        for (int j = 0; j < 15; j++) {
            int wall_col = (wall_start_col + j) % COLUMN;
            if (player_x == wall_row && player_y == wall_col) {  // 玩家与墙壁重叠
               
                game_over = true;
                return 1;
            }
            else{
                
            }
        }
        
    }
    return 0;
}

int if_player_meet_gold() {
    // 遍历所有的金块位置，检查玩家是否与金块重叠
    int i = 0;
    for (std::map<int, bool>::iterator it = gold_rows.begin(); it != gold_rows.end(); ++it, ++i) {
        int gold_row = it->first;
        int gold_col = gold_positions[i];
        
        // 检查金块是否仍在地图上（即没有被玩家收集）
        if (it->second) {  // 如果金块存在
            // 如果玩家的位置与金块的位置相同
            if (player_x == gold_row && player_y == gold_col) {
                return gold_row;  // 返回金块所在的行号
            }
        }
    }
    return 0;  // 玩家没有与任何金块重叠
}

/* 主函数，初始化地图和启动线程 */
int main() {
    if (pthread_mutex_init(&mtx, NULL) != 0) {
        printf("Mutex init failed\n");
        return 1;
    }
    srand(time(0));  // 初始化随机数种子

    // 初始化地图
    memset(map, ' ', sizeof(map));
    for (int j = 1; j <= COLUMN - 2; j++) {
        map[0][j] = HORI_LINE;  // 上边界
        map[ROW - 1][j] = HORI_LINE;  // 下边界
    }
    for (int i = 1; i <= ROW - 2; i++) {
        map[i][0] = VERT_LINE;  // 左边界
        map[i][COLUMN - 1] = VERT_LINE;  // 右边界
    }
    map[0][0] = CORNER;  // 左上角
    map[0][COLUMN - 1] = CORNER;  // 右上角
    map[ROW - 1][0] = CORNER;  // 左下角
    map[ROW - 1][COLUMN - 1] = CORNER;  // 右下角
    for (int i = 0; i < ROW; ++i) {
        map[i][COLUMN] = '\0';  // 确保每一行以空字符结尾
    }

    player_x = 8;  // 玩家初始位置（行）
    player_y = 24;  // 玩家初始位置（列）
    map[player_x][player_y] = PLAYER;  // 在地图上标记玩家位置
    map_print(); //构建初始地图，只有围栏，没有金块和墙和玩家

    // 初始化墙壁和金块的位置
    for (int i = 0; i < 6; i++) {
        walls_positions[i] = rand() % COLUMN;
        gold_positions[i] = rand() % COLUMN;
    }

    // 创建并启动线程
    pthread_t wall_thread;  // 墙壁线程
    pthread_create(&wall_thread, NULL, wall_thread_f, NULL);
    pthread_t gold_thread;  // 金块线程
    pthread_create(&gold_thread, NULL, gold_thread_f, NULL);
    pthread_t player_thread;  // 玩家线程
    pthread_create(&player_thread, NULL, player_thread_f, NULL);

    pthread_join(player_thread, NULL);  // 等待玩家线程结束

    game_over = true;  // 设置游戏结束标志
    pthread_join(wall_thread, NULL);  // 等待墙壁线程结束
    pthread_join(gold_thread, NULL);  // 等待金块线程结束

    pthread_mutex_destroy(&mtx);  // 销毁互斥锁
    pthread_exit(NULL);
}

