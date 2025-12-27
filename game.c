#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h> // transport file between code and system
#include <stdbool.h>
#include <stdio.h>

// ---Fixed---
#define BOARD_ROWS 20 // the height of the board
#define BOARD_COLS 50 // the width of the board
#define MAX_NAME   20 // the user's name can not over 20 characters

#define ROBOT_BODY 'O' // use "O" represent the body of robot
#define ROBOT_HEAD '^' // use "^" represent the head of robot
#define PERSON     'P' // use "P" represent the people need to be saved
#define MINE       'X' // use "X" represent the mine
#define OBSTACLE   '#' // use "#" represent the obstacle

#define INVINCIBLE_TICKS   10 // creativity: after robot die, it have 10 steps that no one can kill him
#define LEADERBOARD_FILE   "leaderboard.txt"

// ---Changed for diffirent difficulty of game---
#define INITIAL_LIVES      3 // how many times you can die initially
#define PEOPLE_PER_LEVEL   5 // how many people you need save for getting to next level
#define MAX_MINES          50 // the number of mines maximum can appear in this map
#define BASE_MINES         5 // how many mines initially the map have
#define MINES_PER_LEVEL    2 // how many mines will increase for increase one level

#define BASE_DELAY_MS      400 // Every step robot move takes 400ms
#define MIN_DELAY_MS       50 // the fastest speed of game can be

#define MAX_BODY_SEGMENTS  20 // the number of lives maximum for robot

// ---Struct---
typedef struct {
    int x;
    int y;
} Position;

typedef struct {
    Position pos; // position of head of the robot
    char     direction; // 'N','S','E','W'
    bool     ai_mode; // in ai mode or manual mode
    bool     invincible; // do robot in invincible period
    int      invincible_ticks;

    int      body_length;   // how many segment does robot have
    Position body[MAX_BODY_SEGMENTS];
} Robot;

typedef struct {
    char name[MAX_NAME + 1]; // +1 for the "\0" at the end
    int  score; // score the player get
    int  lives; // how many lives player have
    int  level; // what's the level player is
    int  rescued; // number of people robot saved
} Player;

typedef struct {
    int width; // describe the width of obstacle in center
    int height; // describe the height of obstacle in center
    int center_x;
    int center_y;
} CrossObstacle;

typedef struct {
    char name[MAX_NAME + 1]; // +1 for the "\0" at the end
    int  score; // score the player get
    int  level; // what's the level player is
} LeaderboardEntry;

// ---Color---
#define CP_ROBOT     1 // use these number to represent color of different situations
#define CP_PERSON    2
#define CP_MINE      3
#define CP_OBSTACLE  4
#define CP_STATUS    5
#define CP_BOARD_BG  6

void init_colors(void) {
    if (!has_colors()) return; // make sure it is availible to use color
    start_color(); // initiallize color function in ncurses
    use_default_colors(); // for use like "-1" which means the color of terminal

    init_pair(CP_ROBOT, COLOR_WHITE, COLOR_BLACK); // o> is white, background is black
    init_pair(CP_PERSON, COLOR_GREEN, COLOR_BLACK); // P is green, background is black
    init_pair(CP_MINE, COLOR_RED, COLOR_BLACK); // X is red, background is black
    init_pair(CP_OBSTACLE, COLOR_YELLOW, COLOR_BLACK); // # is yellow, background is black
    init_pair(CP_STATUS, COLOR_CYAN, -1); // text for status is cyan, background is the color of terminal
    init_pair(CP_BOARD_BG, COLOR_WHITE, COLOR_BLACK); // boundary of board is white, background is black
}

// ---All Function---
void draw_title_screen(Player *player);

void set_direction(Robot *robot, char dir);
void direction_to_delta(char dir, int *dx, int *dy);

void init_obstacle(CrossObstacle *obstacle);
bool is_obstacle_position(const CrossObstacle *obstacle, int x, int y);
void draw_obstacle(WINDOW *board, const CrossObstacle *obstacle);

bool is_mine_at(const Position *mines, int mine_count, int x, int y);

Position find_safe_spawn_position(const Position *mines, int mine_count,
                                  const CrossObstacle *obstacle);

void reset_robot_body_from_lives(Robot *robot, const Player *player);

WINDOW* init_game(Player *player, Robot *robot,
                  Position *person, Position *mines,
                  int mine_count, CrossObstacle *obstacle);

void update_UI(const Player *player, const Robot *robot);

void handle_input(Robot *robot, int input, bool *running);

void spawn_mines(const Robot *robot, const Position *person,
                 Position *mines, int *mine_count,
                 int target_count, const CrossObstacle *obstacle);
void draw_mines(WINDOW *board, const Position *mines, int mine_count);

void spawn_person(const Robot *robot, Position *person,
                  const Position *mines, int mine_count,
                  const CrossObstacle *obstacle);
void draw_person(WINDOW *board, const Position *person);

void clear_robot(WINDOW *board, const Robot *robot);
void draw_robot(WINDOW *board, const Robot *robot);

void move_robot(Robot *robot);
void move_robot_ai(Robot *robot, const Position *person,
                   const Position *mines, int mine_count,
                   const CrossObstacle *obstacle);

void bomb_mines(Player *player, const Robot *robot,
                Position *mines, int *mine_count, WINDOW *board);

void check_collision(Player *player, Robot *robot,
                     const Position *mines, int mine_count,
                     const CrossObstacle *obstacle,
                     bool *running, bool *life_lost);

int  get_delay_for_level(int level);
void game_over_screen(const Player *player);

// ---Direction To dx and dy---
void set_direction(Robot *robot, char dir) {
    robot->direction = dir;
}

void direction_to_delta(char dir, int *dx, int *dy) { // delta x/y will be change here
    *dx = 0; *dy = 0; // initialize dx = dy = 0
    switch (dir) {
        case 'N':
            *dx = 0;  *dy = -1;
            break; // move upper represent the change in y should be -1
        case 'S':
            *dx = 0;  *dy = 1;
            break; // move lower represent the change in y should be +1
        case 'W':
            *dx = -1; *dy = 0;
            break; // move left represent the change in x should be -1
        case 'E':
            *dx = 1;  *dy = 0;
            break; // move right represent the change in x should be +1
        default:
            *dx = 0;  *dy = 0;
            break;
    }
}

void reset_robot_body_from_lives(Robot *robot, const Player *player) { // Build the number of segement which also represent lives of robot
    int len = player->lives; // Get the length from current lives
    if (len < 0) len = 0; // it can not be negative
    if (len > MAX_BODY_SEGMENTS) len = MAX_BODY_SEGMENTS; // length of robot should be smaller than 21 (20 segments and a head)

    robot->body_length = len; // get the number of segment

    int dx, dy;
    direction_to_delta(robot->direction, &dx, &dy);
    if (dx == 0 && dy == 0) { dx = -1; dy = 0; } // If their are no direction, head of robot should be point to right

    for (int i = 0; i < robot->body_length; i++) { // Loop for get every segment's location
        int bx = robot->pos.x - dx * (i + 1);
        int by = robot->pos.y - dy * (i + 1);

        if (bx <= 1 || bx >= BOARD_COLS - 2 ||
            by <= 1 || by >= BOARD_ROWS - 2) { // If over the boundary, design for location of head
            bx = robot->pos.x;
            by = robot->pos.y;
        }

        robot->body[i].x = bx; // set the location of body
        robot->body[i].y = by;
    }
}

// ---Initial Title Screen---
void draw_title_screen(Player *player) {
    nodelay(stdscr, FALSE); // should be have delay now, waiting for input player's name
    clear(); // clear the screen

    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax); // Get the size of screen

    const char *title = "Rescue Bot: Snake on a Minefield"; // Get the title
    mvprintw(2, (xmax - (int)strlen(title)) / 2, "%s", title); // print title in the middle

    mvprintw(4, 4, "Description:"); // Adding description in title screen
    mvprintw(5, 6, "Guide a snake-like robot to rescue people on a minefield.");
    mvprintw(6, 6, "Avoid walls, mines and the central cross obstacle (#).");
    mvprintw(7, 6, "Your robot has multiple body segments = number of lives.");
    mvprintw(8, 6, "Lose one life -> lose one segment.");
    mvprintw(9, 6, "Every 5 levels you gain +1 extra life (segment).");
    mvprintw(10,6, "Rescue people, survive longer, and beat the high score!");

    mvprintw(12, 4, "Controls:"); // Adding control instruction
    mvprintw(13, 6, "Arrow keys / WASD : move robot (Manual mode)");
    mvprintw(14, 6, "'m'               : toggle Manual / AI mode");
    mvprintw(15, 6, "'q'               : quit game");
    mvprintw(16, 6, "SPACE (level>10)  : spend 5 levels to bomb nearby mines");

    player->score   = 0; // Initiallizing
    player->lives   = INITIAL_LIVES;
    player->level   = 1;
    player->rescued = 0;

    mvprintw(18, 4, "Enter your name (max %d chars) and press ENTER:", MAX_NAME);
    mvprintw(19, 4, "> ");
    move(19, 6);  // move mouse after ">" and wait for player's input

    echo();
    curs_set(1); // player can see the mouse

    char buf[MAX_NAME + 1];
    memset(buf, 0, sizeof(buf)); // fill the memory with 0
    getnstr(buf, MAX_NAME); // get the name from string

    noecho();
    curs_set(0); // player can not see the mouse

    if (strlen(buf) == 0) {
        strcpy(player->name, "Player"); // If there are no input, the name of player is just "Player"
    } else {
        strncpy(player->name, buf, MAX_NAME); // Copy the name
        player->name[MAX_NAME] = '\0';
    }

    mvprintw(21, 4, "Welcome, %s! Press any key to start...", player->name); // welcomr the player
    refresh();
    getch(); // waiting for any input to countinue the program

    nodelay(stdscr, TRUE); // start without any delay now
}

// ---Obstacle---

void init_obstacle(CrossObstacle *obstacle) {
    obstacle->width    = 11; // the width is 11
    obstacle->height   = 11; // the height is 11
    obstacle->center_x = BOARD_COLS / 2; // center is in the middle of x of board
    obstacle->center_y = BOARD_ROWS / 2; // center is in the middle of y of board
}

bool is_obstacle_position(const CrossObstacle *obstacle, int x, int y) { // check again the place of obstacle
    int cx = obstacle->center_x;
    int cy = obstacle->center_y;
    int half_w = obstacle->width  / 2;
    int half_h = obstacle->height / 2;

    if (y == cy && x >= cx - half_w && x <= cx + half_w) return true;
    if (x == cx && y >= cy - half_h && y <= cy + half_h) return true;
    return false; // If not, stop the program
}

void draw_obstacle(WINDOW *board, const CrossObstacle *obstacle) { // drawing the obstacle
    wattron(board, COLOR_PAIR(CP_OBSTACLE)); // obstacle should be yellow
    for (int y = 1; y < BOARD_ROWS - 1; y++) { // draw "#" if any place in board should be the place of obstacle
        for (int x = 1; x < BOARD_COLS - 1; x++) {
            if (is_obstacle_position(obstacle, x, y)) {
                mvwaddch(board, y, x, OBSTACLE);
            }
        }
    }
    wattroff(board, COLOR_PAIR(CP_OBSTACLE)); // close the color
}

// ---Check location for mine---
bool is_mine_at(const Position *mines, int mine_count, int x, int y) {
    for (int i = 0; i < mine_count; i++) {
        if (mines[i].x == x && mines[i].y == y) return true;
    }
    return false;
}

// ---Spawn in a safe Place near (10,10)---
Position find_safe_spawn_position(const Position *mines, int mine_count,
                                  const CrossObstacle *obstacle) {
    int target_x = 10;
    int target_y = 10; // the target place of robot to spawn is (10,10)

    Position best = {BOARD_COLS / 2, BOARD_ROWS / 2};
    int best_dist = 1000000; // Just design a big number that any distance in board should be smaller than that

    for (int y = 2; y < BOARD_ROWS - 2; y++) {
        for (int x = 2; x < BOARD_COLS - 2; x++) {
            if (is_obstacle_position(obstacle, x, y)) continue; // Do not let robot spawn on obstacle
            if (mines && is_mine_at(mines, mine_count, x, y)) continue; // Do not let robot spawn on mine

            int dist = abs(x - target_x) + abs(y - target_y); // Calculate the distance
            if (dist < best_dist) { // If it can find a closer place, then change it
                best_dist = dist;
                best.x = x;
                best.y = y;
            }
        }
    }
    return best;
}

// ---Initiallize the game board---

WINDOW* init_game(Player *player, Robot *robot,
                  Position *person, Position *mines,
                  int mine_count, CrossObstacle *obstacle) {
    (void)person; (void)mines; (void)mine_count; (void)player;

    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax); // get the size of terminal it can used

    int start_y = (ymax - BOARD_ROWS) / 2; // put in the middle of the height

    int right_margin = 20; // leave 20 space the board from the right side of terminal
    int start_x = xmax - BOARD_COLS - right_margin;
    if (start_x < 0) start_x = 0; // it can not be negative, if so, then 0

    WINDOW *board = newwin(BOARD_ROWS, BOARD_COLS, start_y, start_x); // create some space for board
    wbkgd(board, COLOR_PAIR(CP_BOARD_BG)); // set up the color of background
    werase(board); // erase the board
    box(board, 0, 0); // draw the boundary

    init_obstacle(obstacle);

    Position spawn = find_safe_spawn_position(NULL, 0, obstacle);
    robot->pos = spawn; // Find a safe place and set up for the spawn of robot

    robot->ai_mode          = true; // start with ai mode
    robot->invincible       = false; // start without invincible mode
    robot->invincible_ticks = 0; // 0 steps invincible at first
    robot->body_length      = 0;
    set_direction(robot, 'W'); // robot initial point to west

    wrefresh(board); // refresh the terminal
    return board;
}

// ---UI Status---
void update_UI(const Player *player, const Robot *robot) {
    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax); // Get the size of screen

    char buf[256]; // leave a buffer area
    snprintf(buf, sizeof(buf),
             "Player: %s  Score: %d  Level: %d  Lives: %d  Mode: %s  Segments: %d",
             player->name, player->score, player->level, player->lives,
             robot->ai_mode ? "AI" : "Manual",
             robot->body_length);

    attron(COLOR_PAIR(CP_STATUS)); // start show the color
    mvhline(0, 0, ' ', xmax); // leave the row empty
    mvprintw(0, 0, "%s", buf);

    mvhline(1, 0, ' ', xmax); // leave the row empty
    mvprintw(1, 0, "Use Arrow keys/WASD to move. 'm' toggle AI, 'q' quit, SPACE bombs mines (lvl>10).");
    attroff(COLOR_PAIR(CP_STATUS)); // close to show the color

    refresh();
}

// ---Get input for "m" "q" and control keyboard in game---
void handle_input(Robot *robot, int input, bool *running) {
    if (input == ERR) return; // waiting for the input

    switch (input) {
        case 'q': case 'Q':
            *running = false; // type "q" to escape the game
            break;
        case 'm': case 'M':
            robot->ai_mode = !robot->ai_mode; // type "m" to change the mode between ai and manually
            break;
        case KEY_UP: case 'w': case 'W':
            if (!robot->ai_mode) set_direction(robot, 'N'); // move the robot upper/lower/left/right
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

// ---Get the random mine and draw it---
void spawn_mines(const Robot *robot, const Position *person,
                 Position *mines, int *mine_count,
                 int target_count, const CrossObstacle *obstacle) {
    if (target_count > MAX_MINES) target_count = MAX_MINES;

    while (*mine_count < target_count) { // loop until to the number of mine it should be
        int x = 1 + rand() % (BOARD_COLS - 2); // randomly get x/y coordinate of mine
        int y = 1 + rand() % (BOARD_ROWS - 2);

        if (x == robot->pos.x && y == robot->pos.y) continue; // it can not be the position of person/robot/obstacle/mine
        if (person && x == person->x && y == person->y) continue;
        if (is_obstacle_position(obstacle, x, y)) continue;
        if (is_mine_at(mines, *mine_count, x, y)) continue;

        mines[*mine_count].x = x;
        mines[*mine_count].y = y;
        (*mine_count)++; // number of mine get one more
    }
}

void draw_mines(WINDOW *board, const Position *mines, int mine_count) {
    wattron(board, COLOR_PAIR(CP_MINE)); // start to show the color of mine
    for (int i = 0; i < mine_count; i++) {
        mvwaddch(board, mines[i].y, mines[i].x, MINE); // draw all mines one by one
    }
    wattroff(board, COLOR_PAIR(CP_MINE)); // close the color of mine
}

// ---Person---
void spawn_person(const Robot *robot, Position *person,
                  const Position *mines, int mine_count,
                  const CrossObstacle *obstacle) {
    while (1) {
        int x = 1 + rand() % (BOARD_COLS - 2); // random x/y coordinate of person
        int y = 1 + rand() % (BOARD_ROWS - 2);

        if (x == robot->pos.x && y == robot->pos.y) continue; // can not be at robot/obstacle/mine's place
        if (is_mine_at(mines, mine_count, x, y)) continue;
        if (is_obstacle_position(obstacle, x, y)) continue;

        person->x = x;
        person->y = y;
        break;
    }
}

void draw_person(WINDOW *board, const Position *person) {
    wattron(board, COLOR_PAIR(CP_PERSON)); // show and close the color of person
    mvwaddch(board, person->y, person->x, PERSON); // draw the person
    wattroff(board, COLOR_PAIR(CP_PERSON));
}

// ---Movement of Robot---
void clear_robot(WINDOW *board, const Robot *robot) {
    mvwaddch(board, robot->pos.y, robot->pos.x,
             ' ' | COLOR_PAIR(CP_BOARD_BG)); // clear the head of robot at orignal place

    for (int i = 0; i < robot->body_length; i++) {
        int bx = robot->body[i].x;
        int by = robot->body[i].y; // clear also all the segments of robot
        if (bx > 0 && bx < BOARD_COLS - 1 &&
            by > 0 && by < BOARD_ROWS - 1) {
            mvwaddch(board, by, bx, ' ' | COLOR_PAIR(CP_BOARD_BG));
        }
    }
}

void draw_robot(WINDOW *board, const Robot *robot) {
    if (robot->invincible &&
        (robot->invincible_ticks % 2 == 1)) { // if it in invincible period, it shows only even frame so that it looks like shining
        return;
    }

    wattron(board, COLOR_PAIR(CP_ROBOT)); // start show the color of robot

    for (int i = 0; i < robot->body_length; i++) {
        int bx = robot->body[i].x;
        int by = robot->body[i].y; // draew all the segment of robot
        if (bx > 0 && bx < BOARD_COLS - 1 &&
            by > 0 && by < BOARD_ROWS - 1) {
            mvwaddch(board, by, bx, ROBOT_BODY);
        }
    }

    char head_char = ROBOT_HEAD;
    switch (robot->direction) {
        case 'N': head_char = '^'; break; // difirent direction should use different symbol as the head
        case 'S': head_char = 'v'; break;
        case 'W': head_char = '<'; break;
        case 'E': head_char = '>'; break;
        default:  head_char = ROBOT_HEAD; break;
    }
    mvwaddch(board, robot->pos.y, robot->pos.x, head_char); // draw the head of robot

    wattroff(board, COLOR_PAIR(CP_ROBOT)); // close the color
}

void move_robot(Robot *robot) {
    int dx, dy; // get the changing of coordinate of robot
    direction_to_delta(robot->direction, &dx, &dy);

    for (int i = robot->body_length - 1; i > 0; i--) { // move the body from tail to head
        robot->body[i] = robot->body[i - 1];
    }
    if (robot->body_length > 0) {
        robot->body[0] = robot->pos;
    }

    robot->pos.x += dx; // move the head
    robot->pos.y += dy;
}

// ---AI mode(BFS)---
typedef struct { int x, y; } Node;

static bool is_blocked_cell(int x, int y,
                            const Position *mines, int mine_count,
                            const CrossObstacle *obstacle) {
    if (x <= 0 || x >= BOARD_COLS - 1 ||
        y <= 0 || y >= BOARD_ROWS - 1) // check the boundary
        return true;
    if (is_obstacle_position(obstacle, x, y)) return true; // check the obstacle and mine
    if (is_mine_at(mines, mine_count, x, y)) return true;
    return false;
}

static bool bfs_next_direction(const Robot *robot, const Position *person,
                               const Position *mines, int mine_count,
                               const CrossObstacle *obstacle,
                               char *out_dir) {
    bool visited[BOARD_ROWS][BOARD_COLS] = {false};
    Position parent[BOARD_ROWS][BOARD_COLS]; // set up a Parent node array

    for (int y = 0; y < BOARD_ROWS; y++) {
        for (int x = 0; x < BOARD_COLS; x++) {
            parent[y][x].x = -1;
            parent[y][x].y = -1; // initiallize parent node array all to -1
        }
    }

    Node queue[BOARD_ROWS * BOARD_COLS];
    int front = 0, back = 0;

    int sx = robot->pos.x;
    int sy = robot->pos.y; // initial position
    int tx = person->x;
    int ty = person->y; // target position

    queue[back++] = (Node){sx, sy};
    visited[sy][sx] = true;

    int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}}; // there are four direction
    bool found = false; // check is there really a way

    while (front < back) {
        Node cur = queue[front++];

        if (cur.x == tx && cur.y == ty) {
            found = true; // notice some place can be walked
            break;
        }

        for (int i = 0; i < 4; i++) { // check all four direction
            int nx = cur.x + dirs[i][0];
            int ny = cur.y + dirs[i][1]; // get the new location

            if (nx < 0 || nx >= BOARD_COLS ||
                ny < 0 || ny >= BOARD_ROWS) // check the boundary
                continue;
            if (visited[ny][nx]) continue;
            if (is_blocked_cell(nx, ny, mines, mine_count, obstacle))
                continue;

            visited[ny][nx] = true;
            parent[ny][nx].x = cur.x; // set up the parent node array
            parent[ny][nx].y = cur.y;
            queue[back++] = (Node){nx, ny};
        }
    }

    if (!found) return false;

    int cx = tx, cy = ty; // do inverse from target which is person
    int px = parent[cy][cx].x;
    int py = parent[cy][cx].y;

    if (px == -1 && py == -1) return false;

    while (!(px == sx && py == sy)) { // go back to currently the location of robot
        cx = px;
        cy = py;
        px = parent[cy][cx].x;
        py = parent[cy][cx].y;
        if (px == -1 && py == -1) break;
    }

    int dx = cx - sx; // calculate the next change in x/y
    int dy = cy - sy;

    if (dx == 1 && dy == 0)*out_dir = 'E'; // get the output direction
    else if (dx == -1 && dy == 0)*out_dir = 'W';
    else if (dx == 0 && dy == 1)*out_dir = 'S';
    else if (dx == 0 && dy == -1)*out_dir = 'N';
    else return false;

    return true;
}
// ---Movement of Robot by AI---
void move_robot_ai(Robot *robot, const Position *person,
                   const Position *mines, int mine_count,
                   const CrossObstacle *obstacle) {
    if (!person) return; // It should have person in board

    char dir;
    if (bfs_next_direction(robot, person, mines, mine_count, obstacle, &dir)) {
        set_direction(robot, dir); // the case bfs can found the direction
        return;
    }

    char candidates[4] = {'N','S','E','W'};
    for (int k = 0; k < 4; k++) { // walk randomly if bfs cannot find the direction
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

// ---Creativity: Bomb can destroy a range of mines---
void bomb_mines(Player *player, const Robot *robot,
                Position *mines, int *mine_count, WINDOW *board) {
    if (player->level <= 10) return; // only can be used after your level is over 10

    bool to_clear[MAX_MINES] = {false}; // let the mine disappear
    int cx = robot->pos.x; // get the x/y coordinate of center
    int cy = robot->pos.y;
    int radius = 5; // the range is 5 radius
    bool any = false;

    for (int i = 0; i < *mine_count; i++) { // check all the mine which are inside the bomb's range
        if (abs(mines[i].x - cx) <= radius &&
            abs(mines[i].y - cy) <= radius) {
            to_clear[i] = true;
            any = true;
        }
    }

    player->level -= 5; // it consume 5 level to use this
    if (player->level < 1) player->level = 1; // level cannot be lower than 1

    if (any) {
        for (int t = 0; t < 6; t++) {   // shining all mines for 6 frame
            for (int i = 0; i < *mine_count; i++) {
                if (!to_clear[i]) continue; // the image of odd and even frame should be different
                char ch = (t % 2 == 0) ? '*' : ' ';
                mvwaddch(board, mines[i].y, mines[i].x, ch);
            }
            wrefresh(board); // refresh
            update_UI(player, robot); // change the ui cause level changed
            napms(80); // with a 80ms delay
        }
    }

    int w = 0;
    for (int i = 0; i < *mine_count; i++) { // delete these mine
        if (to_clear[i]) continue;
        if (w != i) mines[w] = mines[i];
        w++;
    }
    *mine_count = w; // also update the number of mine
}

// ---Collision Test---
void check_collision(Player *player, Robot *robot,
                     const Position *mines, int mine_count,
                     const CrossObstacle *obstacle,
                     bool *running, bool *life_lost) {
    if (life_lost) *life_lost = false;

    int x = robot->pos.x; // get the current location of robot
    int y = robot->pos.y;

    bool hit_wall = (x <= 0 || x >= BOARD_COLS - 1 || // check collision with wall/mine/obstacle
                     y <= 0 || y >= BOARD_ROWS - 1);
    bool hit_mine = is_mine_at(mines, mine_count, x, y);
    bool hit_obs  = is_obstacle_position(obstacle, x, y);

    bool deadly = hit_wall || hit_mine || hit_obs; // any situation will cause lose life

    if (deadly && !robot->invincible) {
        player->lives--; // check do robot in invincible period

        if (player->lives <= 0) { // it is over if life is smaller than 1
            *running = false;
            return;
        }

        robot->invincible       = true; // set up the invincible period after spawn
        robot->invincible_ticks = INVINCIBLE_TICKS;

        Position spawn = find_safe_spawn_position(mines, mine_count, obstacle); // get the place for spawn
        robot->pos = spawn;
        reset_robot_body_from_lives(robot, player); // set up head and body

        if (life_lost) *life_lost = true; // it lost a life
    }

    if (robot->invincible) { // the case in invincible
        robot->invincible_ticks--;
        if (robot->invincible_ticks <= 0) {
            robot->invincible = false;
        }
    }
}

// ---Leaderboard or GameOver---
int compare_scores_desc(const void *a, const void *b) {
    const LeaderboardEntry *ea = (const LeaderboardEntry *)a;
    const LeaderboardEntry *eb = (const LeaderboardEntry *)b;
    return eb->score - ea->score;
}

void game_over_screen(const Player *player) {
    LeaderboardEntry *entries = NULL;
    int count = 0, cap = 0;

    FILE *f = fopen(LEADERBOARD_FILE, "r"); // open the file and read
    if (f) {
        char name_buf[MAX_NAME + 1]; // buffer for name
        int score, level;
        while (fscanf(f, "%20s %d %d", name_buf, &score, &level) == 3) {
            if (count >= cap) { // expand the room if it in is not enough
                cap = (cap == 0) ? 16 : cap * 2;
                entries = realloc(entries, sizeof(LeaderboardEntry) * cap);
                if (!entries) break;
            }
            strncpy(entries[count].name, name_buf, MAX_NAME); // copy the name
            entries[count].name[MAX_NAME] = '\0';
            entries[count].score = score; // set up the score ane level
            entries[count].level = level;
            count++;
        }
        fclose(f); // close the file
    }

    int best_before = -1; // initial the best score to -1 so that whatever you got first time it should be your best score
    for (int i = 0; i < count; i++) {
        if (entries[i].score > best_before)
            best_before = entries[i].score;
    }
    bool new_record = (count == 0 || player->score > best_before); // check is that the new best score

    if (count >= cap) { // add this player
        cap = (cap == 0) ? 16 : cap * 2;
        entries = realloc(entries, sizeof(LeaderboardEntry) * cap);
    }
    strncpy(entries[count].name, player->name, MAX_NAME);
    entries[count].name[MAX_NAME] = '\0';
    entries[count].score = player->score;
    entries[count].level = player->level;
    count++;

    if (entries && count > 0) { // range the score from higher to lower
        qsort(entries, count, sizeof(LeaderboardEntry), compare_scores_desc);
    }

    f = fopen(LEADERBOARD_FILE, "w"); // open the file and write
    if (f) {
        for (int i = 0; i < count; i++) { // put all the information of that player, basiclly name level and score
            fprintf(f, "%s %d %d\n",
                    entries[i].name, entries[i].score, entries[i].level);
        }
        fclose(f);
    }

// ---Screen to encourage you before leaderboard---
    clear();
    int ymax, xmax;
    getmaxyx(stdscr, ymax, xmax); // get the size of screen

    const char *msg = "GAME OVER";
    mvprintw(2, (xmax - (int)strlen(msg)) / 2, "%s", msg); // print game over

    char buf1[64];
    snprintf(buf1, sizeof(buf1), "Final score: %d", player->score); // print final score
    mvprintw(4, (xmax - (int)strlen(buf1)) / 2, "%s", buf1);

    char buf2[64];
    snprintf(buf2, sizeof(buf2), "Player: %s (Level %d)",
             player->name, player->level); // information of  player
    mvprintw(5, (xmax - (int)strlen(buf2)) / 2, "%s", buf2);

    if (new_record) {
        const char *rec = "Congratulations! NEW HIGH SCORE!"; // situation that you got a new high score
        mvprintw(7, (xmax - (int)strlen(rec)) / 2, "%s", rec);
    } else {
        const char *tip = "Nice run! Try to beat the record next time."; // situation that you did not
        mvprintw(7, (xmax - (int)strlen(tip)) / 2, "%s", tip);
    }

    mvprintw(ymax - 3, (xmax - 36) / 2,
             "Press any key to view leaderboard..."); // tell player need to type sth and then can countinue
    refresh();

    nodelay(stdscr, FALSE); // should have delay now
    getch(); // get the input so that code can continue

// ---leaderboard---
    clear();

    const char *title = "LEADERBOARD - STATIC MINES MODE"; // print the title of table
    mvprintw(2, (xmax - (int)strlen(title)) / 2, "%s", title);

    mvprintw(4, 4, "Rank  Name        Level  Score");
    mvprintw(5, 4, "--------------------------------------");

    if (!entries || count == 0) {
        mvprintw(7, 6, "No records yet."); // the case of no any records of score
    } else {
        int top = (count < 10) ? count : 10; // only show the top ten
        for (int i = 0; i < top; i++) {
            mvprintw(6 + i, 4, "%2d    %-10s  %5d  %5d",
                     i + 1,
                     entries[i].name,
                     entries[i].level,
                     entries[i].score);
        }
    }

    mvprintw(ymax - 2, 4, "Press any key to exit.");
    refresh();
    getch();
    nodelay(stdscr, TRUE); // no delay now

    free(entries); // avoid the memory leak
}
// ---Velocity Changing---

int get_delay_for_level(int level) {
    int delay = BASE_DELAY_MS; // start with a 400ms delay
    for (int i = 1; i < level && delay > MIN_DELAY_MS; i++) {
        delay /= 2; // every level will let the delay be half of original
    }
    if (delay < MIN_DELAY_MS) delay = MIN_DELAY_MS;
    return delay; // but delay can not be smaller than 50ms
}

// ---Main---
int main(void) {
    srand((unsigned int)time(NULL)); // set up the seed for random number

    initscr(); // initial nucrses
    cbreak();
    noecho(); // not showing character
    keypad(stdscr, TRUE); // you can use pageup ... these in keyboard
    curs_set(0); // not showing mouse
    nodelay(stdscr, TRUE); // there shold be no delay
    init_colors(); // start to use the color

    Player  player; // define player/robot/person/mine/obstacle
    Robot   robot;
    Position person; 
    Position mines[MAX_MINES];
    int mine_count = 0;
    CrossObstacle obstacle;

    draw_title_screen(&player);

    WINDOW *board = init_game(&player, &robot, &person, mines,
                              mine_count, &obstacle);
    reset_robot_body_from_lives(&robot, &player); // put your robot into board

    spawn_person(&robot, &person, mines, mine_count, &obstacle); // put the first person
    spawn_mines(&robot, &person, mines, &mine_count,
                BASE_MINES, &obstacle); // put the basic mines

    bool running = true;

    while (running && player.lives > 0) {
        int ch = getch(); // get the input

        if (ch == ' ') { // if input is " " then it's for bomb
            if (player.level > 10) { // level of player should be over 10
                bomb_mines(&player, &robot, mines, &mine_count, board);
            }
        } else {
            handle_input(&robot, ch, &running);
        }
        if (!running) break;

        if (robot.ai_mode) { // ai mode first
            move_robot_ai(&robot, &person, mines,
                          mine_count, &obstacle);
        }

        clear_robot(board, &robot);
        move_robot(&robot);

        bool life_lost = false;
        check_collision(&player, &robot, mines,
                        mine_count, &obstacle,
                        &running, &life_lost);
        if (!running || player.lives <= 0) break;

        if (life_lost) { // after lose a life, erase board, and do all things again with updated UI
            werase(board);
            box(board, 0, 0);
            draw_obstacle(board, &obstacle);
            draw_mines(board, mines, mine_count);
            draw_person(board, &person);
            draw_robot(board, &robot);

            update_UI(&player, &robot);

            int ymax, xmax;
            getmaxyx(stdscr, ymax, xmax);
            mvprintw(ymax - 1, 4,
                     "You lost a life! Press 'y' to continue or 'q' to quit."); // ask player to keep the game or not
            refresh();
            wrefresh(board);

            nodelay(stdscr, FALSE); // should have delay now
            int key;
            do {
                key = getch();
            } while (key != 'y' && key != 'Y' && // "y" for continue or just quit the game
                     key != 'q' && key != 'Q');

            if (key == 'q' || key == 'Q') {
                running = false;
                break;
            }

            nodelay(stdscr, TRUE);
            continue;
        }

// ---Saving people---
        if (robot.pos.x == person.x && robot.pos.y == person.y) { // same location means robot save person successfully
            player.score += 10; // saving a person plus 10 score
            player.rescued++; // the number of robot saved

            if (player.rescued >= PEOPLE_PER_LEVEL) {
                player.level++;
                player.rescued = 0; // a new level will let the number of person be saved change to 0

                int target = mine_count + MINES_PER_LEVEL; // plus two new mine for a new level
                spawn_mines(&robot, &person, mines,
                            &mine_count, target, &obstacle);

                if (player.level % 5 == 0) { // five level will add a live for you
                    player.lives++;
                    if (player.lives > MAX_BODY_SEGMENTS)
                        player.lives = MAX_BODY_SEGMENTS;
                    reset_robot_body_from_lives(&robot, &player);
                }
            }

            spawn_person(&robot, &person, mines, // get a new person after you save oringinal person
                         mine_count, &obstacle);
        }

// ---Draw the New Board---
        werase(board); // draw all the component again
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

    game_over_screen(&player); // the screen for game over

    delwin(board); // delete the window
    endwin(); // close ncurses
    return 0;
}
