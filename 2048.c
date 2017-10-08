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

#define FOR(n) \
  for(int i = 0; i < n; ++i)

#define FOR_TILES \
  for(i8 i = 0; i < TILES_PER_DIM; ++i) \
  for(i8 j = 0; j < TILES_PER_DIM; ++j)

#define EQ(a, b) (memcmp((a), (b), sizeof(*(a))) == 0)

// If a tile at (i, j) is present, tiles[i][j] contains its log_2 value.
// If a tile at (i, j) is not present, tiles[i][j] = 0.
struct board {
  u8 tiles[TILES_PER_DIM][TILES_PER_DIM];
};

struct game {
  struct board board;
  int          score;
  unsigned     seed;
};

static char itoc(int x, char zero) { return x == 0 ? zero : (x + '0'); }
static void rep(char c, int n)     { FOR(n) addch(c); }

// draws nothing if x == 0
static void draw_u8(int x) {
  x = x == 0 ? 0 : (1 << x);
  int hspace = TILE_WIDTH - 4;
  int left_hspace = hspace / 2;
  int right_hspace = hspace - left_hspace;
  rep(' ', left_hspace);
  int div = 1000;
  FOR(4) {
    addch(itoc(
      (x / div) % 10,
      x < div ? ' ' : '0'));
    div /= 10;
  }
  rep(' ', right_hspace);
}

static void h_line(char v_edge, char h_edge, int lspace) {
  rep(' ', lspace);
  FOR(TILES_PER_DIM) {
    addch(v_edge);
    rep(h_edge, TILE_WIDTH);
  }
  addch(v_edge);
  addch('\n');
}

static void h_lines(char v_edge, char h_edge, int lspace, int n) {
  FOR(n) h_line(v_edge, h_edge, lspace);
}

static void draw_tile_contents_row(const u8 row[TILES_PER_DIM], int lspace) {
  rep(' ', lspace);
  FOR(TILES_PER_DIM) {
    addch('|');
    draw_u8(row[i]);
  }
  addch('|');
  addch('\n');
}

static void print(const struct game* g) {
  int x, y;
  getmaxyx(stdscr, y, x);
  int lspace = (x - ((1 + TILE_WIDTH ) * TILES_PER_DIM + 1)) / 2;
  int tspace = (y - ((1 + TILE_HEIGHT) * TILES_PER_DIM + 1)) / 2 - 3;
  int top_vspace = (TILE_HEIGHT - 1) / 2;
  int bot_vspace = (TILE_HEIGHT - 1) - top_vspace;
  rep('\n', tspace);
  rep(' ', x/2 - 5);
  printw("Score: %d", g->score);
  rep('\n', 2);
  FOR(TILES_PER_DIM) {
    h_line('+', '-', lspace);
    h_lines('|', ' ', lspace, top_vspace);
    draw_tile_contents_row(g->board.tiles[i], lspace);
    h_lines('|', ' ', lspace, bot_vspace);
  }
  h_line('+', '-', lspace);
}

static void draw(const struct game* g) {
  move(0, 0);
  print(g);
  refresh();
}

int count_zeros(const struct board* b) {
  int zeros = 0;
  FOR_TILES { zeros += b->tiles[i][j] == 0; }
  return zeros;
}

static void new_tile(struct board* b, unsigned* seed) {
  int zeros = count_zeros(b);
  int tile = rand_r(seed) % zeros;
  u8 tile_size = 1 + (rand_r(seed) % 10 < 1);
  FOR_TILES {
    if(b->tiles[i][j] == 0 && tile-- == 0) {
      b->tiles[i][j] = tile_size;
      break;
    }
  }
}

static void rotate_cw(struct board* b) {
  struct board r;
  FOR_TILES { r.tiles[i][j] = b->tiles[4-j-1][i]; }
  *b = r;
}

static int move_nonzero_first(u8 row[], int len) {
  int num_nonzero = 0;
  FOR(len) if(row[i])    row[num_nonzero++] = row[i];
  FOR(len - num_nonzero) row[num_nonzero+i] = 0;
  return num_nonzero;
}

static void merge_row_left(u8 row[TILES_PER_DIM]) {
  int num_nonzero = move_nonzero_first(row, TILES_PER_DIM);
  FOR(num_nonzero - 1) {
    if(row[i+0] != row[i+1]) continue;
    row[i+0] += 1;
    row[i+1]  = 0;
  }
  move_nonzero_first(row, num_nonzero);
}

static void merge_left(struct board* b) {
  FOR(TILES_PER_DIM)
    merge_row_left(b->tiles[i]);
}

static bool is_victory(const struct board* b) {
  bool r = false;
  FOR_TILES { r |= b->tiles[i][j] >= TARGET_TILE; }
  return r;
}

static bool is_loss(const struct board* b) {
  bool no_moves = true;
  struct board rotated = *b;
  FOR(4) {
    struct board merged = rotated;
    merge_left(&merged);
    no_moves &= EQ(&rotated, &merged);
    rotate_cw(&rotated);
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

static int new_points(const struct board* b0, const struct board* b1) {
  i8 dcount[TARGET_TILE+1] = {0};
  FOR_TILES { dcount[b0->tiles[i][j]] -= 1; }
  FOR_TILES { dcount[b1->tiles[i][j]] += 1; }
  int score = 0;
  for(int i = TARGET_TILE; i >= 1; i--) {
    if(dcount[i] <= 0) continue;
    dcount[i-1] += 2;
    score += (int)dcount[i] << i;
  }
  return score;
}

static void update(struct game* g, int key) {
  int rotations = cw_rotations_of_key(key);
  if(rotations < 0) return;

  struct board* board = &g->board;
  struct board  b0    = *board;

  FOR(4) {
    if(i == rotations) merge_left(board);
    rotate_cw(board);
  }

  g->score += new_points(&b0, board);

  if(!EQ(board, &b0))
    new_tile(board, &g->seed);
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
  FOR(INITIAL_TILES) new_tile(&g.board, &g.seed);
  while(!is_victory(&g.board) && !is_loss(&g.board)) {
    draw(&g);
    int key = getch();
    update(&g, key);
  }
  endwin();
  printf("You %s, with score %d!\n",
    is_victory(&g.board) ? "WIN" : "LOSE",
    g.score);
}
