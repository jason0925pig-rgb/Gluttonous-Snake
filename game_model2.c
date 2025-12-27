#include <ncurses.h>    // 终端图形库
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>

/* -------------- 老师 starter code 里的宏（可以改数值，不改名字） -------------- */

#define BOARD_ROWS 20   // 棋盘行数（包含边框）
#define BOARD_COLS 50   // 棋盘列数（包含边框）
#define MAX_NAME  20

#define ROBOT_BODY 'O'
#define ROBOT_HEAD '^'
#define PERSON     'P'
#define MINE       'X'
#define OBSTACLE   '#'

/* -------------- 游戏规则相关常量 -------------- */

#define INITIAL_LIVES      3
#define PEOPLE_PER_LEVEL   5      // 每救 5 个人升一级
#define MAX_MINES          50
#define BASE_MINES         5      // 初始地雷数
#define MINES_PER_LEVEL    2      // 每升一级新增地雷数

#define BASE_DELAY_MS      200    // 初始刷新延迟（毫秒）
#define LEVEL_SPEEDUP_MS   20     // 每升一级减小的延迟
#define MIN_DELAY_MS       60     // 最快速度

#define INVINCIBLE_TICKS   10     // 死亡后短暂无敌步数
#define LEADERBOARD_FILE   "leaderboard.txt"

/* -------------- 结构体定义（与老师一致/兼容） -------------- */

typedef struct {
    int x;  // 列（1 ~ BOARD_COLS-2）
    int y;  // 行（1 ~ BOARD_ROWS-2）
} Position;

typedef struct {
    Position pos;      // 机器人头部位置
    char direction;    // 'N','S','E','W'
    bool ai_mode;      // true = AI 模式，false = 手动模式
    bool invincible;   // 是否处于无敌状态
    int  invincible_ticks;
} Robot;

typedef struct {
    char name[MAX_NAME + 1];
    int  score;
    int  lives;
    int  level;
    int  rescued;      // 当前等级已经救的人数
} Player;

typedef struct {
    int width;
    int height;
    int center_x;      // 十字架中心在棋盘中的 x（列）
    int center_y;      // 十字架中心在棋盘中的 y（行）
} CrossObstacle;

typedef struct {
    char name[MAX_NAME + 1];
    int  score;
} LeaderboardEntry;

/* -------------- 颜色相关 -------------- */

void init_colors(void) {
    if (!has_colors()) return;
    start_color();
    // 1: 机器人（白）
    init_pair(1, COLOR_WHITE,  COLOR_BLACK);
    // 2: 人（绿）
    init_pair(2, COLOR_GREEN,  COLOR_BLACK);
    // 3: 地雷（红）
    init_pair(3, COLOR_RED,    COLOR_BLACK);
    // 4: 障碍物（黄）
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
    // 5: 状态栏文字（青）
    init_pair(5, COLOR_CYAN,   COLOR_BLACK);
}

/* -------------- 一些函数声明（对应老师 starter code 风格） -------------- */

void draw_title_screen(Player *player);

WINDOW* init_game(Player *player,
                  Robot  *robot,
                  Position *person,
                  Position *mines,
                  int mine_count,
                  CrossObstacle *obstacle);

void update_UI(const Player *player, const Robot *robot);

void handle_input(Robot *robot, int input, bool *running);

void move_robot(Robot *robot);
void move_robot_ai(Robot *robot, const Position *person,
                   const Position *mines, int mine_count);

void clear_robot(WINDOW *board, const Robot *robot);
void draw_robot (WINDOW *board, const Robot *robot);

void check_collision(Player *player, Robot *robot,
                     const Position *mines, int mine_count,
                     const CrossObstacle *obstacle,
                     bool *running);

void spawn_person(const Robot *robot, Position *person,
                  const Position *mines, int mine_count,
                  const CrossObstacle *obstacle);

void spawn_mines(const Robot *robot, const Position *person,
                 Position *mines, int *mine_count,
                 int target_count,
                 const CrossObstacle *obstacle);

void game_over_screen(const Player *player);
void save_score(const Player *player);
void show_leaderboard(void);

void init_obstacle(CrossObstacle *obstacle);
void draw_obstacle(WINDOW *board, const CrossObstacle *obstacle);
bool is_obstacle_position(const CrossObstacle *obstacle, int x, int y);

int  get_delay_for_level(int level);
bool is_mine_at(const Position *mines, int mine_count, int x, int y);

/* -------------- 工具函数：方向 -> (dx,dy) -------------- */

void set_direction(Robot *robot, char dir) {
    robot->direction = dir;
}

/* 根据机器人当前方向算出 dx, dy */
void direction_to_delta(char dir, int *dx, int *dy) {
    *dx = 0; *dy = 0;
    switch (dir) {
        case 'N': *dx = 0;  *dy = -1; break;
        case 'S': *dx = 0;  *dy = 1;  break;
        case 'W': *dx = -1; *dy = 0;  break;
        case 'E': *dx = 1;  *dy = 0;  break;
        default:  *dx = 0;  *dy = 0;  break;
    }
}

/* -------------- 标题界面：输入名字、说明 -------------- */

void draw_title_screen(Player *player) {
    // 标题界面里要用“阻塞输入”，否则 getnstr 会读不全
    nodelay(stdscr, FALSE);

    clear();
    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);

    const char *title = "Rescue Bot: Minesweeper Snake";
    mvprintw(3, (xmax - (int)strlen(title)) / 2, "%s", title);

    mvprintw(5, 4, "Description:");
    mvprintw(6, 6, "Control a small robot to rescue people on a minefield.");
    mvprintw(7, 6, "Avoid walls, mines and the central obstacle.");
    mvprintw(8, 6, "Each rescued person increases your score.");
    mvprintw(9, 6, "Every %d people rescued: level up (faster, more mines).",
             PEOPLE_PER_LEVEL);

    mvprintw(11, 4, "Controls:");
    mvprintw(12, 6, "Arrow keys or WASD: move robot (manual mode)");
    mvprintw(13, 6, "'m'              : toggle Manual/AI mode");
    mvprintw(14, 6, "'q'              : quit game");

    mvprintw(16, 4,
             "Enter your name (max %d chars) and press ENTER:", MAX_NAME);
    mvprintw(17, 4, "> ");
    move(17, 6);  // 光标移动到 "> " 后面

    echo();
    curs_set(1);

    char buf[MAX_NAME + 1];
    memset(buf, 0, sizeof(buf));
    getnstr(buf, MAX_NAME);   // 这里会一直等你输完并按回车

    noecho();
    curs_set(0);

    if (strlen(buf) == 0) {
        strcpy(player->name, "Player");
    } else {
        strncpy(player->name, buf, MAX_NAME);
        player->name[MAX_NAME] = '\0';
    }

    mvprintw(19, 4, "Welcome, %s! Press any key to start...", player->name);
    refresh();
    getch();   // 等你按任意键再开始游戏

    // 游戏主循环需要非阻塞输入，再切回去
    nodelay(stdscr, TRUE);
}


/* -------------- 初始化棋盘窗口 + 机器人 + 障碍物 -------------- */

WINDOW* init_game(Player *player,
                  Robot  *robot,
                  Position *person,
                  Position *mines,
                  int mine_count,
                  CrossObstacle *obstacle) {
    (void)player;   // 当前这个函数里没用到，用 (void) 防止编译器警告
    (void)person;
    (void)mines;
    (void)mine_count;

    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);

    int start_y = (ymax - BOARD_ROWS) / 2;
    int start_x = (xmax - BOARD_COLS) / 2;

    WINDOW *board = newwin(BOARD_ROWS, BOARD_COLS, start_y, start_x);
    box(board, 0, 0);

    // 初始化机器人：大概在棋盘中间偏下一点
    robot->pos.x = BOARD_COLS / 2;
    robot->pos.y = BOARD_ROWS / 2 + 3;
    if (robot->pos.y >= BOARD_ROWS - 1) robot->pos.y = BOARD_ROWS / 2;
    robot->ai_mode = true;
    robot->invincible = false;
    robot->invincible_ticks = 0;
    set_direction(robot, 'W');  // 初始向左

    // 初始化十字架障碍物
    init_obstacle(obstacle);

    wrefresh(board);
    return board;
}

/* 设置十字架障碍物尺寸和中心 */
void init_obstacle(CrossObstacle *obstacle) {
    obstacle->width    = 11;                   // 十字架宽度（水平臂总长）
    obstacle->height   = 11;                   // 十字架高度（竖直臂总长）
    obstacle->center_x = BOARD_COLS / 2;       // 中心坐标在棋盘内部
    obstacle->center_y = BOARD_ROWS / 2;
}

/* 判断一个内部坐标 (x,y) 是否属于障碍物 */
bool is_obstacle_position(const CrossObstacle *obstacle, int x, int y) {
    int cx = obstacle->center_x;
    int cy = obstacle->center_y;
    int half_w = obstacle->width  / 2;
    int half_h = obstacle->height / 2;

    // 注意：棋盘可走区域是 1 ~ BOARD_COLS-2 / 1 ~ BOARD_ROWS-2
    if (y == cy && x >= cx - half_w && x <= cx + half_w) {
        return true;    // 水平方向的一条线
    }
    if (x == cx && y >= cy - half_h && y <= cy + half_h) {
        return true;    // 垂直方向的一条线
    }
    return false;
}

/* 在 board 窗口中画出十字架障碍物 */
void draw_obstacle(WINDOW *board, const CrossObstacle *obstacle) {
    if (has_colors()) wattron(board, COLOR_PAIR(4));
    for (int y = 1; y < BOARD_ROWS - 1; y++) {
        for (int x = 1; x < BOARD_COLS - 1; x++) {
            if (is_obstacle_position(obstacle, x, y)) {
                mvwaddch(board, y, x, OBSTACLE);
            }
        }
    }
    if (has_colors()) wattroff(board, COLOR_PAIR(4));
}

/* -------------- UI 状态栏 -------------- */

void update_UI(const Player *player, const Robot *robot) {
    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "Player: %s  Score: %d  Level: %d  Lives: %d  Mode: %s  Invincible: %s",
             player->name, player->score, player->level, player->lives,
             robot->ai_mode ? "AI" : "Manual",
             robot->invincible ? "YES" : "NO");

    if (has_colors()) attron(COLOR_PAIR(5));
    mvhline(0, 0, ' ', xmax);
    mvprintw(0, 0, "%s", buf);

    mvhline(1, 0, ' ', xmax);
    mvprintw(1, 0, "Use Arrow Keys/WASD to move. 'm' toggle AI, 'q' quit.");
    if (has_colors()) attroff(COLOR_PAIR(5));

    refresh();
}

/* -------------- 输入处理：方向、模式切换、退出 -------------- */

void handle_input(Robot *robot, int input, bool *running) {
    if (input == ERR) return;   // 没有按键

    switch (input) {
        case 'q':
        case 'Q':
            *running = false;
            break;
        case 'm':
        case 'M':
            robot->ai_mode = !robot->ai_mode;
            break;
        case KEY_UP:
        case 'w':
        case 'W':
            if (!robot->ai_mode) set_direction(robot, 'N');
            break;
        case KEY_DOWN:
        case 's':
        case 'S':
            if (!robot->ai_mode) set_direction(robot, 'S');
            break;
        case KEY_LEFT:
        case 'a':
        case 'A':
            if (!robot->ai_mode) set_direction(robot, 'W');
            break;
        case KEY_RIGHT:
        case 'd':
        case 'D':
            if (!robot->ai_mode) set_direction(robot, 'E');
            break;
        default:
            break;
    }
}

/* -------------- 地雷相关 -------------- */

bool is_mine_at(const Position *mines, int mine_count, int x, int y) {
    for (int i = 0; i < mine_count; i++) {
        if (mines[i].x == x && mines[i].y == y) return true;
    }
    return false;
}

/* 向目标数量填充地雷（不会覆盖机器人/人/障碍物/已有地雷） */
void spawn_mines(const Robot *robot, const Position *person,
                 Position *mines, int *mine_count,
                 int target_count,
                 const CrossObstacle *obstacle) {
    if (target_count > MAX_MINES) target_count = MAX_MINES;

    while (*mine_count < target_count) {
        int x = 1 + rand() % (BOARD_COLS - 2);
        int y = 1 + rand() % (BOARD_ROWS - 2);

        if (x == robot->pos.x && y == robot->pos.y) continue;
        if (person && x == person->x && y == person->y) continue;
        if (is_obstacle_position(obstacle, x, y)) continue;
        if (is_mine_at(mines, *mine_count, x, y)) continue;

        mines[*mine_count].x = x;
        mines[*mine_count].y = y;
        (*mine_count)++;
    }
}

/* 在 board 上画出所有地雷 */
void draw_mines(WINDOW *board, const Position *mines, int mine_count) {
    if (has_colors()) wattron(board, COLOR_PAIR(3));
    for (int i = 0; i < mine_count; i++) {
        mvwaddch(board, mines[i].y, mines[i].x, MINE);
    }
    if (has_colors()) wattroff(board, COLOR_PAIR(3));
}

/* -------------- “人” 相关 -------------- */

/* 随机生成一个人：不能和机器人/地雷/障碍物重合 */
void spawn_person(const Robot *robot, Position *person,
                  const Position *mines, int mine_count,
                  const CrossObstacle *obstacle) {
    while (1) {
        int x = 1 + rand() % (BOARD_COLS - 2);
        int y = 1 + rand() % (BOARD_ROWS - 2);

        if (x == robot->pos.x && y == robot->pos.y) continue;
        if (is_mine_at(mines, mine_count, x, y)) continue;
        if (is_obstacle_position(obstacle, x, y)) continue;

        person->x = x;
        person->y = y;
        break;
    }
}

/* 在 board 上画出人 */
void draw_person(WINDOW *board, const Position *person) {
    if (has_colors()) wattron(board, COLOR_PAIR(2));
    mvwaddch(board, person->y, person->x, PERSON);
    if (has_colors()) wattroff(board, COLOR_PAIR(2));
}

/* -------------- 机器人移动 & 绘制 -------------- */

void clear_robot(WINDOW *board, const Robot *robot) {
    int dx, dy;
    direction_to_delta(robot->direction, &dx, &dy);

    // 擦头
    mvwaddch(board, robot->pos.y, robot->pos.x, ' ');

    // 擦身体（在头的反方向一格）
    int bx = robot->pos.x - dx;
    int by = robot->pos.y - dy;
    if (bx > 0 && bx < BOARD_COLS - 1 && by > 0 && by < BOARD_ROWS - 1) {
        mvwaddch(board, by, bx, ' ');
    }
}

/* 画机器人：头部方向不同字符 + 身体一个 'O' */
void draw_robot(WINDOW *board, const Robot *robot) {
    int dx, dy;
    direction_to_delta(robot->direction, &dx, &dy);

    // 身体
    int bx = robot->pos.x - dx;
    int by = robot->pos.y - dy;
    if (bx > 0 && bx < BOARD_COLS - 1 && by > 0 && by < BOARD_ROWS - 1) {
        if (has_colors()) wattron(board, COLOR_PAIR(1));
        mvwaddch(board, by, bx, ROBOT_BODY);
        if (has_colors()) wattroff(board, COLOR_PAIR(1));
    }

    // 头
    char head_char = ROBOT_HEAD;
    switch (robot->direction) {
        case 'N': head_char = '^'; break;
        case 'S': head_char = 'v'; break;
        case 'W': head_char = '<'; break;
        case 'E': head_char = '>'; break;
        default:  head_char = ROBOT_HEAD; break;
    }

    if (has_colors()) wattron(board, COLOR_PAIR(1));
    mvwaddch(board, robot->pos.y, robot->pos.x, head_char);
    if (has_colors()) wattroff(board, COLOR_PAIR(1));
}

/* 手动模式/AI 模式下，移动一步（不在这里做碰撞判定） */
void move_robot(Robot *robot) {
    int dx, dy;
    direction_to_delta(robot->direction, &dx, &dy);
    robot->pos.x += dx;
    robot->pos.y += dy;
}

/* AI：向“人”靠近，简单策略 + 避免撞墙/雷/障碍物 */
void move_robot_ai(Robot *robot, const Position *person,
                   const Position *mines, int mine_count) {
    if (!person) return;

    int target_x = person->x;
    int target_y = person->y;

    char new_dir = robot->direction;

    if (robot->pos.x < target_x)      new_dir = 'E';
    else if (robot->pos.x > target_x) new_dir = 'W';
    else if (robot->pos.y < target_y) new_dir = 'S';
    else if (robot->pos.y > target_y) new_dir = 'N';

    int dx, dy;
    direction_to_delta(new_dir, &dx, &dy);
    int nx = robot->pos.x + dx;
    int ny = robot->pos.y + dy;

    // 简单绕行：如果前方是墙/雷，就尝试其它方向
    if (nx <= 0 || nx >= BOARD_COLS - 1 || ny <= 0 || ny >= BOARD_ROWS - 1 ||
        is_mine_at(mines, mine_count, nx, ny)) {
        char candidates[4] = {'N','S','W','E'};
        for (int i = 0; i < 4; i++) {
            direction_to_delta(candidates[i], &dx, &dy);
            nx = robot->pos.x + dx;
            ny = robot->pos.y + dy;
            if (nx <= 0 || nx >= BOARD_COLS - 1 ||
                ny <= 0 || ny >= BOARD_ROWS - 1) {
                continue;
            }
            if (is_mine_at(mines, mine_count, nx, ny)) continue;
            new_dir = candidates[i];
            break;
        }
    }

    set_direction(robot, new_dir);
}

/* 碰撞检测：撞墙/雷/障碍物 -> 扣命、重生、无敌 */
void check_collision(Player *player, Robot *robot,
                     const Position *mines, int mine_count,
                     const CrossObstacle *obstacle,
                     bool *running) {
    int x = robot->pos.x;
    int y = robot->pos.y;

    bool hit_wall = (x <= 0 || x >= BOARD_COLS - 1 ||
                     y <= 0 || y >= BOARD_ROWS - 1);
    bool hit_obstacle = is_obstacle_position(obstacle, x, y);
    bool hit_mine = is_mine_at(mines, mine_count, x, y);

    bool deadly = (hit_wall || hit_obstacle || hit_mine);

    if (deadly && !robot->invincible) {
        player->lives--;
        robot->invincible = true;
        robot->invincible_ticks = INVINCIBLE_TICKS;

        if (player->lives <= 0) {
            *running = false;
            return;
        }

        // 重生到中心偏下
        robot->pos.x = BOARD_COLS / 2;
        robot->pos.y = BOARD_ROWS / 2 + 3;
        if (robot->pos.y >= BOARD_ROWS - 1) robot->pos.y = BOARD_ROWS / 2;
        set_direction(robot, 'W');
    }

    if (robot->invincible) {
        robot->invincible_ticks--;
        if (robot->invincible_ticks <= 0) {
            robot->invincible = false;
        }
    }
}

/* -------------- Game Over + 排行榜 -------------- */

int compare_scores_desc(const void *a, const void *b) {
    const LeaderboardEntry *ea = (const LeaderboardEntry *)a;
    const LeaderboardEntry *eb = (const LeaderboardEntry *)b;
    return eb->score - ea->score;
}

void save_score(const Player *player) {
    FILE *f = fopen(LEADERBOARD_FILE, "a");
    if (!f) return;
    fprintf(f, "%s %d\n", player->name, player->score);
    fclose(f);
}

void show_leaderboard(void) {
    FILE *f = fopen(LEADERBOARD_FILE, "r");
    if (!f) {
        mvprintw(5, 4, "No leaderboard yet.");
        return;
    }

    LeaderboardEntry *entries = NULL;
    int capacity = 0;
    int count = 0;

    char name_buf[MAX_NAME + 1];
    int score;

    while (fscanf(f, "%20s %d", name_buf, &score) == 2) {
        if (count >= capacity) {
            capacity = (capacity == 0) ? 16 : capacity * 2;
            entries = (LeaderboardEntry *)realloc(entries,
                         sizeof(LeaderboardEntry) * capacity);
            if (!entries) {
                fclose(f);
                mvprintw(5, 4, "Failed to allocate memory for leaderboard.");
                return;
            }
        }
        strncpy(entries[count].name, name_buf, MAX_NAME);
        entries[count].name[MAX_NAME] = '\0';
        entries[count].score = score;
        count++;
    }
    fclose(f);

    if (count == 0) {
        mvprintw(5, 4, "Leaderboard is empty.");
        free(entries);
        return;
    }

    qsort(entries, count, sizeof(LeaderboardEntry), compare_scores_desc);

    mvprintw(5, 4, "===== Leaderboard (Top 10) =====");
    int top = (count < 10) ? count : 10;
    for (int i = 0; i < top; i++) {
        mvprintw(7 + i, 6, "%2d. %-10s %5d",
                 i + 1, entries[i].name, entries[i].score);
    }

    free(entries);
}

void game_over_screen(const Player *player) {
    clear();
    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);

    const char *msg = "GAME OVER";
    mvprintw(ymax / 2 - 2, (xmax - (int)strlen(msg)) / 2, "%s", msg);

    char line[64];
    snprintf(line, sizeof(line), "Final score: %d", player->score);
    mvprintw(ymax / 2, (xmax - (int)strlen(line)) / 2, "%s", line);

    mvprintw(ymax / 2 + 2, (xmax - 30) / 2, "Player: %s", player->name);
    mvprintw(ymax / 2 + 4, (xmax - 40) / 2,
             "Press any key to save & view leaderboard...");
    refresh();

    // 等你看完 Game Over，再按任意键
    nodelay(stdscr, FALSE);
    getch();
    nodelay(stdscr, TRUE);

    // ① 保存当前成绩到 leaderboard.txt
    save_score(player);

    // ② 清屏并显示排行榜
    clear();
    mvprintw(2, 4, "Leaderboard file: %s", LEADERBOARD_FILE);
    show_leaderboard();   // 会打印 Top 10 排名
    mvprintw(LINES - 2, 4, "Press any key to exit.");
    refresh();
    getch();
}

/* -------------- 速度控制 -------------- */

int get_delay_for_level(int level) {
    int delay = BASE_DELAY_MS - (level - 1) * LEVEL_SPEEDUP_MS;
    if (delay < MIN_DELAY_MS) delay = MIN_DELAY_MS;
    return delay;
}

/* -------------- main：主循环 -------------- */

int main(void) {
    srand((unsigned int)time(NULL));

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE);
    init_colors();

    Player player;
    Robot  robot;
    Position person;
    Position mines[MAX_MINES];
    int mine_count = 0;
    CrossObstacle obstacle;

    // 标题界面：输入名字 + 初始化玩家
    draw_title_screen(&player);

    // 初始化棋盘窗口和机器人、障碍物
    WINDOW *board = init_game(&player, &robot, &person, mines, mine_count, &obstacle);

    // 初始“人”和地雷
    spawn_person(&robot, &person, mines, mine_count, &obstacle);
    spawn_mines(&robot, &person, mines, &mine_count, BASE_MINES, &obstacle);

    bool running = true;

    while (running && player.lives > 0) {
        int ch = getch();
        handle_input(&robot, ch, &running);
        if (!running) break;

        if (robot.ai_mode) {
            move_robot_ai(&robot, &person, mines, mine_count);
        }

        clear_robot(board, &robot);
        move_robot(&robot);
        check_collision(&player, &robot, mines, mine_count, &obstacle, &running);
        if (!running || player.lives <= 0) break;

        // 检查是否救到人
        if (robot.pos.x == person.x && robot.pos.y == person.y) {
            player.score += 10;
            player.rescued++;

            if (player.rescued >= PEOPLE_PER_LEVEL) {
                player.level++;
                player.rescued = 0;
                int target = mine_count + MINES_PER_LEVEL;
                spawn_mines(&robot, &person, mines, &mine_count, target, &obstacle);
            }

            spawn_person(&robot, &person, mines, mine_count, &obstacle);
        }

        // 重新绘制棋盘内容
        werase(board);
        box(board, 0, 0);
        draw_obstacle(board, &obstacle);
        draw_mines(board, mines, mine_count);
        draw_person(board, &person);
        draw_robot(board, &robot);

        update_UI(&player, &robot);
        wrefresh(board);

        int delay_ms = get_delay_for_level(player.level);
        napms(delay_ms);
    }

    game_over_screen(&player);

    delwin(board);
    endwin();
    return 0;
}
