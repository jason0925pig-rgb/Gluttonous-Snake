#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>

/* ================== 基本宏（保持老师风格） ================== */

#define BOARD_ROWS 20
#define BOARD_COLS 50
#define MAX_NAME   20

#define ROBOT_BODY 'O'
#define ROBOT_HEAD '^'
#define PERSON     'P'
#define MINE       'X'
#define OBSTACLE   '#'

#define INITIAL_LIVES      3          // 初始生命条数
#define PEOPLE_PER_LEVEL   5          // 每救 5 人升一级
#define MAX_MINES          50
#define BASE_MINES         5
#define MINES_PER_LEVEL    2          // 每升一级增加地雷

#define BASE_DELAY_MS      200        // 初始刷新间隔
#define LEVEL_SPEEDUP_MS   20         // 每升一级减少的间隔
#define MIN_DELAY_MS       60         // 最快速度

#define INVINCIBLE_TICKS   10         // 死亡后短暂无敌步数

#define LEADERBOARD_FILE   "leaderboard.txt"

// 贪吃蛇身体的最大长度（最多 20 条命）
#define MAX_BODY_SEGMENTS  20

/* ================== 结构体定义 ================== */

typedef struct {
    int x;  // 列
    int y;  // 行
} Position;

typedef struct {
    Position pos;           // 头部位置
    char     direction;     // 'N','S','E','W'
    bool     ai_mode;       // 是否 AI 模式
    bool     invincible;    // 是否处于无敌
    int      invincible_ticks;

    int      body_length;   // 身体段数量（不含头）
    Position body[MAX_BODY_SEGMENTS]; // body[0] 紧贴头部，body[body_length-1] 是尾巴
} Robot;

typedef struct {
    char name[MAX_NAME + 1];
    int  score;
    int  lives;
    int  level;
    int  rescued;           // 当前等级已经救的人数
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

/* ================== 颜色设置 ================== */

#define CP_ROBOT     1
#define CP_PERSON    2
#define CP_MINE      3
#define CP_OBSTACLE  4
#define CP_STATUS    5
#define CP_BOARD_BG  6   // 棋盘统一黑底

void init_colors(void) {
    if (!has_colors()) return;
    start_color();
    use_default_colors();

    init_pair(CP_ROBOT,    COLOR_WHITE,  COLOR_BLACK);
    init_pair(CP_PERSON,   COLOR_GREEN,  COLOR_BLACK);
    init_pair(CP_MINE,     COLOR_RED,    COLOR_BLACK);
    init_pair(CP_OBSTACLE, COLOR_YELLOW, COLOR_BLACK);
    init_pair(CP_STATUS,   COLOR_CYAN,   -1);
    init_pair(CP_BOARD_BG, COLOR_WHITE,  COLOR_BLACK);
}

/* ================== 方向工具函数 ================== */

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

/* 根据玩家生命数，重新生成蛇身（在头的反方向一排） */
void reset_robot_body_from_lives(Robot *robot, const Player *player) {
    int len = player->lives;
    if (len < 0) len = 0;
    if (len > MAX_BODY_SEGMENTS) len = MAX_BODY_SEGMENTS;

    robot->body_length = len;

    int dx, dy;
    direction_to_delta(robot->direction, &dx, &dy);
    if (dx == 0 && dy == 0) { dx = -1; dy = 0; } // 没方向时默认向左

    for (int i = 0; i < robot->body_length; i++) {
        int bx = robot->pos.x - dx * (i + 1);
        int by = robot->pos.y - dy * (i + 1);

        // 防止一开始就画出边界
        if (bx <= 0 || bx >= BOARD_COLS - 1 ||
            by <= 0 || by >= BOARD_ROWS - 1) {
            bx = robot->pos.x;
            by = robot->pos.y;
        }

        robot->body[i].x = bx;
        robot->body[i].y = by;
    }
}

/* ================== 标题界面 ================== */

void draw_title_screen(Player *player) {
    // 标题界面用阻塞输入
    nodelay(stdscr, FALSE);
    clear();

    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);

    const char *title = "Rescue Bot: Snake on a Minefield";
    mvprintw(2, (xmax - (int)strlen(title)) / 2, "%s", title);

    mvprintw(4, 4, "Description:");
    mvprintw(5, 6, "Guide a snake-like robot to rescue people on a minefield.");
    mvprintw(6, 6, "Avoid walls, mines and the central cross obstacle.");
    mvprintw(7, 6, "Your robot has multiple body segments = number of lives.");
    mvprintw(8, 6, "Lose one life -> lose one segment.");
    mvprintw(9, 6, "Every 5 levels you gain +1 extra life (segment).");
    mvprintw(10,6, "Rescue people, survive longer, and beat the high score!");

    mvprintw(12, 4, "Controls:");
    mvprintw(13, 6, "Arrow keys / WASD : move robot (Manual mode)");
    mvprintw(14, 6, "'m'               : toggle Manual / AI mode");
    mvprintw(15, 6, "'q'               : quit game");

    // 初始化玩家状态
    player->score   = 0;
    player->lives   = INITIAL_LIVES;
    player->level   = 1;
    player->rescued = 0;

    mvprintw(17, 4, "Enter your name (max %d chars) and press ENTER:", MAX_NAME);
    mvprintw(18, 4, "> ");
    move(18, 6);

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

    mvprintw(20, 4, "Welcome, %s! Press any key to start...", player->name);
    refresh();
    getch();          // 等你按键再开始游戏

    // 游戏主循环需要非阻塞输入
    nodelay(stdscr, TRUE);
}

/* ================== 障碍物（十字架） ================== */

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

WINDOW* init_game(Player *player, Robot *robot,
                  Position *person, Position *mines,
                  int mine_count, CrossObstacle *obstacle) {
    (void)player; (void)person; (void)mines; (void)mine_count;

    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);
    int start_y = (ymax - BOARD_ROWS) / 2;
    int start_x = (xmax - BOARD_COLS) / 2;

    WINDOW *board = newwin(BOARD_ROWS, BOARD_COLS, start_y, start_x);
    wbkgd(board, COLOR_PAIR(CP_BOARD_BG));
    werase(board);
    box(board, 0, 0);

    // 机器人初始位置
    robot->pos.x = BOARD_COLS / 2;
    robot->pos.y = BOARD_ROWS / 2 + 3;
    if (robot->pos.y >= BOARD_ROWS - 1)
        robot->pos.y = BOARD_ROWS / 2;

    robot->ai_mode          = true;   // 初始就是 AI 模式
    robot->invincible       = false;
    robot->invincible_ticks = 0;
    robot->body_length      = 0;
    set_direction(robot, 'W');        // 初始向左

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
             "Player: %s  Score: %d  Level: %d  Lives: %d  Mode: %s  Segments: %d",
             player->name, player->score, player->level, player->lives,
             robot->ai_mode ? "AI" : "Manual",
             robot->body_length);

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
        case 'q': case 'Q':
            *running = false;
            break;
        case 'm': case 'M':
            robot->ai_mode = !robot->ai_mode;
            break;
        case KEY_UP: case 'w': case 'W':
            if (!robot->ai_mode) set_direction(robot, 'N');
            break;
        case KEY_DOWN: case 's': case 'S':
            if (!robot->ai_mode) set_direction(robot, 'S');
            break;
        case KEY_LEFT: case 'a': case 'A':
            if (!robot->ai_mode) set_direction(robot, 'W');
            break;
        case KEY_RIGHT: case 'd': case 'D':
            if (!robot->ai_mode) set_direction(robot, 'E');
            break;
        default:
            break;
    }
}

/* ================== 地雷 ================== */

bool is_mine_at(const Position *mines, int mine_count, int x, int y) {
    for (int i = 0; i < mine_count; i++) {
        if (mines[i].x == x && mines[i].y == y)
            return true;
    }
    return false;
}

void spawn_mines(const Robot *robot, const Position *person,
                 Position *mines, int *mine_count,
                 int target_count, const CrossObstacle *obstacle) {
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

/* ================== 被救的人 ================== */

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

/* ================== 机器人绘制与移动（贪吃蛇效果） ================== */

void clear_robot(WINDOW *board, const Robot *robot) {
    // 清除头
    mvwaddch(board, robot->pos.y, robot->pos.x,
             ' ' | COLOR_PAIR(CP_BOARD_BG));

    // 清除身体
    for (int i = 0; i < robot->body_length; i++) {
        int bx = robot->body[i].x;
        int by = robot->body[i].y;
        if (bx > 0 && bx < BOARD_COLS - 1 &&
            by > 0 && by < BOARD_ROWS - 1) {
            mvwaddch(board, by, bx, ' ' | COLOR_PAIR(CP_BOARD_BG));
        }
    }
}

void draw_robot(WINDOW *board, const Robot *robot) {
    // 画身体
    wattron(board, COLOR_PAIR(CP_ROBOT));
    for (int i = 0; i < robot->body_length; i++) {
        int bx = robot->body[i].x;
        int by = robot->body[i].y;
        if (bx > 0 && bx < BOARD_COLS - 1 &&
            by > 0 && by < BOARD_ROWS - 1) {
            mvwaddch(board, by, bx, ROBOT_BODY);
        }
    }

    // 画头
    char head_char = ROBOT_HEAD;
    switch (robot->direction) {
        case 'N': head_char = '^'; break;
        case 'S': head_char = 'v'; break;
        case 'W': head_char = '<'; break;
        case 'E': head_char = '>'; break;
        default:  head_char = ROBOT_HEAD; break;
    }
    mvwaddch(board, robot->pos.y, robot->pos.x, head_char);
    wattroff(board, COLOR_PAIR(CP_ROBOT));
}

/* 真·贪吃蛇移动：body[i] 跟随 body[i-1] / 头 */

void move_robot(Robot *robot) {
    int dx, dy;
    direction_to_delta(robot->direction, &dx, &dy);

    // 身体段从尾到头依次往前挪
    for (int i = robot->body_length - 1; i > 0; i--) {
        robot->body[i] = robot->body[i - 1];
    }
    if (robot->body_length > 0) {
        robot->body[0] = robot->pos;
    }

    // 头移动
    robot->pos.x += dx;
    robot->pos.y += dy;
}

/* ================== AI：BFS 寻路 ================== */

typedef struct { int x, y; } Node;

static bool is_blocked_cell(int x, int y,
                            const Position *mines, int mine_count,
                            const CrossObstacle *obstacle) {
    if (x <= 0 || x >= BOARD_COLS - 1 ||
        y <= 0 || y >= BOARD_ROWS - 1)
        return true;
    if (is_obstacle_position(obstacle, x, y)) return true;
    if (is_mine_at(mines, mine_count, x, y)) return true;
    return false;
}

/* BFS 找到从机器人到人的最短路径的下一步方向 */
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

            visited[ny][nx]   = true;
            parent[ny][nx].x  = cur.x;
            parent[ny][nx].y  = cur.y;
            queue[back++]     = (Node){nx, ny};
        }
    }

    if (!found) return false;

    // 回溯路径，找到“从起点走出的第一步”
    int cx = tx, cy = ty;
    int px = parent[cy][cx].x;
    int py = parent[cy][cx].y;

    if (px == -1 && py == -1) return false;

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

    // 找不到路径时，随机试几个方向
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
}

/* ================== 碰撞检测：墙 / 雷 / 障碍 ================== */

void check_collision(Player *player, Robot *robot,
                     const Position *mines, int mine_count,
                     const CrossObstacle *obstacle,
                     bool *running) {
    int x = robot->pos.x;
    int y = robot->pos.y;

    bool hit_wall = (x <= 0 || x >= BOARD_COLS - 1 ||
                     y <= 0 || y >= BOARD_ROWS - 1);
    bool hit_mine = is_mine_at(mines, mine_count, x, y);
    bool hit_obs  = is_obstacle_position(obstacle, x, y);

    bool deadly = hit_wall || hit_mine || hit_obs;

    if (deadly && !robot->invincible) {
        // 掉一条命 = 少一个身体段
        player->lives--;

        if (player->lives <= 0) {
            *running = false;
            return;
        }

        robot->invincible        = true;
        robot->invincible_ticks  = INVINCIBLE_TICKS;

        // 重生到中间偏下一点
        robot->pos.x = BOARD_COLS / 2;
        robot->pos.y = BOARD_ROWS / 2 + 3;
        if (robot->pos.y >= BOARD_ROWS - 1)
            robot->pos.y = BOARD_ROWS / 2;

        set_direction(robot, 'W');
        reset_robot_body_from_lives(robot, player);
    }

    if (robot->invincible) {
        robot->invincible_ticks--;
        if (robot->invincible_ticks <= 0) {
            robot->invincible = false;
        }
    }
}

/* ================== Game Over + 新纪录 + 排行榜 ================== */

int compare_scores_desc(const void *a, const void *b) {
    const LeaderboardEntry *ea = (const LeaderboardEntry *)a;
    const LeaderboardEntry *eb = (const LeaderboardEntry *)b;
    return eb->score - ea->score;
}

void game_over_screen(const Player *player) {
    // 1. 先读旧排行榜
    LeaderboardEntry *entries = NULL;
    int count = 0, cap = 0;

    FILE *f = fopen(LEADERBOARD_FILE, "r");
    if (f) {
        char name_buf[MAX_NAME + 1];
        int score;
        while (fscanf(f, "%20s %d", name_buf, &score) == 2) {
            if (count >= cap) {
                cap = (cap == 0) ? 16 : cap * 2;
                entries = realloc(entries,
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

    int best_before = -1;
    for (int i = 0; i < count; i++) {
        if (entries[i].score > best_before)
            best_before = entries[i].score;
    }

    bool new_record = (count == 0 || player->score > best_before);

    // 2. 把当前成绩也加入数组
    if (count >= cap) {
        cap = (cap == 0) ? 16 : cap * 2;
        entries = realloc(entries,
                          sizeof(LeaderboardEntry) * cap);
    }
    strncpy(entries[count].name, player->name, MAX_NAME);
    entries[count].name[MAX_NAME] = '\0';
    entries[count].score = player->score;
    count++;

    // 3. 排序 + 重写 leaderboard.txt（从高到低）
    if (entries && count > 0) {
        qsort(entries, count, sizeof(LeaderboardEntry),
              compare_scores_desc);
    }

    f = fopen(LEADERBOARD_FILE, "w");
    if (f) {
        for (int i = 0; i < count; i++) {
            fprintf(f, "%s %d\n",
                    entries[i].name, entries[i].score);
        }
        fclose(f);
    }

    /* ---------- 第一张结算画面：Game Over + 新纪录提示 ---------- */
    clear();
    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax);

    const char *msg = "GAME OVER";
    mvprintw(2, (xmax - (int)strlen(msg)) / 2, "%s", msg);

    char buf1[64];
    snprintf(buf1, sizeof(buf1), "Final score: %d", player->score);
    mvprintw(4, (xmax - (int)strlen(buf1)) / 2, "%s", buf1);

    char buf2[64];
    snprintf(buf2, sizeof(buf2), "Player: %s", player->name);
    mvprintw(5, (xmax - (int)strlen(buf2)) / 2, "%s", buf2);

    if (new_record) {
        const char *rec = "Congratulations! NEW HIGH SCORE!";
        mvprintw(7, (xmax - (int)strlen(rec)) / 2, "%s", rec);

        char buf3[64];
        snprintf(buf3, sizeof(buf3),
                 "Well done, %s!", player->name);
        mvprintw(9, (xmax - (int)strlen(buf3)) / 2, "%s", buf3);
    } else {
        const char *tip = "Nice run! Try to beat the record next time.";
        mvprintw(7, (xmax - (int)strlen(tip)) / 2, "%s", tip);
    }

    mvprintw(ymax - 3, (xmax - 36) / 2,
             "Press any key to view leaderboard...");
    refresh();

    nodelay(stdscr, FALSE);
    getch();

    /* ---------- 第二张画面：排行榜 Top 10 ---------- */
    clear();
    mvprintw(2, 4, "===== Leaderboard (Top 10) =====");

    if (!entries || count == 0) {
        mvprintw(4, 6, "No records yet.");
    } else {
        int top = (count < 10) ? count : 10;
        for (int i = 0; i < top; i++) {
            mvprintw(5 + i, 6, "%2d. %-10s  %5d",
                     i + 1, entries[i].name, entries[i].score);
        }
    }

    mvprintw(ymax - 2, 4, "Press any key to exit.");
    refresh();
    getch();
    nodelay(stdscr, TRUE);

    free(entries);
}

/* ================== 速度控制 ================== */

int get_delay_for_level(int level) {
    int d = BASE_DELAY_MS - (level - 1) * LEVEL_SPEEDUP_MS;
    return (d < MIN_DELAY_MS) ? MIN_DELAY_MS : d;
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

    // 标题 + 输入名字
    draw_title_screen(&player);

    // 初始化棋盘、机器人、障碍物
    WINDOW *board = init_game(&player, &robot, &person, mines,
                              mine_count, &obstacle);
    reset_robot_body_from_lives(&robot, &player);

    // 初始人 + 地雷
    spawn_person(&robot, &person, mines, mine_count, &obstacle);
    spawn_mines(&robot, &person, mines, &mine_count,
                BASE_MINES, &obstacle);

    bool running = true;

    while (running && player.lives > 0) {
        int ch = getch();
        handle_input(&robot, ch, &running);
        if (!running) break;

        if (robot.ai_mode) {
            move_robot_ai(&robot, &person, mines,
                          mine_count, &obstacle);
        }

        clear_robot(board, &robot);
        move_robot(&robot);
        check_collision(&player, &robot, mines,
                        mine_count, &obstacle, &running);
        if (!running || player.lives <= 0) break;

        // ---- 救人逻辑 ----
        if (robot.pos.x == person.x && robot.pos.y == person.y) {
            player.score += 10;
            player.rescued++;

            if (player.rescued >= PEOPLE_PER_LEVEL) {
                player.level++;
                player.rescued = 0;

                int target = mine_count + MINES_PER_LEVEL;
                spawn_mines(&robot, &person, mines,
                            &mine_count, target, &obstacle);

                // 每升 5 级加一条命（加一个身体段）
                if (player.level % 5 == 0) {
                    player.lives++;
                    if (player.lives > MAX_BODY_SEGMENTS)
                        player.lives = MAX_BODY_SEGMENTS;
                    reset_robot_body_from_lives(&robot, &player);
                }
            }

            // 生成新的“人”
            spawn_person(&robot, &person, mines,
                         mine_count, &obstacle);
        }

        // ---- 重画棋盘 ----
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

    // 结算 + 新纪录提示 + 排行榜
    game_over_screen(&player);

    delwin(board);
    endwin();
    return 0;
}
