#include <curses.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// 2^11 = 2048
#define TARGET_TILE   11
#define INITIAL_TILES 2
#define TILE_WIDTH    8
#define TILE_HEIGHT   3
#define TILES_PER_DIM 4

#define i8  int8_t
#define u8  uint8_t
#define u64 uint64_t

// If a tile at (i, j) is present, tiles[i][j] contains its log_2 value.
// If a tile at (i, j) is not present, tiles[i][j] = 0.
struct board {
  u8 tiles[TILES_PER_DIM][TILES_PER_DIM];
};

struct game {
  struct board board;
  int score;
  unsigned seed;
};

static char itoc(int x, char zero) { return x == 0 ? zero : (x + '0');    }
static void rep(char c, int n)     { for(int i = 0; i < n; ++i) addch(c); }

// draws nothing if x == 0
static void draw_u8(int x) {
  x = x == 0 ? 0 : (1 << x);
  int hspace = TILE_WIDTH - 4;
  int left_hspace = hspace / 2;
  int right_hspace = hspace - left_hspace;
  rep(' ', left_hspace);
  int div = 1000;
  for(int i = 0; i < 4; ++i) {
    addch(itoc(
      (x / div) % 10,
      x < div ? ' ' : '0'));
    div /= 10;
  }
  rep(' ', right_hspace);
}

static void h_line(char v_edge, char h_edge, int lspace) {
  rep(' ', lspace);
  for(int i = 0; i < TILES_PER_DIM; ++i) {
    addch(v_edge);
    rep(h_edge, TILE_WIDTH);
  }
  addch(v_edge);
  addch('\n');
}

static void h_lines(char v_edge, char h_edge, int lspace, int n) {
  for(int i = 0; i < n; ++i) h_line(v_edge, h_edge, lspace);
}

static void draw_tile_contents_row(const u8 row[TILES_PER_DIM], int lspace) {
  rep(' ', lspace);
  for(int i = 0; i < TILES_PER_DIM; ++i) {
    addch('|');
    draw_u8(row[i]);
  }
  addch('|');
  addch('\n');
}

static void print(struct game g) {
  int x, y;
  getmaxyx(stdscr, y, x);
  int lspace = (x - ((1 + TILE_WIDTH ) * TILES_PER_DIM + 1)) / 2;
  int tspace = (y - ((1 + TILE_HEIGHT) * TILES_PER_DIM + 1)) / 2 - 3;
  int top_vspace = (TILE_HEIGHT - 1) / 2;
  int bot_vspace = (TILE_HEIGHT - 1) - top_vspace;
  rep('\n', tspace);
  rep(' ', x/2 - 5);
  printw("Score: %d\n", g.score);
  rep('\n', 2);
  for(int i = 0; i < TILES_PER_DIM; ++i) {
    h_line('+', '-', lspace);
    h_lines('|', ' ', lspace, top_vspace);
    draw_tile_contents_row(g.board.tiles[i], lspace);
    h_lines('|', ' ', lspace, bot_vspace);
  }
  h_line('+', '-', lspace);
}

static void draw(struct game g) {
  move(0, 0);
  print(g);
  refresh();
}

int count_zeros(const struct board b) {
  int zeros = 0;
  for(int i = 0; i < TILES_PER_DIM; ++i)
  for(int j = 0; j < TILES_PER_DIM; ++j)
    zeros += b.tiles[i][j] == 0;
  return zeros;
}

static struct board new_tile(struct board b, unsigned* seed) {
  int zeros = count_zeros(b);
  int tile = rand_r(seed) % zeros;
  u8 tile_size = 1 + (rand_r(seed) % 10 < 1);
  for(int i = 0; i < TILES_PER_DIM; ++i)
  for(int j = 0; j < TILES_PER_DIM; ++j) {
    if(b.tiles[i][j] != 0) continue;
    if(tile-- == 0) b.tiles[i][j] = tile_size;
  }
  return b;
}

static struct board rotate_cw(const struct board b) {
  struct board r;
  for(int i = 0; i < TILES_PER_DIM; ++i)
  for(int j = 0; j < TILES_PER_DIM; ++j)
    r.tiles[i][j] = b.tiles[4-j-1][i];
  return r;
}

static int move_nonzero_first(u8 row[], int len) {
  int num_nonzero = 0;
  for(int i = 0; i < len; i++) {
    if(row[i] != 0) row[num_nonzero++] = row[i];
  }
  memset(&row[num_nonzero], 0, len - num_nonzero);
  return num_nonzero;
}

static void merge_row_left(u8 row[TILES_PER_DIM]) {
  int num_nonzero = move_nonzero_first(row, TILES_PER_DIM);
  for(int i = 0; i < num_nonzero - 1; ++i) {
    if(row[i] != row[i+1]) continue;
    row[i]++;
    row[i+1] = 0;
  }
  move_nonzero_first(row, num_nonzero);
}

static struct board merge_left(struct board b) {
  for(int i = 0; i < TILES_PER_DIM; ++i)
    merge_row_left(b.tiles[i]);
  return b;
}

static bool is_victory(const struct board b) {
  bool r = false;
  for(int i = 0; i < TILES_PER_DIM; ++i)
  for(int j = 0; j < TILES_PER_DIM; ++j)
    r |= b.tiles[i][j] >= TARGET_TILE;
  return r;
}

static bool eq(const struct board a, const struct board b) {
  return memcmp(&a, &b, sizeof(struct board)) == 0;
}

static bool is_loss(struct board b) {
  bool no_moves = true;
  for(int i = 0; i < 4; ++i) {
    no_moves &= eq(b, merge_left(b));
    b = rotate_cw(b);
  }
  return no_moves;
}

static int cw_rotations_of_key(int key) {
  switch(key) {
  case KEY_LEFT:  return 0;
  case KEY_DOWN:  return 1;
  case KEY_RIGHT: return 2;
  case KEY_UP:    return 3;
  default:        return -1;
  }
}

static int dscore(struct board b0, struct board b1) {
  i8 b0_count[TARGET_TILE+1] = {0};
  i8 b1_count[TARGET_TILE+1] = {0};
  for(int i = 0; i < TILES_PER_DIM; ++i)
  for(int j = 0; j < TILES_PER_DIM; ++j) {
    b0_count[b0.tiles[i][j]]++;
    b1_count[b1.tiles[i][j]]++;
  }
  int score = 0;
  for(int i = TARGET_TILE; i >= 1; i--) {
    i8 dcount = b1_count[i] - b0_count[i];
    if(dcount > 0) {
      b0_count[i-1] -= 2;
      score += (int)dcount << i;
    }
  }
  return score;
}

static struct game update(struct game g, int key) {
  int rotations = cw_rotations_of_key(key);
  if(rotations < 0) return g;

  struct board b0 = g.board;

  for(int i = 0; i < 4; ++i) {
    if(i == rotations) g.board = merge_left(g.board);
    g.board = rotate_cw(g.board);
  }

  g.score += dscore(b0, g.board);

  if(!eq(g.board, b0))
    g.board = new_tile(g.board, &g.seed);

  return g;
}

static void sigint(int _) {
  endwin();
  exit(0);
}

int main(void) {
  struct sigaction act;
  act.sa_handler = sigint;
  sigaction(SIGINT, &act, NULL);
  initscr();
  curs_set(0);
  cbreak();
  noecho();
  keypad(stdscr, true);
  struct game g =
    { .board = { .tiles = {{0}} }
    , .score = 0
    , .seed  = time(NULL)
    };
  for(int i = 0; i < INITIAL_TILES; ++i)
    g.board = new_tile(g.board, &g.seed);
  while(!is_victory(g.board) && !is_loss(g.board)) {
    draw(g);
    int key = getch();
    g = update(g, key);
  }
  endwin();
  printf("You %s, with score %d!\n",
    is_victory(g.board) ? "WIN" : "LOSE",
    g.score);
}
