#include "tetris.h"
#include "display.h"
#include <esp_system.h>

static const int COLS = 10;
static const int ROWS = 20;
static const int CELL = 8;

static const uint8_t SHAPES[7][4][4] = {
    { {0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0} },
    { {0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0} },
    { {0,0,0,0},{0,1,0,0},{1,1,1,0},{0,0,0,0} },
    { {0,0,0,0},{1,0,0,0},{1,1,1,0},{0,0,0,0} },
    { {0,0,0,0},{0,0,1,0},{1,1,1,0},{0,0,0,0} },
    { {0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0} },
    { {0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0} }
};

static const uint16_t COLORS[7] = {
    ILI9341_CYAN,
    ILI9341_YELLOW,
    ILI9341_MAGENTA,
    0xFD20,
    ILI9341_BLUE,
    ILI9341_GREEN,
    ILI9341_RED
};

static uint8_t board[ROWS][COLS];

struct Piece {
    int type;
    int x;
    int y;
    int rot;
};

static Piece curPiece;
static Piece nextPiece;
static bool active = false;
static bool paused = false;
static bool gameOver = false;
static uint32_t score = 0;
static uint32_t totalLines = 0;
static uint32_t level = 1;
static uint32_t fallMs = 700;
static uint32_t lastFall = 0;
static Button gameButtons[7];

static bool cellAt(int type, int rot, int r, int c) {
    int sr, sc;
    switch (rot & 3) {
        case 0: sr = r; sc = c; break;
        case 1: sr = c; sc = 3 - r; break;
        case 2: sr = 3 - r; sc = 3 - c; break;
        default: sr = 3 - c; sc = r; break;
    }
    return SHAPES[type][sr][sc] != 0;
}

static bool collides(int type, int x, int y, int rot) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!cellAt(type, rot, r, c)) continue;
            int bx = x + c;
            int by = y + r;
            if (by < 0) continue;
            if (bx < 0 || bx >= COLS || by >= ROWS) return true;
            if (board[by][bx] != 0) return true;
        }
    }
    return false;
}

static void mergePiece() {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!cellAt(curPiece.type, curPiece.rot, r, c)) continue;
            int by = curPiece.y + r;
            int bx = curPiece.x + c;
            if (by >= 0 && by < ROWS && bx >= 0 && bx < COLS) {
                board[by][bx] = curPiece.type + 1;
            }
        }
    }
}

static int clearLines() {
    int cleared = 0;
    for (int row = ROWS - 1; row >= 0; row--) {
        bool full = true;
        for (int c = 0; c < COLS; c++) {
            if (board[row][c] == 0) {
                full = false;
                break;
            }
        }
        if (full) {
            cleared++;
            for (int r = row; r > 0; r--) {
                for (int c = 0; c < COLS; c++) {
                    board[r][c] = board[r - 1][c];
                }
            }
            for (int c = 0; c < COLS; c++) board[0][c] = 0;
            row++;
        }
    }
    return cleared;
}

static void spawnPiece() {
    curPiece.type = nextPiece.type;
    curPiece.x = 3;
    curPiece.y = -1;
    curPiece.rot = 0;
    nextPiece.type = random(0, 7);

    if (collides(curPiece.type, curPiece.x, curPiece.y, curPiece.rot)) {
        gameOver = true;
    }
}

static void lockPiece() {
    mergePiece();
    int cleared = clearLines();
    if (cleared > 0) {
        totalLines += cleared;
        uint32_t points = 0;
        if (cleared == 1) points = 100;
        else if (cleared == 2) points = 300;
        else if (cleared == 3) points = 600;
        else points = 1000;
        score += points * level;
        level = totalLines / 10 + 1;
        fallMs = 700 - (level - 1) * 70;
        if (fallMs < 100) fallMs = 100;
    }
    spawnPiece();
}

void tetris_start() {
    randomSeed(esp_random());
    memset(board, 0, sizeof(board));
    score = 0;
    totalLines = 0;
    level = 1;
    fallMs = 700;
    active = true;
    paused = false;
    gameOver = false;
    nextPiece.type = random(0, 7);
    spawnPiece();
    lastFall = millis();
}

bool tetris_is_active() {
    return active;
}

static Button draw_game_button(int x, int y, int w, int h, const String& label) {
    tft.fillRect(x, y, w, h, ILI9341_BLACK);
    tft.drawRect(x, y, w, h, ILI9341_GREEN);
    int tw = 6 * label.length();
    int cx = x + (w - tw) / 2;
    if (cx < x + 2) cx = x + 2;
    display_draw_text(cx, y + (h - 8) / 2, label, ILI9341_GREEN, 1);
    return { x, y, w, h, label };
}

void tetris_draw() {
    if (!active) return;

    tft.fillRect(0, 24, tft.width(), tft.height() - 42, ILI9341_BLACK);

    const int px = 8;
    const int py = 28;
    tft.drawRect(px - 1, py - 1, COLS * CELL + 2, ROWS * CELL + 2, ILI9341_DARKGREY);

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (board[r][c] != 0) {
                uint16_t color = COLORS[board[r][c] - 1];
                tft.fillRect(px + c * CELL + 1, py + r * CELL + 1, CELL - 2, CELL - 2, color);
            }
        }
    }

    int ghostY = curPiece.y;
    while (!collides(curPiece.type, curPiece.x, ghostY + 1, curPiece.rot)) ghostY++;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!cellAt(curPiece.type, curPiece.rot, r, c)) continue;
            int gy = ghostY + r;
            int gx = curPiece.x + c;
            if (gy >= 0 && gy < ROWS) {
                tft.fillRect(px + gx * CELL + 2, py + gy * CELL + 2, CELL - 4, CELL - 4, ILI9341_DARKGREY);
            }
        }
    }

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!cellAt(curPiece.type, curPiece.rot, r, c)) continue;
            int by = curPiece.y + r;
            int bx = curPiece.x + c;
            if (by >= 0 && by < ROWS) {
                tft.fillRect(px + bx * CELL + 1, py + by * CELL + 1, CELL - 2, CELL - 2, COLORS[curPiece.type]);
            }
        }
    }

    gameButtons[0] = draw_game_button(104, 28, 62, 24, "Left");
    gameButtons[1] = draw_game_button(170, 28, 62, 24, "Right");
    gameButtons[2] = draw_game_button(236, 28, 62, 24, "Rot");
    gameButtons[3] = draw_game_button(104, 56, 62, 24, "Down");
    gameButtons[4] = draw_game_button(170, 56, 62, 24, "Drop");
    gameButtons[5] = draw_game_button(236, 56, 62, 24, paused ? "Play" : "Pause");
    gameButtons[6] = draw_game_button(104, 84, 194, 24, "New Game");

    display_draw_text(104, 134, "Score: " + String(score), ILI9341_GREEN, 1);
    display_draw_text(104, 146, "Level: " + String(level), ILI9341_GREEN, 1);
    display_draw_text(104, 158, "Lines: " + String(totalLines), ILI9341_GREEN, 1);

    display_draw_text(104, 170, "Next:", ILI9341_GREEN, 1);
    int nx = 108;
    int ny = 178;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (cellAt(nextPiece.type, 0, r, c)) {
                tft.fillRect(nx + c * 6, ny + r * 6, 5, 5, COLORS[nextPiece.type]);
            }
        }
    }

    if (gameOver) {
        display_draw_text(104, 112, "GAME OVER", ILI9341_RED, 2);
    } else if (paused) {
        display_draw_text(104, 112, "PAUSED", ILI9341_YELLOW, 2);
    }
}

bool tetris_handle_touch(int x, int y) {
    for (int i = 0; i < 7; i++) {
        if (!display_point_in_button(gameButtons[i], x, y)) continue;

        if (gameOver) {
            if (i == 6) tetris_start();
            tetris_draw();
            return true;
        }
        if (paused && i != 5 && i != 6) {
            return true;
        }

        if (i == 0) {
            if (!collides(curPiece.type, curPiece.x - 1, curPiece.y, curPiece.rot)) curPiece.x--;
        } else if (i == 1) {
            if (!collides(curPiece.type, curPiece.x + 1, curPiece.y, curPiece.rot)) curPiece.x++;
        } else if (i == 2) {
            int nr = (curPiece.rot + 1) & 3;
            if (!collides(curPiece.type, curPiece.x, curPiece.y, nr)) {
                curPiece.rot = nr;
            } else if (!collides(curPiece.type, curPiece.x - 1, curPiece.y, nr)) {
                curPiece.x--;
                curPiece.rot = nr;
            } else if (!collides(curPiece.type, curPiece.x + 1, curPiece.y, nr)) {
                curPiece.x++;
                curPiece.rot = nr;
            }
        } else if (i == 3) {
            if (!collides(curPiece.type, curPiece.x, curPiece.y + 1, curPiece.rot)) {
                curPiece.y++;
                score += 1;
            } else {
                lockPiece();
            }
        } else if (i == 4) {
            while (!collides(curPiece.type, curPiece.x, curPiece.y + 1, curPiece.rot)) {
                curPiece.y++;
                score += 2;
            }
            lockPiece();
        } else if (i == 5) {
            paused = !paused;
        } else {
            tetris_start();
        }

        lastFall = millis();
        tetris_draw();
        return true;
    }
    return false;
}

bool tetris_update() {
    if (!active || gameOver || paused) return false;

    uint32_t now = millis();
    if (now - lastFall < fallMs) return false;
    lastFall = now;

    if (!collides(curPiece.type, curPiece.x, curPiece.y + 1, curPiece.rot)) {
        curPiece.y++;
    } else {
        lockPiece();
    }
    return true;
}
