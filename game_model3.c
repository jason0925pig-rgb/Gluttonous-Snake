#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>

/* ================== 基本宏（沿用老师风格） ================== */

#define BOARD_ROWS 20      // 包含边框
#define BOARD_COLS 50

#define MAX_NAME 20

#define ROBOT_BODY 'O'
#define ROBOT_HEAD '^'
#define PERSON     'P'
#define MINE       'X'
#define OBSTACLE   '#'

#define INITIAL_LIVES      3
#define PEOPLE_PER_LEVEL   5
#define MAX_MINES          50
#define BASE_MINES         5
#define MINES_PER_LEVEL    2

#define BASE_DELAY_MS      200
#define LEVEL_SPEEDUP_MS   20
#define MIN_DELAY_MS       60

#define INVINCIBLE_TICKS   10

#define LEADERBOARD_FILE   "leaderboard.txt"

/* ================== 结构体定义 ================== */

typedef struct {
    int x; // 列坐标（1~BOARD_COLS-2）
    int y; // 行坐标（1~BOARD_ROWS-2）
} Position;

typedef struct {
    Position pos;      // 机器人头部位置
    char     direction; // 'N','S','E','W'
    bool     ai_mode;   // true=AI，false=手动
    bool     invincible;
    int      invincible_ticks;
} Robot;

typedef struct {
    char name[MAX_NAME + 1];
    int  score;
    int  lives;
    int  level;
    int  rescued;      // 当前等级已救人数
} Player;

typedef struct {
    int width;
    int height;
    int center_x;
    int center_y;
} CrossObstacle;

typedef struct {
    char name[MAX_NAME + 1];
    int  score;
} LeaderboardEntry;

/* ================== 颜色初始化 ================== */

#define CP_ROBOT     1
#define CP_PERSON    2
#define CP_MINE      3
#define CP_OBSTACLE  4
#define CP_STATUS    5
#define CP_BOARD_BG  6  // 棋盘背景（黑底）

void init_colors(void) {
    if (!has_colors()) return;
    start_color();
    use_default_colors();

    init_pair(CP_ROBOT,    COLOR_WHITE,  COLOR_BLACK);
    init_pair(CP_PERSON,   COLOR_GREEN,  COLOR_BLACK);
    init_pair(CP_MINE,     COLOR_RED,    COLOR_BLACK);
    init_pair(CP_OBSTACLE, COLOR_YELLOW, COLOR_BLACK);
    init_pair(CP_STATUS,   COLOR_CYAN,   -1);
    init_pair(CP_BOARD_BG, COLOR_WHITE,  COLOR_BLACK); // 统一棋盘背景黑色
}

/* ================== 工具函数声明 ================== */

void draw_title_screen(Player *player);

WINDOW* init_game(Player *player,
                  Robot *robot,
                  Position *person,
                  Position *mines,
                  int mine_count,
                  CrossObstacle *obstacle);

void init_obstacle(CrossObstacle *obstacle);
bool is_obstacle_position(const CrossObstacle *obstacle, int x, int y);
void draw_obstacle(WINDOW *board, const CrossObstacle *obstacle);

void update_UI(const Player *player, const Robot *robot);

void handle_input(Robot *robot, int input, bool *running);

void move_robot(Robot *robot);
void move_robot_ai(Robot *robot, const Position *person,
                   const Position *mines, int mine_count,
                   const CrossObstacle *obstacle);

void clear_robot(WINDOW *board, const Robot *robot);
void draw_robot(WINDOW *board, const Robot *robot);

bool is_mine_at(const Position *mines, int mine_count, int x, int y);
void spawn_mines(const Robot *robot, const Position *person,
                 Position *mines, int *mine_count,
                 int target_count,
                 const CrossObstacle *obstacle);
void draw_mines(WINDOW *board, const Position *mines, int mine_count);

void spawn_person(const Robot *robot, Position *person,
                  const Position *mines, int mine_count,
                  const CrossObstacle *obstacle);
void draw_person(WINDOW *board, const Position *person);

void check_collision(Player *player, Robot *robot,
                     const Position *mines, int mine_count,
                     const CrossObstacle *obstacle,
                     bool *running);

int  get_delay_for_level(int level);

void game_over_screen(const Player *player);

/* ================== 方向相关 ================== */

void set_direction(Robot *robot, char dir) {
    robot->direction = dir;
}

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

/* ================== 标题界面 ================== */

void draw_title_screen(Player *player) {
    // 标题界面用阻塞输入
    nodelay(stdscr, FALSE);
    clear();

    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);

    const char *title = "Rescue Bot: Minesweeper Snake";
    mvprintw(2, (xmax - (int)strlen(title)) / 2, "%s", title);

    mvprintw(4, 4, "Description:");
    mvprintw(5, 6, "Control a small robot to rescue people on a minefield.");
    mvprintw(6, 6, "Avoid walls, mines and the central obstacle.");
    mvprintw(7, 6, "Each rescued person increases your score.");
    mvprintw(8, 6, "Every %d people rescued: level up (faster, more mines).",
             PEOPLE_PER_LEVEL);

    mvprintw(10, 4, "Controls:");
    mvprintw(11, 6, "Arrow keys / WASD : move robot (manual)");
    mvprintw(12, 6, "'m'               : toggle Manual / AI");
    mvprintw(13, 6, "'q'               : quit game");

    // 确保每次都从干净的初始状态开始
    player->score   = 0;
    player->lives   = INITIAL_LIVES;
    player->level   = 1;
    player->rescued = 0;

    mvprintw(15, 4, "Enter your name (max %d chars) and press ENTER:", MAX_NAME);
    mvprintw(16, 4, "> ");
    move(16, 6);

    echo();
    curs_set(1);

    char buf[MAX_NAME + 1];
    memset(buf, 0, sizeof(buf));
    getnstr(buf, MAX_NAME);

    noecho();
    curs_set(0);

    if (strlen(buf) == 0) {
        strcpy(player->name, "Player");
    } else {
        strncpy(player->name, buf, MAX_NAME);
        player->name[MAX_NAME] = '\0';
    }

    mvprintw(18, 4, "Welcome, %s! Press any key to start...", player->name);
    refresh();
    getch();

    // 游戏主循环需要非阻塞
    nodelay(stdscr, TRUE);
}

/* ================== 障碍物 ================== */

void init_obstacle(CrossObstacle *obstacle) {
    obstacle->width    = 11;
    obstacle->height   = 11;
    obstacle->center_x = BOARD_COLS / 2;
    obstacle->center_y = BOARD_ROWS / 2;
}

bool is_obstacle_position(const CrossObstacle *obstacle, int x, int y) {
    int cx = obstacle->center_x;
    int cy = obstacle->center_y;
    int half_w = obstacle->width  / 2;
    int half_h = obstacle->height / 2;

    if (y == cy && x >= cx - half_w && x <= cx + half_w) return true;
    if (x == cx && y >= cy - half_h && y <= cy + half_h) return true;
    return false;
}

void draw_obstacle(WINDOW *board, const CrossObstacle *obstacle) {
    wattron(board, COLOR_PAIR(CP_OBSTACLE));
    for (int y = 1; y < BOARD_ROWS - 1; y++) {
        for (int x = 1; x < BOARD_COLS - 1; x++) {
            if (is_obstacle_position(obstacle, x, y)) {
                mvwaddch(board, y, x, OBSTACLE);
            }
        }
    }
    wattroff(board, COLOR_PAIR(CP_OBSTACLE));
}

/* ================== 棋盘初始化 ================== */

WINDOW* init_game(Player *player,
                  Robot *robot,
                  Position *person,
                  Position *mines,
                  int mine_count,
                  CrossObstacle *obstacle) {
    (void)player;
    (void)person;
    (void)mines;
    (void)mine_count;

    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);
    int start_y = (ymax - BOARD_ROWS) / 2;
    int start_x = (xmax - BOARD_COLS) / 2;

    WINDOW *board = newwin(BOARD_ROWS, BOARD_COLS, start_y, start_x);

    // 统一棋盘背景为黑色
    wbkgd(board, COLOR_PAIR(CP_BOARD_BG));
    werase(board);
    box(board, 0, 0);

    // 机器人起始位置
    robot->pos.x = BOARD_COLS / 2;
    robot->pos.y = BOARD_ROWS / 2 + 3;
    if (robot->pos.y >= BOARD_ROWS - 1) robot->pos.y = BOARD_ROWS / 2;

    robot->ai_mode = true;      // 一开始就是 AI 模式
    robot->invincible = false;
    robot->invincible_ticks = 0;
    set_direction(robot, 'W');

    init_obstacle(obstacle);

    wrefresh(board);
    return board;
}

/* ================== UI 状态栏 ================== */

void update_UI(const Player *player, const Robot *robot) {
    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "Player: %s  Score: %d  Level: %d  Lives: %d  Mode: %s  Invincible: %s",
             player->name, player->score, player->level, player->lives,
             robot->ai_mode ? "AI" : "Manual",
             robot->invincible ? "YES" : "NO");

    attron(COLOR_PAIR(CP_STATUS));
    mvhline(0, 0, ' ', xmax);
    mvprintw(0, 0, "%s", buf);

    mvhline(1, 0, ' ', xmax);
    mvprintw(1, 0, "Use Arrow keys/WASD to move. 'm' toggle AI, 'q' quit.");
    attroff(COLOR_PAIR(CP_STATUS));

    refresh();
}

/* ================== 输入处理 ================== */

void handle_input(Robot *robot, int input, bool *running) {
    if (input == ERR) return;

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

/* ================== 地雷 ================== */

bool is_mine_at(const Position *mines, int mine_count, int x, int y) {
    for (int i = 0; i < mine_count; i++) {
        if (mines[i].x == x && mines[i].y == y) return true;
    }
    return false;
}

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

void draw_mines(WINDOW *board, const Position *mines, int mine_count) {
    wattron(board, COLOR_PAIR(CP_MINE));
    for (int i = 0; i < mine_count; i++) {
        mvwaddch(board, mines[i].y, mines[i].x, MINE);
    }
    wattroff(board, COLOR_PAIR(CP_MINE));
}

/* ================== 人 ================== */

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

void draw_person(WINDOW *board, const Position *person) {
    wattron(board, COLOR_PAIR(CP_PERSON));
    mvwaddch(board, person->y, person->x, PERSON);
    wattroff(board, COLOR_PAIR(CP_PERSON));
}

/* ================== 机器人绘制与移动 ================== */

void clear_robot(WINDOW *board, const Robot *robot) {
    int dx, dy;
    direction_to_delta(robot->direction, &dx, &dy);

    // 用棋盘背景颜色擦除，保证背景统一
    mvwaddch(board, robot->pos.y, robot->pos.x, ' ' | COLOR_PAIR(CP_BOARD_BG));

    int bx = robot->pos.x - dx;
    int by = robot->pos.y - dy;
    if (bx > 0 && bx < BOARD_COLS - 1 && by > 0 && by < BOARD_ROWS - 1) {
        mvwaddch(board, by, bx, ' ' | COLOR_PAIR(CP_BOARD_BG));
    }
}

void draw_robot(WINDOW *board, const Robot *robot) {
    int dx, dy;
    direction_to_delta(robot->direction, &dx, &dy);

    int bx = robot->pos.x - dx;
    int by = robot->pos.y - dy;

    // 身体
    if (bx > 0 && bx < BOARD_COLS - 1 && by > 0 && by < BOARD_ROWS - 1) {
        wattron(board, COLOR_PAIR(CP_ROBOT));
        mvwaddch(board, by, bx, ROBOT_BODY);
        wattroff(board, COLOR_PAIR(CP_ROBOT));
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

    wattron(board, COLOR_PAIR(CP_ROBOT));
    mvwaddch(board, robot->pos.y, robot->pos.x, head_char);
    wattroff(board, COLOR_PAIR(CP_ROBOT));
}

void move_robot(Robot *robot) {
    int dx, dy;
    direction_to_delta(robot->direction, &dx, &dy);
    robot->pos.x += dx;
    robot->pos.y += dy;
}

/* ================== AI：BFS 寻路 ================== */

typedef struct {
    int x, y;
} Node;

static bool is_blocked_cell(int x, int y,
                            const Position *mines, int mine_count,
                            const CrossObstacle *obstacle) {
    if (x <= 0 || x >= BOARD_COLS - 1 || y <= 0 || y >= BOARD_ROWS - 1)
        return true;
    if (is_obstacle_position(obstacle, x, y))
        return true;
    if (is_mine_at(mines, mine_count, x, y))
        return true;
    return false;
}

/* 尝试用 BFS 找到从机器人到人的最短路径，并给出下一步方向 */
static bool bfs_next_direction(const Robot *robot, const Position *person,
                               const Position *mines, int mine_count,
                               const CrossObstacle *obstacle,
                               char *out_dir) {
    bool visited[BOARD_ROWS][BOARD_COLS] = {false};
    Position parent[BOARD_ROWS][BOARD_COLS];

    for (int y = 0; y < BOARD_ROWS; y++) {
        for (int x = 0; x < BOARD_COLS; x++) {
            parent[y][x].x = -1;
            parent[y][x].y = -1;
        }
    }

    Node queue[BOARD_ROWS * BOARD_COLS];
    int front = 0, back = 0;

    int sx = robot->pos.x;
    int sy = robot->pos.y;
    int tx = person->x;
    int ty = person->y;

    queue[back++] = (Node){sx, sy};
    visited[sy][sx] = true;

    int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};

    bool found = false;

    while (front < back) {
        Node cur = queue[front++];
        if (cur.x == tx && cur.y == ty) {
            found = true;
            break;
        }

        for (int i = 0; i < 4; i++) {
            int nx = cur.x + dirs[i][0];
            int ny = cur.y + dirs[i][1];

            if (nx < 0 || nx >= BOARD_COLS ||
                ny < 0 || ny >= BOARD_ROWS)
                continue;
            if (visited[ny][nx]) continue;
            if (is_blocked_cell(nx, ny, mines, mine_count, obstacle))
                continue;

            visited[ny][nx] = true;
            parent[ny][nx].x = cur.x;
            parent[ny][nx].y = cur.y;
            queue[back++] = (Node){nx, ny};
        }
    }

    if (!found) return false;

    // 从目标回溯到起点，找到起点之后的那一步
    int cx = tx, cy = ty;
    int px = parent[cy][cx].x;
    int py = parent[cy][cx].y;

    if (px == -1 && py == -1) return false; // 理论上不会发生

    while (!(px == sx && py == sy)) {
        cx = px;
        cy = py;
        px = parent[cy][cx].x;
        py = parent[cy][cx].y;
        if (px == -1 && py == -1) break;
    }

    int dx = cx - sx;
    int dy = cy - sy;

    if (dx == 1 && dy == 0)      *out_dir = 'E';
    else if (dx == -1 && dy == 0)*out_dir = 'W';
    else if (dx == 0 && dy == 1) *out_dir = 'S';
    else if (dx == 0 && dy == -1)*out_dir = 'N';
    else return false;

    return true;
}

void move_robot_ai(Robot *robot, const Position *person,
                   const Position *mines, int mine_count,
                   const CrossObstacle *obstacle) {
    if (!person) return;

    char dir;
    if (bfs_next_direction(robot, person, mines, mine_count, obstacle, &dir)) {
        set_direction(robot, dir);
        return;
    }

    // 找不到路径时，随机找一个安全方向
    char candidates[4] = {'N','S','E','W'};
    for (int k = 0; k < 4; k++) {
        int i = rand() % 4;
        int dx, dy;
        direction_to_delta(candidates[i], &dx, &dy);
        int nx = robot->pos.x + dx;
        int ny = robot->pos.y + dy;
        if (!is_blocked_cell(nx, ny, mines, mine_count, obstacle)) {
            set_direction(robot, candidates[i]);
            return;
        }
    }
    // 实在找不到就保持原方向（很少发生）
}

/* ================== 碰撞检测 ================== */

void check_collision(Player *player, Robot *robot,
                     const Position *mines, int mine_count,
                     const CrossObstacle *obstacle,
                     bool *running) {
    int x = robot->pos.x;
    int y = robot->pos.y;

    bool hit_wall = (x <= 0 || x >= BOARD_COLS - 1 ||
                     y <= 0 || y >= BOARD_ROWS - 1);
    bool hit_mine = is_mine_at(mines, mine_count, x, y);
    bool hit_obstacle = is_obstacle_position(obstacle, x, y);

    bool deadly = hit_wall || hit_mine || hit_obstacle;

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

/* ================== Game Over + 排行榜 ================== */

int compare_scores_desc(const void *a, const void *b) {
    const LeaderboardEntry *ea = (const LeaderboardEntry *)a;
    const LeaderboardEntry *eb = (const LeaderboardEntry *)b;
    return eb->score - ea->score;
}

void game_over_screen(const Player *player) {
    // 先把成绩写入文件
    FILE *f = fopen(LEADERBOARD_FILE, "a");
    if (f) {
        fprintf(f, "%s %d\n", player->name, player->score);
        fclose(f);
    }

    // 再读取所有成绩
    f = fopen(LEADERBOARD_FILE, "r");
    LeaderboardEntry *entries = NULL;
    int count = 0, cap = 0;

    if (f) {
        char name_buf[MAX_NAME + 1];
        int score;
        while (fscanf(f, "%20s %d", name_buf, &score) == 2) {
            if (count >= cap) {
                cap = (cap == 0) ? 16 : cap * 2;
                entries = (LeaderboardEntry *)realloc(entries,
                           sizeof(LeaderboardEntry) * cap);
                if (!entries) break;
            }
            strncpy(entries[count].name, name_buf, MAX_NAME);
            entries[count].name[MAX_NAME] = '\0';
            entries[count].score = score;
            count++;
        }
        fclose(f);
    }

    if (entries && count > 0) {
        qsort(entries, count, sizeof(LeaderboardEntry), compare_scores_desc);
    }

    clear();
    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);

    const char *msg = "GAME OVER";
    mvprintw(2, (xmax - (int)strlen(msg)) / 2, "%s", msg);

    char line[64];
    snprintf(line, sizeof(line), "Final score: %d", player->score);
    mvprintw(4, (xmax - (int)strlen(line)) / 2, "%s", line);

    char line2[64];
    snprintf(line2, sizeof(line2), "Player: %s", player->name);
    mvprintw(5, (xmax - (int)strlen(line2)) / 2, "%s", line2);

    mvprintw(7, 4, "===== Leaderboard (Top 10) =====");
    if (!entries || count == 0) {
        mvprintw(9, 6, "No records yet.");
    } else {
        int top = (count < 10) ? count : 10;
        for (int i = 0; i < top; i++) {
            mvprintw(9 + i, 6, "%2d. %-10s %5d",
                     i + 1, entries[i].name, entries[i].score);
        }
    }

    mvprintw(ymax - 2, 4, "Press any key to exit.");
    refresh();

    nodelay(stdscr, FALSE);
    getch();
    nodelay(stdscr, TRUE);

    free(entries);
}

/* ================== 速度控制 ================== */

int get_delay_for_level(int level) {
    int delay = BASE_DELAY_MS - (level - 1) * LEVEL_SPEEDUP_MS;
    if (delay < MIN_DELAY_MS) delay = MIN_DELAY_MS;
    return delay;
}

/* ================== main：主循环 ================== */

int main(void) {
    srand((unsigned int)time(NULL));

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE);
    init_colors();

    Player  player;
    Robot   robot;
    Position person;
    Position mines[MAX_MINES];
    int mine_count = 0;
    CrossObstacle obstacle;

    draw_title_screen(&player);

    WINDOW *board = init_game(&player, &robot, &person, mines, mine_count, &obstacle);

    spawn_person(&robot, &person, mines, mine_count, &obstacle);
    spawn_mines(&robot, &person, mines, &mine_count, BASE_MINES, &obstacle);

    bool running = true;

    while (running && player.lives > 0) {
        int ch = getch();
        handle_input(&robot, ch, &running);
        if (!running) break;

        if (robot.ai_mode) {
            move_robot_ai(&robot, &person, mines, mine_count, &obstacle);
        }

        clear_robot(board, &robot);
        move_robot(&robot);
        check_collision(&player, &robot, mines, mine_count, &obstacle, &running);
        if (!running || player.lives <= 0) break;

        // 救到人
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

        // 重画棋盘
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
