#include "raylib.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* ================== 基本设置 ================== */

#define BOARD_ROWS 20
#define BOARD_COLS 50

#define TILE_SIZE   24
#define PANEL_WIDTH 380
#define WINDOW_WIDTH  (PANEL_WIDTH + BOARD_COLS*TILE_SIZE + 40)
#define WINDOW_HEIGHT (BOARD_ROWS*TILE_SIZE + 80)

#define ROBOT_BODY 'O'
#define PERSON     'P'
#define MINE       'X'
#define OBSTACLE   '#'

#define INITIAL_LIVES      3
#define PEOPLE_PER_LEVEL   5

#define MAX_MINES          50
#define BASE_MINES         5
#define MINES_PER_LEVEL    2

#define BASE_DELAY_MS      400
#define MIN_DELAY_MS       50

#define INVINCIBLE_TICKS   10

#define MAX_NAME           20
#define LEADERBOARD_FILE   "leaderboard.txt"
#define MAX_LEADERBOARD    50

#define MAX_BODY_SEGMENTS  20
#define BOMB_RADIUS        5
#define BOMB_DURATION      0.6f   // 秒

typedef struct {
    int x;
    int y;
} Position;

typedef struct {
    Position pos;           // 头
    char     direction;     // 'N','S','E','W'
    bool     ai_mode;
    bool     invincible;
    int      invincible_ticks;

    int      body_length;   // 段数（不含头）
    Position body[MAX_BODY_SEGMENTS];
} Robot;

typedef struct {
    char name[MAX_NAME + 1];
    int  score;
    int  lives;
    int  level;
    int  rescued;
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
    int  level;
} LeaderboardEntry;

typedef enum {
    STATE_PLAYING,
    STATE_WAIT_CONTINUE,
    STATE_GAME_OVER,
    STATE_LEADERBOARD,
    STATE_EXIT
} GameState;

/* ============ 小工具函数 ============ */

static void DirectionToDelta(char dir, int *dx, int *dy) {
    *dx = 0; *dy = 0;
    switch (dir) {
        case 'N': *dx = 0;  *dy = -1; break;
        case 'S': *dx = 0;  *dy = 1;  break;
        case 'W': *dx = -1; *dy = 0;  break;
        case 'E': *dx = 1;  *dy = 0;  break;
        default:  *dx = 0;  *dy = 0;  break;
    }
}

static void InitObstacle(CrossObstacle *obs) {
    obs->width    = 11;
    obs->height   = 11;
    obs->center_x = BOARD_COLS / 2;
    obs->center_y = BOARD_ROWS / 2;
}

static bool IsObstacle(const CrossObstacle *obs, int x, int y) {
    int cx = obs->center_x;
    int cy = obs->center_y;
    int half_w = obs->width / 2;
    int half_h = obs->height / 2;

    if (y == cy && x >= cx - half_w && x <= cx + half_w) return true;
    if (x == cx && y >= cy - half_h && y <= cy + half_h) return true;
    return false;
}

static bool IsMineAt(const Position *mines, int mine_count, int x, int y) {
    for (int i = 0; i < mine_count; i++) {
        if (mines[i].x == x && mines[i].y == y) return true;
    }
    return false;
}

/* 寻找尽量靠近 (10,10) 的安全出生点 */
static Position FindSafeSpawn(const Position *mines, int mine_count,
                              const CrossObstacle *obs) {
    int target_x = 10;
    int target_y = 10;

    Position best = { BOARD_COLS/2, BOARD_ROWS/2 };
    int best_dist = 1000000;

    for (int y = 2; y < BOARD_ROWS - 2; y++) {
        for (int x = 2; x < BOARD_COLS - 2; x++) {
            if (IsObstacle(obs, x, y)) continue;
            if (mines && IsMineAt(mines, mine_count, x, y)) continue;

            int dist = abs(x - target_x) + abs(y - target_y);
            if (dist < best_dist) {
                best_dist = dist;
                best.x = x;
                best.y = y;
            }
        }
    }
    return best;
}

/* 根据生命数重建蛇身 */
static void ResetRobotBodyFromLives(Robot *robot, const Player *player) {
    int len = player->lives;
    if (len < 0) len = 0;
    if (len > MAX_BODY_SEGMENTS) len = MAX_BODY_SEGMENTS;
    robot->body_length = len;

    int dx, dy;
    DirectionToDelta(robot->direction, &dx, &dy);
    if (dx == 0 && dy == 0) { dx = -1; dy = 0; }

    for (int i = 0; i < robot->body_length; i++) {
        int bx = robot->pos.x - dx * (i + 1);
        int by = robot->pos.y - dy * (i + 1);

        if (bx <= 1 || bx >= BOARD_COLS - 2 ||
            by <= 1 || by >= BOARD_ROWS - 2) {
            bx = robot->pos.x;
            by = robot->pos.y;
        }
        robot->body[i].x = bx;
        robot->body[i].y = by;
    }
}

/* 每一关的移动时间间隔（秒） */
static float GetMoveIntervalSec(int level) {
    int delay = BASE_DELAY_MS;
    for (int i = 1; i < level && delay > MIN_DELAY_MS; i++) {
        delay /= 2;
    }
    if (delay < MIN_DELAY_MS) delay = MIN_DELAY_MS;
    return delay / 1000.0f;
}

/* ============ 生成 & 绘制地图元素 ============ */

static void SpawnMines(const Robot *robot, const Position *person,
                       Position *mines, int *mine_count,
                       int target_count, const CrossObstacle *obs) {
    if (target_count > MAX_MINES) target_count = MAX_MINES;

    while (*mine_count < target_count) {
        int x = 1 + rand() % (BOARD_COLS - 2);
        int y = 1 + rand() % (BOARD_ROWS - 2);

        if (x == robot->pos.x && y == robot->pos.y) continue;
        if (person && x == person->x && y == person->y) continue;
        if (IsObstacle(obs, x, y)) continue;
        if (IsMineAt(mines, *mine_count, x, y)) continue;

        mines[*mine_count].x = x;
        mines[*mine_count].y = y;
        (*mine_count)++;
    }
}

static void SpawnPerson(const Robot *robot, Position *person,
                        const Position *mines, int mine_count,
                        const CrossObstacle *obs) {
    while (1) {
        int x = 1 + rand() % (BOARD_COLS - 2);
        int y = 1 + rand() % (BOARD_ROWS - 2);

        if (x == robot->pos.x && y == robot->pos.y) continue;
        if (IsMineAt(mines, mine_count, x, y)) continue;
        if (IsObstacle(obs, x, y)) continue;

        person->x = x;
        person->y = y;
        break;
    }
}

/* ============ 机器人移动 & AI ============ */

static void MoveRobot(Robot *robot) {
    int dx, dy;
    DirectionToDelta(robot->direction, &dx, &dy);

    for (int i = robot->body_length - 1; i > 0; i--) {
        robot->body[i] = robot->body[i - 1];
    }
    if (robot->body_length > 0) {
        robot->body[0] = robot->pos;
    }

    robot->pos.x += dx;
    robot->pos.y += dy;
}

typedef struct { int x, y; } Node;

static bool IsBlockedCell(int x, int y,
                          const Position *mines, int mine_count,
                          const CrossObstacle *obs) {
    if (x <= 0 || x >= BOARD_COLS - 1 ||
        y <= 0 || y >= BOARD_ROWS - 1)
        return true;
    if (IsObstacle(obs, x, y)) return true;
    if (IsMineAt(mines, mine_count, x, y)) return true;
    return false;
}

static bool BFSNextDirection(const Robot *robot, const Position *person,
                             const Position *mines, int mine_count,
                             const CrossObstacle *obs,
                             char *out_dir) {
    bool visited[BOARD_ROWS][BOARD_COLS] = {false};
    Position parent[BOARD_ROWS][BOARD_COLS];

    for (int y = 0; y < BOARD_ROWS; y++)
        for (int x = 0; x < BOARD_COLS; x++)
            parent[y][x].x = parent[y][x].y = -1;

    Node queue[BOARD_ROWS*BOARD_COLS];
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
                ny < 0 || ny >= BOARD_ROWS) continue;
            if (visited[ny][nx]) continue;
            if (IsBlockedCell(nx, ny, mines, mine_count, obs)) continue;

            visited[ny][nx] = true;
            parent[ny][nx].x = cur.x;
            parent[ny][nx].y = cur.y;
            queue[back++] = (Node){nx, ny};
        }
    }

    if (!found) return false;

    int cx = tx, cy = ty;
    int px = parent[cy][cx].x;
    int py = parent[cy][cx].y;
    if (px == -1 && py == -1) return false;

    while (!(px == sx && py == sy)) {
        cx = px; cy = py;
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

static void MoveRobotAI(Robot *robot, const Position *person,
                        const Position *mines, int mine_count,
                        const CrossObstacle *obs) {
    if (!person) return;
    char dir;
    if (BFSNextDirection(robot, person, mines, mine_count, obs, &dir)) {
        robot->direction = dir;
        return;
    }
    // fallback：随机找一个可行方向
    char cands[4] = {'N','S','E','W'};
    for (int k = 0; k < 4; k++) {
        int i = rand()%4;
        int dx, dy;
        DirectionToDelta(cands[i], &dx, &dy);
        int nx = robot->pos.x + dx;
        int ny = robot->pos.y + dy;
        if (!IsBlockedCell(nx, ny, mines, mine_count, obs)) {
            robot->direction = cands[i];
            return;
        }
    }
}

/* ============ 碰撞 & 掉命 ============ */

static void CheckCollision(Player *player, Robot *robot,
                           const Position *mines, int mine_count,
                           const CrossObstacle *obs,
                           bool *life_lost, GameState *state) {
    *life_lost = false;

    int x = robot->pos.x;
    int y = robot->pos.y;

    bool hit_wall = (x <= 0 || x >= BOARD_COLS - 1 ||
                     y <= 0 || y >= BOARD_ROWS - 1);
    bool hit_mine = IsMineAt(mines, mine_count, x, y);
    bool hit_obs  = IsObstacle(obs, x, y);

    bool deadly = hit_wall || hit_mine || hit_obs;

    if (deadly && !robot->invincible) {
        player->lives--;
        if (player->lives <= 0) {
            *state = STATE_GAME_OVER;
            return;
        }

        robot->invincible       = true;
        robot->invincible_ticks = INVINCIBLE_TICKS;

        Position spawn = FindSafeSpawn(mines, mine_count, obs);
        robot->pos = spawn;
        ResetRobotBodyFromLives(robot, player);

        *life_lost = true;
        *state = STATE_WAIT_CONTINUE;
    }

    if (robot->invincible) {
        robot->invincible_ticks--;
        if (robot->invincible_ticks <= 0) robot->invincible = false;
    }
}

/* ============ 炸弹：标记 + 定时删除 ============ */

static void StartBombIfPossible(Player *player, const Robot *robot,
                                const Position *mines, int mine_count,
                                bool *bombActive, bool bombMarks[MAX_MINES],
                                float *bombTimer) {
    if (player->level <= 10) return;
    if (*bombActive) return;

    // 先扣 5 级
    player->level -= 5;
    if (player->level < 1) player->level = 1;

    int cx = robot->pos.x;
    int cy = robot->pos.y;

    for (int i = 0; i < mine_count; i++) {
        bool inRange =
            abs(mines[i].x - cx) <= BOMB_RADIUS &&
            abs(mines[i].y - cy) <= BOMB_RADIUS;
        bombMarks[i] = inRange;
    }
    *bombActive = true;
    *bombTimer  = 0.0f;
}

/* 删除被标记的雷（bombMarks==true） */
static void RemoveBombedMines(Position *mines, int *mine_count,
                              bool bombMarks[MAX_MINES]) {
    int w = 0;
    for (int i = 0; i < *mine_count; i++) {
        if (bombMarks[i]) continue;
        if (w != i) mines[w] = mines[i];
        w++;
    }
    *mine_count = w;
}

/* ============ 排行榜：载入 + 更新 ============ */

static int CompareScoresDesc(const void *a, const void *b) {
    const LeaderboardEntry *ea = (const LeaderboardEntry*)a;
    const LeaderboardEntry *eb = (const LeaderboardEntry*)b;
    return eb->score - ea->score;
}

static void LoadAndUpdateLeaderboard(const Player *player,
                                     LeaderboardEntry entries[],
                                     int *count, bool *newRecord) {
    *count = 0;
    *newRecord = false;

    FILE *f = fopen(LEADERBOARD_FILE, "r");
    if (f) {
        while (*count < MAX_LEADERBOARD) {
            char name[MAX_NAME+1];
            int score, level;
            if (fscanf(f, "%20s %d %d", name, &score, &level) != 3) break;
            strncpy(entries[*count].name, name, MAX_NAME);
            entries[*count].name[MAX_NAME] = '\0';
            entries[*count].score = score;
            entries[*count].level = level;
            (*count)++;
        }
        fclose(f);
    }

    int best = -1;
    for (int i = 0; i < *count; i++)
        if (entries[i].score > best) best = entries[i].score;
    *newRecord = (*count == 0 || player->score > best);

    // 把当前成绩加入
    if (*count < MAX_LEADERBOARD) {
        strncpy(entries[*count].name, player->name, MAX_NAME);
        entries[*count].name[MAX_NAME] = '\0';
        entries[*count].score = player->score;
        entries[*count].level = player->level;
        (*count)++;
    } else {
        // 已满：只在比最后一个高时替换
        if (player->score > entries[*count-1].score) {
            strncpy(entries[*count-1].name, player->name, MAX_NAME);
            entries[*count-1].name[MAX_NAME] = '\0';
            entries[*count-1].score = player->score;
            entries[*count-1].level = player->level;
        }
    }

    qsort(entries, *count, sizeof(LeaderboardEntry), CompareScoresDesc);

    // 写回文件
    f = fopen(LEADERBOARD_FILE, "w");
    if (f) {
        for (int i = 0; i < *count; i++) {
            fprintf(f, "%s %d %d\n",
                    entries[i].name, entries[i].score, entries[i].level);
        }
        fclose(f);
    }
}

/* ============ 绘制 UI ============ */

static void DrawBoardGrid(int offsetX, int offsetY) {
    DrawRectangleLines(offsetX-1, offsetY-1,
                       BOARD_COLS*TILE_SIZE+2,
                       BOARD_ROWS*TILE_SIZE+2,
                       LIGHTGRAY);
}

static void DrawObstacle(const CrossObstacle *obs,
                         int offsetX, int offsetY) {
    for (int y = 1; y < BOARD_ROWS-1; y++) {
        for (int x = 1; x < BOARD_COLS-1; x++) {
            if (IsObstacle(obs, x, y)) {
                int px = offsetX + x*TILE_SIZE;
                int py = offsetY + y*TILE_SIZE;
                DrawRectangle(px, py, TILE_SIZE, TILE_SIZE, GOLD);
            }
        }
    }
}

static void DrawMines(const Position *mines, int mine_count,
                      int offsetX, int offsetY,
                      bool bombActive, bool bombMarks[MAX_MINES],
                      float bombTimer) {
    for (int i = 0; i < mine_count; i++) {
        int px = offsetX + mines[i].x * TILE_SIZE;
        int py = offsetY + mines[i].y * TILE_SIZE;

        Color c = RED;
        if (bombActive && bombMarks[i]) {
            int flash = (int)(bombTimer * 20.0f) % 2;
            c = flash ? YELLOW : (Color){40, 40, 40, 255};
        }
        DrawCircle(px + TILE_SIZE/2, py + TILE_SIZE/2,
                   TILE_SIZE*0.35f, c);
    }
}

static void DrawPerson(const Position *person,
                       int offsetX, int offsetY) {
    int px = offsetX + person->x * TILE_SIZE;
    int py = offsetY + person->y * TILE_SIZE;
    DrawCircle(px + TILE_SIZE/2, py + TILE_SIZE/2,
               TILE_SIZE*0.35f, GREEN);
}

static void DrawRobot(const Robot *robot,
                      int offsetX, int offsetY) {
    // 无敌时闪烁
    if (robot->invincible && (robot->invincible_ticks % 2 == 1))
        return;

    // 身体
    for (int i = 0; i < robot->body_length; i++) {
        int bx = robot->body[i].x;
        int by = robot->body[i].y;
        int px = offsetX + bx*TILE_SIZE;
        int py = offsetY + by*TILE_SIZE;
        DrawRectangle(px+4, py+4,
                      TILE_SIZE-8, TILE_SIZE-8,
                      SKYBLUE);
    }

    // 头
    int hx = offsetX + robot->pos.x*TILE_SIZE;
    int hy = offsetY + robot->pos.y*TILE_SIZE;
    DrawRectangle(hx+3, hy+3,
                  TILE_SIZE-6, TILE_SIZE-6,
                  BLUE);
}

/* ============ 入口：主程序 ============ */

int main(void) {
    srand((unsigned int)time(NULL));

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
               "Rescue Bot (raylib version)");
    SetTargetFPS(60);

    Player player = {0};
    Robot  robot  = {0};
    Position person = {0};
    Position mines[MAX_MINES];
    int mine_count = 0;
    CrossObstacle obstacle;
    InitObstacle(&obstacle);

    // 简单的“输入名字”界面
    const int fontSize = 20;
    char nameBuf[MAX_NAME+1] = {0};
    int nameLen = 0;
    bool nameDone = false;

    while (!nameDone && !WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(DARKGRAY);

        DrawText("Rescue Bot: Snake on a Minefield", 40, 40, 28, RAYWHITE);

        DrawText("Enter your name (max 20 chars), press ENTER to confirm.",
                 40, 100, fontSize, RAYWHITE);

        DrawRectangle(40, 140, 400, 40, (Color){30,30,30,255});
        DrawRectangleLines(40, 140, 400, 40, RAYWHITE);
        DrawText(TextFormat("> %s", nameBuf), 50, 150, fontSize, SKYBLUE);

        DrawText("ESC to quit", 40, 210, fontSize, GRAY);

        EndDrawing();

        int c = GetCharPressed();
        while (c > 0) {
            if (c >= 32 && c <= 126 && nameLen < MAX_NAME) {
                nameBuf[nameLen++] = (char)c;
                nameBuf[nameLen] = '\0';
            }
            c = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE) && nameLen > 0) {
            nameLen--;
            nameBuf[nameLen] = '\0';
        }
        if (IsKeyPressed(KEY_ENTER)) {
            nameDone = true;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            CloseWindow();
            return 0;
        }
    }

    if (nameLen == 0) strcpy(player.name, "Player");
    else strncpy(player.name, nameBuf, MAX_NAME);
    player.name[MAX_NAME] = '\0';
    player.score   = 0;
    player.lives   = INITIAL_LIVES;
    player.level   = 1;
    player.rescued = 0;

    // 初始化机器人 & 地图
    robot.direction = 'W';
    robot.ai_mode   = true;
    robot.invincible = false;
    robot.invincible_ticks = 0;

    Position spawn = FindSafeSpawn(NULL, 0, &obstacle);
    robot.pos = spawn;
    ResetRobotBodyFromLives(&robot, &player);

    mine_count = 0;
    SpawnPerson(&robot, &person, mines, mine_count, &obstacle);
    SpawnMines(&robot, &person, mines, &mine_count, BASE_MINES, &obstacle);

    GameState state = STATE_PLAYING;
    float moveTimer = 0.0f;

    // 炸弹状态
    bool bombActive = false;
    bool bombMarks[MAX_MINES] = {0};
    float bombTimer = 0.0f;

    // 排行榜
    LeaderboardEntry lbEntries[MAX_LEADERBOARD];
    int  lbCount = 0;
    bool newRecord = false;
    bool leaderboardReady = false;

    while (!WindowShouldClose() && state != STATE_EXIT) {
        float dt = GetFrameTime();

        int boardOffsetX = PANEL_WIDTH + 20;
        int boardOffsetY = (WINDOW_HEIGHT - BOARD_ROWS*TILE_SIZE)/2;

        /* ------- 逻辑更新 ------- */

        if (state == STATE_PLAYING) {
            // 输入：切换 AI / 手动
            if (IsKeyPressed(KEY_M)) {
                robot.ai_mode = !robot.ai_mode;
            }

            // 手动方向（只改变方向，不立即移动）
            if (!robot.ai_mode) {
                if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    robot.direction = 'N';
                if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  robot.direction = 'S';
                if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  robot.direction = 'W';
                if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) robot.direction = 'E';
            }

            // 炸弹
            if (IsKeyPressed(KEY_SPACE)) {
                StartBombIfPossible(&player, &robot, mines, mine_count,
                                    &bombActive, bombMarks, &bombTimer);
            }

            // AI 决策
            if (robot.ai_mode) {
                MoveRobotAI(&robot, &person, mines, mine_count, &obstacle);
            }

            // 控制移动节奏
            moveTimer += dt;
            float interval = GetMoveIntervalSec(player.level);
            while (moveTimer >= interval) {
                moveTimer -= interval;

                MoveRobot(&robot);

                bool life_lost = false;
                CheckCollision(&player, &robot, mines, mine_count,
                               &obstacle, &life_lost, &state);
                if (state != STATE_PLAYING) break;

                // 吃到人
                if (robot.pos.x == person.x && robot.pos.y == person.y) {
                    player.score += 10;
                    player.rescued++;

                    if (player.rescued >= PEOPLE_PER_LEVEL) {
                        player.level++;
                        player.rescued = 0;

                        int target = mine_count + MINES_PER_LEVEL;
                        SpawnMines(&robot, &person, mines, &mine_count,
                                   target, &obstacle);

                        if (player.level % 5 == 0) {
                            player.lives++;
                            if (player.lives > MAX_BODY_SEGMENTS)
                                player.lives = MAX_BODY_SEGMENTS;
                            ResetRobotBodyFromLives(&robot, &player);
                        }
                    }

                    SpawnPerson(&robot, &person, mines, mine_count, &obstacle);
                }
            }

            // 炸弹计时：到时间删除雷
            if (bombActive) {
                bombTimer += dt;
                if (bombTimer >= BOMB_DURATION) {
                    RemoveBombedMines(mines, &mine_count, bombMarks);
                    bombActive = false;
                    for (int i = 0; i < MAX_MINES; i++) bombMarks[i] = false;
                }
            }

            // lives <=0 时切到 GAME_OVER
            if (player.lives <= 0 && state != STATE_GAME_OVER) {
                state = STATE_GAME_OVER;
            }

            // 如果要结束游戏，预先准备排行榜
            if (state == STATE_GAME_OVER && !leaderboardReady) {
                LoadAndUpdateLeaderboard(&player,
                                         lbEntries, &lbCount, &newRecord);
                leaderboardReady = true;
            }
        }
        else if (state == STATE_WAIT_CONTINUE) {
            if (IsKeyPressed(KEY_Y)) {
                state = STATE_PLAYING;
            }
            if (IsKeyPressed(KEY_Q)) {
                state = STATE_GAME_OVER;
                if (!leaderboardReady) {
                    LoadAndUpdateLeaderboard(&player,
                                             lbEntries, &lbCount, &newRecord);
                    leaderboardReady = true;
                }
            }
        }
        else if (state == STATE_GAME_OVER) {
            // 在 GAME_OVER 画面按任意键进入排行榜
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) ||
                IsKeyPressed(KEY_Y) || IsKeyPressed(KEY_Q)) {
                state = STATE_LEADERBOARD;
            }
        }
        else if (state == STATE_LEADERBOARD) {
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
                state = STATE_EXIT;
            }
        }

        /* ------- 绘制 ------- */

        BeginDrawing();
        ClearBackground((Color){25,25,25,255});

        if (state == STATE_PLAYING || state == STATE_WAIT_CONTINUE) {
            // 左侧信息面板
            DrawRectangle(20, 40, PANEL_WIDTH-40,
                          WINDOW_HEIGHT-80, (Color){30,30,30,255});

            int tx = 40;
            int ty = 60;
            int fs = 20;

            DrawText(TextFormat("Player: %s", player.name),
                     tx, ty, fs, RAYWHITE); ty += 30;
            DrawText(TextFormat("Score : %d", player.score),
                     tx, ty, fs, RAYWHITE); ty += 30;
            DrawText(TextFormat("Level : %d", player.level),
                     tx, ty, fs, RAYWHITE); ty += 30;
            DrawText(TextFormat("Lives : %d", player.lives),
                     tx, ty, fs, RAYWHITE); ty += 30;
            DrawText(TextFormat("Mode  : %s",
                     robot.ai_mode ? "AI" : "Manual"),
                     tx, ty, fs, RAYWHITE); ty += 40;

            DrawText("Description:", tx, ty, fs, SKYBLUE); ty += 24;
            DrawText("Guide a snake-like robot to rescue", tx, ty, 16, LIGHTGRAY); ty += 20;
            DrawText("people on a minefield. Avoid mines,", tx, ty, 16, LIGHTGRAY); ty += 20;
            DrawText("walls and the cross obstacle (#).", tx, ty, 16, LIGHTGRAY); ty += 20;
            DrawText("Every 5 people -> level up.", tx, ty, 16, LIGHTGRAY); ty += 20;
            DrawText("Every 5 levels -> +1 life.", tx, ty, 16, LIGHTGRAY); ty += 30;

            DrawText("Controls:", tx, ty, fs, SKYBLUE); ty += 24;
            DrawText("Arrows/WASD: move (Manual)", tx, ty, 16, LIGHTGRAY); ty += 20;
            DrawText("M: toggle AI / Manual",         tx, ty, 16, LIGHTGRAY); ty += 20;
            DrawText("SPACE (lvl>10): bomb mines",   tx, ty, 16, LIGHTGRAY); ty += 20;
            DrawText("Q: quit (from wait/game over)",tx, ty, 16, LIGHTGRAY); ty += 24;

            if (state == STATE_WAIT_CONTINUE) {
                DrawText("You lost a life!", tx, ty, fs, RED); ty += 26;
                DrawText("Press Y to continue", tx, ty, 16, YELLOW); ty += 20;
                DrawText("Press Q to quit",     tx, ty, 16, YELLOW); ty += 20;
            }

            // 右边棋盘
            DrawBoardGrid(boardOffsetX, boardOffsetY);
            DrawObstacle(&obstacle, boardOffsetX, boardOffsetY);
            DrawMines(mines, mine_count, boardOffsetX, boardOffsetY,
                      bombActive, bombMarks, bombTimer);
            DrawPerson(&person, boardOffsetX, boardOffsetY);
            DrawRobot(&robot, boardOffsetX, boardOffsetY);
        }
        else if (state == STATE_GAME_OVER) {
            const char *msg = "GAME OVER";
            int w = MeasureText(msg, 40);
            DrawText(msg, (WINDOW_WIDTH-w)/2, 120, 40, RAYWHITE);

            char buf[128];
            sprintf(buf, "Final score: %d", player.score);
            w = MeasureText(buf, 26);
            DrawText(buf, (WINDOW_WIDTH-w)/2, 190, 26, RAYWHITE);

            sprintf(buf, "Player: %s (Level %d)", player.name, player.level);
            w = MeasureText(buf, 26);
            DrawText(buf, (WINDOW_WIDTH-w)/2, 225, 26, RAYWHITE);

            if (leaderboardReady && newRecord) {
                const char *rec = "Congratulations! NEW HIGH SCORE!";
                w = MeasureText(rec, 24);
                DrawText(rec, (WINDOW_WIDTH-w)/2, 270, 24, YELLOW);
            } else {
                const char *tip = "Nice run! Try to beat the record next time.";
                w = MeasureText(tip, 24);
                DrawText(tip, (WINDOW_WIDTH-w)/2, 270, 24, LIGHTGRAY);
            }

            const char *hint = "Press ENTER / SPACE to view leaderboard...";
            w = MeasureText(hint, 20);
            DrawText(hint, (WINDOW_WIDTH-w)/2, 330, 20, GRAY);
        }
        else if (state == STATE_LEADERBOARD) {
            const char *title = "LEADERBOARD - STATIC MINES MODE";
            int w = MeasureText(title, 32);
            DrawText(title, (WINDOW_WIDTH-w)/2, 60, 32, RAYWHITE);

            DrawText("Rank", 200, 130, 22, SKYBLUE);
            DrawText("Name", 280, 130, 22, SKYBLUE);
            DrawText("Level", 520, 130, 22, SKYBLUE);
            DrawText("Score", 640, 130, 22, SKYBLUE);

            DrawLine(180, 160, WINDOW_WIDTH-180, 160, LIGHTGRAY);

            int top = lbCount < 10 ? lbCount : 10;
            for (int i = 0; i < top; i++) {
                int y = 180 + i*28;
                DrawText(TextFormat("%2d", i+1), 200, y, 20, RAYWHITE);
                DrawText(lbEntries[i].name,     280, y, 20, RAYWHITE);
                DrawText(TextFormat("%5d", lbEntries[i].level),
                         520, y, 20, RAYWHITE);
                DrawText(TextFormat("%5d", lbEntries[i].score),
                         640, y, 20, RAYWHITE);
            }

            const char *hint = "Press ENTER or ESC to quit.";
            w = MeasureText(hint, 20);
            DrawText(hint, (WINDOW_WIDTH-w)/2, WINDOW_HEIGHT-60, 20, GRAY);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
