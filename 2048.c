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

#define u8 uint8_t

// If a tile at (i, j) is present, tiles[i][j] contains its log_2 value.
// If a tile at (i, j) is not present, tiles[i][j] = 0.
struct board {
  u8 tiles[TILES_PER_DIM][TILES_PER_DIM];
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

static void draw_tile_contents_row(const struct board* b, int i, int lspace) {
  rep(' ', lspace);
  for(int j = 0; j < TILES_PER_DIM; ++j) {
    addch('|');
    draw_u8(b->tiles[i][j]);
  }
  addch('|');
  addch('\n');
}

static void print(const struct board* b) {
  int x, y;
  getmaxyx(stdscr, y, x);
  int lspace = (x - ((1 + TILE_WIDTH ) * TILES_PER_DIM + 1)) / 2;
  int tspace = (y - ((1 + TILE_HEIGHT) * TILES_PER_DIM + 1)) / 2;
  int top_vspace = (TILE_HEIGHT - 1) / 2;
  int bot_vspace = (TILE_HEIGHT - 1) - top_vspace;
  rep('\n', tspace);
  for(int i = 0; i < TILES_PER_DIM; ++i) {
    h_line('+', '-', lspace);
    h_lines('|', ' ', lspace, top_vspace);
    draw_tile_contents_row(b, i, lspace);
    h_lines('|', ' ', lspace, bot_vspace);
  }
  h_line('+', '-', lspace);
}

static void draw(const struct board* b) {
  move(0, 0);
  print(b);
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
  int zeros    = count_zeros(b);
  int tile     = rand_r(seed) % zeros;
  u8 tile_size = 1 + (rand_r(seed) % 10 < 1);
  for(int i = 0; i < TILES_PER_DIM; ++i)
  for(int j = 0; j < TILES_PER_DIM; ++j) {
    if(b.tiles[i][j] != 0) continue;
    if(tile-- == 0) b.tiles[i][j] = tile_size;
  }
  return b;
}

static struct board rotate_cw(struct board b) {
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

static struct board rotate_cw_n(struct board b, int n) {
  while(n--) b = rotate_cw(b);
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

static struct board update(struct board b0, int key, unsigned* seed) {
  int rotations = cw_rotations_of_key(key);
  if(rotations < 0) return b0;

  struct board b = rotate_cw_n(b0, rotations);
  b = merge_left(b);
  b = rotate_cw_n(b, (4 - rotations) % 4);

  if(!eq(b, b0))
    b = new_tile(b, seed);

  return b;
}

static void sigint(int _) {
  endwin();
  exit(0);
}

int main(void) {
  unsigned seed = time(NULL);
  struct sigaction act;
  act.sa_handler = sigint;
  sigaction(SIGINT, &act, NULL);
  initscr();
  curs_set(0);
  cbreak();
  noecho();
  keypad(stdscr, true);
  struct board b = { .tiles = {{0}} };
  for(int i = 0; i < INITIAL_TILES; ++i)
    b = new_tile(b, &seed);
  while(!is_victory(b) && !is_loss(b)) {
    draw(&b);
    int key = getch();
    b = update(b, key, &seed);
  }
  endwin();
  printf("You %s!\n", is_victory(b) ? "WIN" : "LOSE");
}
