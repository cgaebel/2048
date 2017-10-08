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

#define i8 int8_t
#define u8 uint8_t

#define FOR(i, s, n) \
  for(i8 i = s; i < n; ++i)

#define FOR_TILES(i, j) \
  FOR(i, 0, TILES_PER_DIM) \
  FOR(j, 0, TILES_PER_DIM)

#define EQ(a, b) \
  (memcmp((a), (b), sizeof(*(a))) == 0)

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
static void rep(char c, int n)     { FOR(i, 0, n) addch(c); }

// draws nothing if x == 0
static void draw_u8(int x) {
  x = x == 0 ? 0 : (1 << x);
  int hspace = TILE_WIDTH - 4;
  int left_hspace = hspace / 2;
  int right_hspace = hspace - left_hspace;
  rep(' ', left_hspace);
  int div = 1000;
  FOR(i, 0, 4) {
    addch(itoc(
      (x / div) % 10,
      x < div ? ' ' : '0'));
    div /= 10;
  }
  rep(' ', right_hspace);
}

static void h_line(char v_edge, char h_edge, int lspace) {
  rep(' ', lspace);
  FOR(i, 0, TILES_PER_DIM) {
    addch(v_edge);
    rep(h_edge, TILE_WIDTH);
  }
  addch(v_edge);
  addch('\n');
}

static void h_lines(char v_edge, char h_edge, int lspace, int n) {
  FOR(i, 0, n) h_line(v_edge, h_edge, lspace);
}

static void draw_tile_contents_row(const u8 row[TILES_PER_DIM], int lspace) {
  rep(' ', lspace);
  FOR(i, 0, TILES_PER_DIM) {
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
  FOR(i, 0, TILES_PER_DIM) {
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
  FOR_TILES(i, j) zeros += b->tiles[i][j] == 0;
  return zeros;
}

static void new_tile(struct board* b, unsigned* seed) {
  int zeros = count_zeros(b);
  int tile = rand_r(seed) % zeros;
  // 10% chance of 4, 90% chance of 2
  u8 tile_size = 1 + (rand_r(seed) % 10 < 1);
  FOR_TILES(i, j) {
    if(b->tiles[i][j] == 0 && tile-- == 0)
      b->tiles[i][j] = tile_size;
  }
}

static void rotate_cw(struct board* b) {
  struct board r;
  FOR_TILES(i, j) r.tiles[i][j] = b->tiles[4-j-1][i];
  *b = r;
}

static int move_nonzero_first(u8 row[], u8 len) {
  int num_nonzero = 0;
  FOR(i, 0, len) if(row[i])    row[num_nonzero++] = row[i];
  FOR(i, 0, len - num_nonzero) row[num_nonzero+i] = 0;
  return num_nonzero;
}

static void merge_row_left(u8 row[TILES_PER_DIM]) {
  u8 num_nonzero = move_nonzero_first(row, TILES_PER_DIM);
  FOR(i, 0, num_nonzero - 1) {
    if(row[i+0] != row[i+1]) continue;
    row[i+0] += 1;
    row[i+1]  = 0;
  }
  move_nonzero_first(row, num_nonzero);
}

static void merge_left(struct board* b) {
  FOR(i, 0, TILES_PER_DIM)
    merge_row_left(b->tiles[i]);
}

static bool is_victory(const struct board* b) {
  bool r = false;
  FOR_TILES(i, j) r |= b->tiles[i][j] >= TARGET_TILE;
  return r;
}

static bool is_loss(const struct board* b) {
  bool no_moves = true;
  struct board rotated = *b;
  FOR(i, 0, 4) {
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
  // dcount = # old tiles - # new tiles
  i8 dcount[TARGET_TILE+1] = {0};
  FOR_TILES(i, j) dcount[b0->tiles[i][j]] += 1;
  FOR_TILES(i, j) dcount[b1->tiles[i][j]] -= 1;
  int score = 0;
  FOR(i, 1, TARGET_TILE) {
    i8 upgrades = dcount[i] / 2;
    if(upgrades <= 0) continue;
    score += (int)upgrades << (int)(i+1);
    dcount[i+1] += upgrades;
  }
  return score;
}

static void update(struct game* g, int key) {
  int rotations = cw_rotations_of_key(key);
  if(rotations < 0) return;

  struct board* board = &g->board;
  struct board  b0    = *board;

  FOR(i, 0, 4) {
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
  FOR(i, 0, INITIAL_TILES) new_tile(&g.board, &g.seed);
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
