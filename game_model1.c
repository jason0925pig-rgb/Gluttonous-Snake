#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <ncurses.h>

/*
 * 简单版本的“机器人救人躲地雷”游戏主程序
 * 尽量覆盖作业要求中的各个点，代码中有大量中文注释帮助理解。
 *
 * 编译方式（在 Ubuntu + ncurses 环境）：
 *      gcc game.c -o game -lncurses
 *
 * 运行方式：
 *      ./game
 */

/* -------------------- 一些基本参数 -------------------- */

#define BOARD_WIDTH   50      /* 棋盘宽度（不包含边框），单位：字符 */
#define BOARD_HEIGHT  20      /* 棋盘高度（不包含边框）             */

#define INITIAL_LIVES 3       /* 初始生命数 */
#define PEOPLE_PER_LEVEL 5    /* 每救 5 个人升一级 */

#define MAX_MINES     50      /* 地雷最多数量 */
#define BASE_MINES     5      /* 初始地雷数量 */
#define MINES_PER_LEVEL 2     /* 每升一级新增地雷数 */

#define MAX_NAME_LEN  31
#define LEADERBOARD_FILE "leaderboard.txt"

/* 游戏刷新时间相关：基准延迟（毫秒） */
#define BASE_DELAY_MS 200
#define LEVEL_SPEEDUP_MS 20
#define MIN_DELAY_MS 60

/* 棋盘左上角在屏幕上的偏移量，便于上面留空间显示信息栏 */
#define BOARD_OFFSET_X 2
#define BOARD_OFFSET_Y 3

/* 安全重生时的短暂停顿时间（毫秒） */
#define RESPAWN_PAUSE_MS 800

/* 无敌时间步数（简单创意扩展：玩家死后有短暂无敌） */
#define INVINCIBLE_TICKS 10

/* -------------------- 结构体定义 -------------------- */

/* 玩家信息 */
typedef struct {
    char name[MAX_NAME_LEN + 1];
    int score;
    int lives;
    int level;
    int rescued;   /* 当前等级已经救的人数，达到 5 则 level++ */
} Player;

/* 机器人位置和状态 */
typedef struct {
    int x;      /* 机器人头部的 x（棋盘坐标，0~BOARD_WIDTH-1）  */
    int y;      /* 机器人头部的 y（棋盘坐标，0~BOARD_HEIGHT-1） */
    int dx;     /* 当前移动方向的 x 分量：-1 左，1 右，0 不变      */
    int dy;     /* 当前移动方向的 y 分量：-1 上，1 下，0 不变      */
    bool ai_mode;           /* true = AI 模式，false = 手动模式 */
    bool invincible;        /* 是否处于无敌状态（创意扩展）     */
    int invincible_ticks;   /* 无敌还剩多少步                   */
} Robot;

/* 被救的“人”（一次只出现一个） */
typedef struct {
    int x;
    int y;
    bool active;    /* true = 有人存在，false = 暂无（应重新生成） */
} Person;

/* 地雷 */
typedef struct {
    int x;
    int y;
} Mine;

/* 排行榜条目 */
typedef struct {
    char name[MAX_NAME_LEN + 1];
    int score;
} LeaderboardEntry;

/* -------------------- 全局变量（简单起见） -------------------- */

Mine *mines = NULL;     /* 动态数组存储地雷 */
int mine_count = 0;     /* 当前地雷数量 */

/* -------------------- 辅助函数声明 -------------------- */

void init_colors(void);
void draw_title_screen(Player *player);
void draw_board_border(void);
void draw_obstacle(void);
bool is_obstacle_cell(int x, int y);

void place_robot_center(Robot *robot);
void draw_robot(const Robot *robot);
void erase_robot(const Robot *robot);

void random_place_person(const Robot *robot, Person *person);
void draw_person(const Person *person);

void init_mines(Player *player, const Robot *robot, const Person *person);
void add_mines_for_level(Player *player, const Robot *robot, const Person *person);
void draw_mines(void);
bool is_mine_at(int x, int y);

int  handle_input(Robot *robot, bool *running);
void move_robot_ai(Robot *robot, const Person *person);
bool is_wall(int x, int y);

void update_robot_position(Player *player, Robot *robot, Person *person, bool *running);
bool collides_with_obstacle(int x, int y);

void draw_status_bar(const Player *player, const Robot *robot);
void game_over_screen(const Player *player);
void show_leaderboard(void);
void update_leaderboard(const Player *player);

int  get_delay_for_level(int level);

/* -------------------- 颜色初始化 -------------------- */

/*
 * 初始化颜色对（color pair）
 * pair 1: 机器人（白色）
 * pair 2: 人（绿色）
 * pair 3: 地雷（红色）
 * pair 4: 障碍物（黄色）
 * pair 5: 信息栏文本（青色）
 * 注：黑色/白色/红色/绿色/黄色/青色 都属于不同颜色，满足“至少 3 种颜色”的要求。
 */
void init_colors(void) {
    if (!has_colors()) {
        return;
    }
    start_color();
    /* 前景色, 背景色 */
    init_pair(1, COLOR_WHITE,  COLOR_BLACK);   /* 机器人 */
    init_pair(2, COLOR_GREEN,  COLOR_BLACK);   /* 人 */
    init_pair(3, COLOR_RED,    COLOR_BLACK);   /* 地雷 */
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);   /* 障碍物 */
    init_pair(5, COLOR_CYAN,   COLOR_BLACK);   /* 状态栏文字 */
}

/* -------------------- 标题界面 -------------------- */

/*
 * 标题界面：显示游戏名称、说明，读取玩家名字并初始化玩家数据。
 */
void draw_title_screen(Player *player) {
    clear();
    int maxy, maxx;
    getmaxyx(stdscr, maxy, maxx);

    const char *title = "Rescue Bot: Minesweeper Snake";
    mvprintw(3, (maxx - (int)strlen(title)) / 2, "%s", title);

    mvprintw(5, 4, "Description:");
    mvprintw(6, 6, "You control a small robot to rescue people on a minefield.");
    mvprintw(7, 6, "Avoid walls, mines and the central obstacle. Each rescued person");
    mvprintw(8, 6, "gives you score; every 5 rescued people increase your level.");

    mvprintw(10, 4, "Controls:");
    mvprintw(11, 6, "Arrow Keys : Move robot (Manual mode)");
    mvprintw(12, 6, "'m'        : Toggle Manual/AI mode");
    mvprintw(13, 6, "'q'        : Quit game");
    mvprintw(14, 6, "Robot continues moving in last direction in manual mode.");

    mvprintw(16, 4, "Press ENTER after inputting your name.");
    mvprintw(17, 4, "Player name (max %d chars): ", MAX_NAME_LEN);

    echo();             /* 打开回显用于输入名字 */
    curs_set(1);        /* 显示光标 */
    char name_buf[MAX_NAME_LEN + 1];
    memset(name_buf, 0, sizeof(name_buf));
    getnstr(name_buf, MAX_NAME_LEN);
    noecho();
    curs_set(0);

    if (strlen(name_buf) == 0) {
        strcpy(player->name, "Player");
    } else {
        strncpy(player->name, name_buf, MAX_NAME_LEN);
        player->name[MAX_NAME_LEN] = '\0';
    }

    player->score   = 0;
    player->lives   = INITIAL_LIVES;
    player->level   = 1;
    player->rescued = 0;

    mvprintw(19, 4, "Welcome, %s! Press any key to start...", player->name);
    nodelay(stdscr, FALSE);  /* 阻塞等待按键 */
    getch();
    nodelay(stdscr, TRUE);   /* 之后游戏主循环使用非阻塞输入 */
}

/* -------------------- 棋盘和障碍物 -------------------- */

/* 画棋盘边框（简单矩形，用 # 画） */
void draw_board_border(void) {
    int w = BOARD_WIDTH + 2;
    int h = BOARD_HEIGHT + 2;

    for (int x = 0; x < w; x++) {
        mvaddch(BOARD_OFFSET_Y,         BOARD_OFFSET_X + x, '#');
        mvaddch(BOARD_OFFSET_Y + h - 1, BOARD_OFFSET_X + x, '#');
    }
    for (int y = 0; y < h; y++) {
        mvaddch(BOARD_OFFSET_Y + y, BOARD_OFFSET_X,         '#');
        mvaddch(BOARD_OFFSET_Y + y, BOARD_OFFSET_X + w - 1, '#');
    }
}

/* 判断一个棋盘坐标是否是障碍物（十字架）的位置 */
bool is_obstacle_cell(int x, int y) {
    int cx = BOARD_WIDTH  / 2;
    int cy = BOARD_HEIGHT / 2;
    int arm_len = 5; /* 半臂长，十字架总宽/高大约 2*arm_len+1 ≈ 11 */

    /* 水平臂：y == cy, x 在 [cx-arm_len, cx+arm_len] */
    if (y == cy && x >= cx - arm_len && x <= cx + arm_len) {
        return true;
    }
    /* 垂直臂：x == cx, y 在 [cy-arm_len, cy+arm_len] */
    if (x == cx && y >= cy - arm_len && y <= cy + arm_len) {
        return true;
    }
    return false;
}

/* 在棋盘中画出十字架障碍物 */
void draw_obstacle(void) {
    if (has_colors()) attron(COLOR_PAIR(4));  /* 黄色 */
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (is_obstacle_cell(x, y)) {
                mvaddch(BOARD_OFFSET_Y + 1 + y,
                        BOARD_OFFSET_X + 1 + x,
                        'X');
            }
        }
    }
    if (has_colors()) attroff(COLOR_PAIR(4));
}

/* -------------------- 机器人相关 -------------------- */

/* 把机器人放到棋盘中央偏下一点，并设置初始方向/状态 */
void place_robot_center(Robot *robot) {
    robot->x = BOARD_WIDTH  / 2;
    robot->y = BOARD_HEIGHT / 2 + 3; /* 稍微偏下一点，避免刚好在障碍物上 */
    robot->dx = 1;   /* 默认向右 */
    robot->dy = 0;
    robot->ai_mode = false;  /* 默认手动模式 */
    robot->invincible = false;
    robot->invincible_ticks = 0;
}

/* 在屏幕上画出机器人（头 + 身体） */
void draw_robot(const Robot *robot) {
    int screen_x = BOARD_OFFSET_X + 1 + robot->x;
    int screen_y = BOARD_OFFSET_Y + 1 + robot->y;

    /* 根据方向选择不同的头部字符 */
    char head_char = '>';
    if (robot->dx == -1 && robot->dy == 0)      head_char = '<';
    else if (robot->dx == 1 && robot->dy == 0)  head_char = '>';
    else if (robot->dx == 0 && robot->dy == -1) head_char = '^';
    else if (robot->dx == 0 && robot->dy == 1)  head_char = 'v';

    /* 身体放在头的后面一格（简单模拟“蛇身”） */
    int body_x = robot->x - robot->dx;
    int body_y = robot->y - robot->dy;
    if (body_x < 0 || body_x >= BOARD_WIDTH || body_y < 0 || body_y >= BOARD_HEIGHT) {
        /* 身体超出棋盘就不画 */
    } else {
        int body_screen_x = BOARD_OFFSET_X + 1 + body_x;
        int body_screen_y = BOARD_OFFSET_Y + 1 + body_y;
        if (has_colors()) attron(COLOR_PAIR(1));
        mvaddch(body_screen_y, body_screen_x, 'O');
        if (has_colors()) attroff(COLOR_PAIR(1));
    }

    /* 画头 */
    if (has_colors()) attron(COLOR_PAIR(1));
    mvaddch(screen_y, screen_x, head_char);
    if (has_colors()) attroff(COLOR_PAIR(1));
}

/* 用空格擦掉机器人当前图像（下一帧重新画） */
void erase_robot(const Robot *robot) {
    int screen_x = BOARD_OFFSET_X + 1 + robot->x;
    int screen_y = BOARD_OFFSET_Y + 1 + robot->y;
    mvaddch(screen_y, screen_x, ' ');

    int body_x = robot->x - robot->dx;
    int body_y = robot->y - robot->dy;
    if (body_x >= 0 && body_x < BOARD_WIDTH && body_y >= 0 && body_y < BOARD_HEIGHT) {
        int body_screen_x = BOARD_OFFSET_X + 1 + body_x;
        int body_screen_y = BOARD_OFFSET_Y + 1 + body_y;
        mvaddch(body_screen_y, body_screen_x, ' ');
    }
}

/* -------------------- 人（待救对象）相关 -------------------- */

/* 随机生成一个“人”的位置，不能在机器人、地雷或障碍物上 */
void random_place_person(const Robot *robot, Person *person) {
    int x, y;
    while (1) {
        x = rand() % BOARD_WIDTH;
        y = rand() % BOARD_HEIGHT;

        /* 不能和机器人重合 */
        if (x == robot->x && y == robot->y) continue;
        if (is_obstacle_cell(x, y)) continue;
        if (is_mine_at(x, y)) continue;

        person->x = x;
        person->y = y;
        person->active = true;
        break;
    }
}

/* 画出人 */
void draw_person(const Person *person) {
    if (!person->active) return;
    int screen_x = BOARD_OFFSET_X + 1 + person->x;
    int screen_y = BOARD_OFFSET_Y + 1 + person->y;

    if (has_colors()) attron(COLOR_PAIR(2));
    mvaddch(screen_y, screen_x, 'P');
    if (has_colors()) attroff(COLOR_PAIR(2));
}

/* -------------------- 地雷相关 -------------------- */

/* 检查某坐标是否有地雷 */
bool is_mine_at(int x, int y) {
    for (int i = 0; i < mine_count; i++) {
        if (mines[i].x == x && mines[i].y == y) {
            return true;
        }
    }
    return false;
}

/* 初始生成地雷（根据玩家 level，用 BASE_MINES） */
void init_mines(Player *player, const Robot *robot, const Person *person) {
    if (mines != NULL) {
        free(mines);
        mines = NULL;
    }
    mine_count = 0;

    int target = BASE_MINES;
    if (target > MAX_MINES) target = MAX_MINES;

    mines = (Mine *)malloc(sizeof(Mine) * MAX_MINES);
    if (!mines) {
        endwin();
        fprintf(stderr, "Failed to allocate memory for mines\n");
        exit(EXIT_FAILURE);
    }

    while (mine_count < target) {
        int x = rand() % BOARD_WIDTH;
        int y = rand() % BOARD_HEIGHT;

        if (x == robot->x && y == robot->y) continue;
        if (person && person->active && x == person->x && y == person->y) continue;
        if (is_obstacle_cell(x, y)) continue;
        if (is_mine_at(x, y)) continue;

        mines[mine_count].x = x;
        mines[mine_count].y = y;
        mine_count++;
    }
}

/* 每升一级增加新的雷 */
void add_mines_for_level(Player *player, const Robot *robot, const Person *person) {
    int target = mine_count + MINES_PER_LEVEL;
    if (target > MAX_MINES) target = MAX_MINES;

    while (mine_count < target) {
        int x = rand() % BOARD_WIDTH;
        int y = rand() % BOARD_HEIGHT;

        if (x == robot->x && y == robot->y) continue;
        if (person && person->active && x == person->x && y == person->y) continue;
        if (is_obstacle_cell(x, y)) continue;
        if (is_mine_at(x, y)) continue;

        mines[mine_count].x = x;
        mines[mine_count].y = y;
        mine_count++;
    }
}

/* 画出所有地雷 */
void draw_mines(void) {
    if (has_colors()) attron(COLOR_PAIR(3));
    for (int i = 0; i < mine_count; i++) {
        int screen_x = BOARD_OFFSET_X + 1 + mines[i].x;
        int screen_y = BOARD_OFFSET_Y + 1 + mines[i].y;
        mvaddch(screen_y, screen_x, '*');
    }
    if (has_colors()) attroff(COLOR_PAIR(3));
}

/* -------------------- 输入处理与 AI -------------------- */

/*
 * 处理键盘输入
 * 返回值：最后读取的键值（可能是 ERR = 没有输入）
 * running 为 false 则表示要退出主循环
 */
int handle_input(Robot *robot, bool *running) {
    int ch = getch();
    if (ch == ERR) {
        return ERR;
    }

    switch (ch) {
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
            if (!robot->ai_mode) {
                robot->dx = 0;
                robot->dy = -1;
            }
            break;
        case KEY_DOWN:
        case 's':
        case 'S':
            if (!robot->ai_mode) {
                robot->dx = 0;
                robot->dy = 1;
            }
            break;
        case KEY_LEFT:
        case 'a':
        case 'A':
            if (!robot->ai_mode) {
                robot->dx = -1;
                robot->dy = 0;
            }
            break;
        case KEY_RIGHT:
        case 'd':
        case 'D':
            if (!robot->ai_mode) {
                robot->dx = 1;
                robot->dy = 0;
            }
            break;
        default:
            break;
    }

    return ch;
}

/* 判断是否撞墙（出界） */
bool is_wall(int x, int y) {
    if (x < 0 || x >= BOARD_WIDTH) return true;
    if (y < 0 || y >= BOARD_HEIGHT) return true;
    return false;
}

/* AI 模式：向最近的人简单追踪（先移动 x，再移动 y） */
void move_robot_ai(Robot *robot, const Person *person) {
    if (!person || !person->active) return;

    int target_x = person->x;
    int target_y = person->y;

    int new_dx = robot->dx;
    int new_dy = robot->dy;

    /* 简单策略：优先在 x 方向靠近，再在 y 方向靠近 */
    if (robot->x < target_x) {
        new_dx = 1;
        new_dy = 0;
    } else if (robot->x > target_x) {
        new_dx = -1;
        new_dy = 0;
    } else if (robot->y < target_y) {
        new_dx = 0;
        new_dy = 1;
    } else if (robot->y > target_y) {
        new_dx = 0;
        new_dy = -1;
    }

    /* 如果前方是墙/障碍/雷，就尝试改走别的方向（非常简单的绕行逻辑） */
    int trial_x = robot->x + new_dx;
    int trial_y = robot->y + new_dy;
    if (is_wall(trial_x, trial_y) || is_obstacle_cell(trial_x, trial_y) || is_mine_at(trial_x, trial_y)) {
        /* 尝试上下左右四个方向，找到一个安全的就用 */
        int dirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
        for (int i = 0; i < 4; i++) {
            int tx = robot->x + dirs[i][0];
            int ty = robot->y + dirs[i][1];
            if (!is_wall(tx, ty) && !is_obstacle_cell(tx, ty) && !is_mine_at(tx, ty)) {
                new_dx = dirs[i][0];
                new_dy = dirs[i][1];
                break;
            }
        }
    }

    robot->dx = new_dx;
    robot->dy = new_dy;
}

/* -------------------- 位置更新与碰撞检测 -------------------- */

/* 判断是否碰到障碍物 */
bool collides_with_obstacle(int x, int y) {
    return is_obstacle_cell(x, y);
}

/*
 * 更新机器人位置，并处理：
 *   - 撞墙 / 雷 / 障碍物：扣命、重生、可能结束游戏
 *   - 救人：加分、计数、升级、重生新的人和更多雷
 */
void update_robot_position(Player *player, Robot *robot, Person *person, bool *running) {
    /* 如果还没有方向（理论上不会发生），就不动 */
    if (robot->dx == 0 && robot->dy == 0) return;

    int new_x = robot->x + robot->dx;
    int new_y = robot->y + robot->dy;

    /* 检查碰撞 */
    bool hit_wall      = is_wall(new_x, new_y);
    bool hit_obstacle  = collides_with_obstacle(new_x, new_y);
    bool hit_mine      = is_mine_at(new_x, new_y);

    bool deadly_hit = (hit_wall || hit_obstacle || hit_mine);

    if (deadly_hit && !robot->invincible) {
        /* 碰到致命物体，且不处于无敌状态：扣命 */
        player->lives--;
        robot->invincible = true;
        robot->invincible_ticks = INVINCIBLE_TICKS;

        /* 让玩家有一点时间看到结果 */
        draw_status_bar(player, robot);
        refresh();
        napms(RESPAWN_PAUSE_MS);

        /* 生命用完 -> 游戏结束 */
        if (player->lives <= 0) {
            *running = false;
            return;
        }

        /* 重置机器人到中心 */
        erase_robot(robot);
        place_robot_center(robot);
        return;
    }

    /* 处于无敌时，也要减小无敌步数 */
    if (robot->invincible) {
        robot->invincible_ticks--;
        if (robot->invincible_ticks <= 0) {
            robot->invincible = false;
        }
    }

    /* 安全移动 */
    erase_robot(robot);
    robot->x = new_x;
    robot->y = new_y;

    /* 检查是否救到了人 */
    if (person->active && robot->x == person->x && robot->y == person->y) {
        person->active = false;
        player->score += 10;     /* 每救一个人 +10 分，可自行调整 */
        player->rescued++;

        /* 每救够 PEOPLE_PER_LEVEL 个，人就升级，速度变快，并多生成地雷 */
        if (player->rescued >= PEOPLE_PER_LEVEL) {
            player->level++;
            player->rescued = 0;
            add_mines_for_level(player, robot, person);
        }

        random_place_person(robot, person);
    }
}

/* -------------------- 状态栏与 Game Over -------------------- */

/* 在顶部打印玩家名字、分数、等级、生命等信息 */
void draw_status_bar(const Player *player, const Robot *robot) {
    int maxy, maxx;
    getmaxyx(stdscr, maxy, maxx);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "Player: %s  Score: %d  Level: %d  Lives: %d  Mode: %s  Invincible: %s",
             player->name, player->score, player->level, player->lives,
             robot->ai_mode ? "AI" : "Manual",
             robot->invincible ? "YES" : "NO");

    if (has_colors()) attron(COLOR_PAIR(5));
    mvhline(0, 0, ' ', maxx); /* 清空第一行 */
    mvprintw(0, 0, "%s", buf);
    if (has_colors()) attroff(COLOR_PAIR(5));

    /* 第二行用于提示 */
    mvhline(1, 0, ' ', maxx);
    mvprintw(1, 0, "Use Arrow Keys or WASD to move. 'm' toggles AI/Manual. 'q' to quit.");
}

/* -------------------- 排行榜相关 -------------------- */

/* 比较函数，用于 qsort 按得分从高到低排序 */
int compare_scores_desc(const void *a, const void *b) {
    const LeaderboardEntry *ea = (const LeaderboardEntry *)a;
    const LeaderboardEntry *eb = (const LeaderboardEntry *)b;
    return eb->score - ea->score;
}

/* 把当前玩家成绩写入 leaderboard 文件，之后读出并显示前几名 */
void update_leaderboard(const Player *player) {
    /* 1. 先追加当前成绩到文件 */
    FILE *f = fopen(LEADERBOARD_FILE, "a");
    if (f) {
        fprintf(f, "%s %d\n", player->name, player->score);
        fclose(f);
    }

    /* 2. 再读出全部内容，排序并显示（只显示前 10 名） */
    show_leaderboard();
}

/* 读取 leaderboard.txt 并显示 */
void show_leaderboard(void) {
    FILE *f = fopen(LEADERBOARD_FILE, "r");
    if (!f) {
        mvprintw(5, 4, "No leaderboard file yet.");
        return;
    }

    LeaderboardEntry *entries = NULL;
    int capacity = 0;
    int count = 0;

    char name_buf[MAX_NAME_LEN + 1];
    int score;

    while (fscanf(f, "%31s %d", name_buf, &score) == 2) {
        if (count >= capacity) {
            capacity = (capacity == 0) ? 16 : capacity * 2;
            entries = (LeaderboardEntry *)realloc(entries, sizeof(LeaderboardEntry) * capacity);
            if (!entries) {
                fclose(f);
                mvprintw(5, 4, "Failed to allocate memory for leaderboard.");
                return;
            }
        }
        strncpy(entries[count].name, name_buf, MAX_NAME_LEN);
        entries[count].name[MAX_NAME_LEN] = '\0';
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
        mvprintw(7 + i, 6, "%2d. %-10s  %5d", i + 1, entries[i].name, entries[i].score);
    }

    free(entries);
}

/* Game Over 画面 */
void game_over_screen(const Player *player) {
    clear();
    int maxy, maxx;
    getmaxyx(stdscr, maxy, maxx);

    const char *msg = "GAME OVER";
    mvprintw(maxy / 2 - 2, (maxx - (int)strlen(msg)) / 2, "%s", msg);

    char score_line[64];
    snprintf(score_line, sizeof(score_line), "Final score: %d", player->score);
    mvprintw(maxy / 2, (maxx - (int)strlen(score_line)) / 2, "%s", score_line);

    mvprintw(maxy / 2 + 2, (maxx - 30) / 2, "Your name: %s", player->name);
    mvprintw(maxy / 2 + 4, (maxx - 40) / 2, "Press any key to view leaderboard...");

    refresh();
    nodelay(stdscr, FALSE);
    getch();
    nodelay(stdscr, TRUE);

    clear();
    mvprintw(2, 4, "Your results have been saved to '%s'.", LEADERBOARD_FILE);
    update_leaderboard(player);

    mvprintw(LINES - 2, 4, "Press any key to exit.");
    refresh();
    getch();
}

/* -------------------- 其它辅助 -------------------- */

/* 根据等级计算当前帧之间的延迟时间（毫秒），等级越高越快 */
int get_delay_for_level(int level) {
    int delay = BASE_DELAY_MS - (level - 1) * LEVEL_SPEEDUP_MS;
    if (delay < MIN_DELAY_MS) delay = MIN_DELAY_MS;
    return delay;
}

/* -------------------- main 函数：游戏主循环 -------------------- */

int main(void) {
    /* 初始化随机数种子 */
    srand((unsigned int)time(NULL));

    /* 初始化 ncurses */
    initscr();              /* 初始化屏幕 */
    cbreak();               /* 关闭行缓冲，立即响应按键 */
    noecho();               /* 不在屏幕上回显按键 */
    keypad(stdscr, TRUE);   /* 开启方向键等扩展键 */
    curs_set(0);            /* 隐藏光标 */
    nodelay(stdscr, TRUE);  /* getch 非阻塞 */
    init_colors();          /* 初始化颜色 */

    Player player;
    Robot robot;
    Person person;

    /* 标题/开始界面：输入名字、初始化玩家 */
    draw_title_screen(&player);

    /* 初始化棋盘边框、障碍物、机器人、人、雷 */
    clear();
    draw_board_border();
    draw_obstacle();

    place_robot_center(&robot);

    person.active = false;
    random_place_person(&robot, &person);

    init_mines(&player, &robot, &person);

    bool running = true;
    int delay_ms = get_delay_for_level(player.level);

    /* 游戏主循环 */
    while (running && player.lives > 0) {
        /* 1. 处理键盘输入（非阻塞） */
        int ch = handle_input(&robot, &running);
        (void)ch; /* 把 ch 标记为已使用，避免编译器未使用警告 */

        /* 按 q 之后立刻退出，不再更新位置 */
        if (!running) {
            break;
        }

        /* 2. AI 模式时，让 AI 决定下一步方向 */
        if (robot.ai_mode) {
            move_robot_ai(&robot, &person);
        }

        /* 3. 更新机器人位置（包括碰撞检测、救人、升级等） */
        update_robot_position(&player, &robot, &person, &running);
        if (!running || player.lives <= 0) {
            break;
        }

        /* 4. 重新绘制整个场景：状态栏、边框、障碍物、地雷、人、机器人 */
        draw_status_bar(&player, &robot);
        draw_board_border();
        draw_obstacle();
        draw_mines();
        draw_person(&person);
        draw_robot(&robot);

        refresh();

        /* 5. 根据当前等级速度休眠一小段时间 */
        delay_ms = get_delay_for_level(player.level);
        napms(delay_ms);
    }

    /* 游戏结束画面 + 排行榜 */
    game_over_screen(&player);

    /* 清理资源 */
    if (mines) {
        free(mines);
        mines = NULL;
    }

    endwin();   /* 结束 ncurses 模式，恢复终端 */

    return 0;
}
