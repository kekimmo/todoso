
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#define GL_GLEXT_PROTOTYPES
#include <SDL/SDL.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <png.h>


static const int TEXTURE_SIZE = 32;
static const int COORD_PREC = 100;
static const int TILE_SIZE = 3200;


static const int NULL_TEXTURE = 0;



// Tile coordinate to pixel coordinate (center of tile)
int pc (const int tile_coord) {
  return tile_coord * TILE_SIZE + TILE_SIZE / 2;
}


int pc_corner (const int tile_coord) {
  return tile_coord * TILE_SIZE;
}


// Pixel coordinate to tile coordinate
int tc (const int pixel_coord) {
  return pixel_coord / TILE_SIZE;
}


bool bresenham (int x0, int y0, int x1, int y1, bool (*callback) (int, int, const void*, void*), const void* data_in, void* data_out) {
  const int dx = abs(x1 - x0);
  const int dy = abs(y1 - y0);

  const int sx = (x0 < x1) ? 1 : -1;
  const int sy = (y0 < y1) ? 1 : -1;

  int err = dx - dy;

  for (;;) {
    if (x0 == x1 && y0 == y1) {
      return true;
    }

    const bool cont = callback(x0, y0, data_in, data_out);
    if (!cont) {
      return false;
    }

    const int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}



typedef struct {
  int x;
  int y;
  int angle;
  int radius;
} Actor;


typedef enum {
  FORWARD = 0,
  BACKWARD,
  LEFT,
  RIGHT,
  ACTIVATE,
  NOP,
} Action;
const int ACTION_COUNT = NOP + 1;


typedef struct Tile_ Tile;
struct Tile_ {
  int code;
  bool active;
  int activation_time; // -1 means cannot be activated, 0 means instant
  int flips_in;
  bool (*passable) (const Tile*);
  bool (*see_through) (const Tile*);
};


typedef struct {
  int width;
  int height;
  Tile* tiles;
} Level;


typedef enum {
  DEBUG,
  ERROR
} LogLevel;


void actual_log (const LogLevel level, const char* file, const int line,
    const char* func, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  fprintf(stderr, "[%d, %s:%d %s] ", level, file, line, func);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");

  va_end(ap);
}

#define macro_log(level,fmt,...) actual_log(level, __FILE__, __LINE__, __func__, fmt, __VA_ARGS__)

#define log_e(fmt,...) macro_log(ERROR, fmt, __VA_ARGS__)
#define log_d(fmt,...) macro_log(DEBUG, fmt, __VA_ARGS__)


bool tile_always (const Tile* tile) {
  return true;
}

bool tile_never (const Tile* tile) {
  return false;
}

bool tile_if_active (const Tile* tile) {
  return tile->active;
}


Tile* tile_at (const Level* const level, const int x, const int y) {
  assert(x >= 0);
  assert(x < level->width);
  assert(y >= 0);
  assert(y < level->height);
  return &level->tiles[y * level->width + x];
}


bool passable (const Level* const level, const int x, const int y) {
  const Tile* tile = tile_at(level, x, y);
  return tile->passable(tile);
}


bool see_through (const Level* const level, const int x, const int y) {
  const Tile* tile = tile_at(level, x, y);
  return tile->see_through(tile);
}


bool can_be_activated (const Level* const level, const int x, const int y) {
  const Tile* tile = tile_at(level, x, y);
  return tile->activation_time >= 0 && tile->flips_in < 0;
}


bool load_level (const char* filename, Level* level) {
  FILE* file = fopen(filename, "r");
  if (file == NULL) {
    log_e("Could not open %s: %s", filename, strerror(errno));
    return false;
  }

  log_d("Loading level %s...", filename);

  int width = 0;
  while (!feof(file) && fgetc(file) != '\n') {
    ++width;
  }

  int height = 1;
  while (!feof(file)) {
    if (fgetc(file) == '\n') {
      ++height;
    }
  }

  rewind(file);

  log_d("Level dimensions: %d x %d", width, height);

  level->width = width;
  level->height = height;

  level->tiles = calloc(width * height, sizeof(Tile));
  if (!level->tiles) { 
    log_e("Memory allocation failed: %s", strerror(errno));
    return false;
  }

  int i = 0;
  while (!feof(file)) {
    char c = fgetc(file);
    if (c == '\n' || c == EOF) {
      continue;
    }

    Tile* t = &level->tiles[i++];

    t->code = 0;
    t->active = false;
    t->activation_time = -1;
    t->flips_in = -1;
    t->passable = tile_never;
    t->see_through = tile_never;

    switch (c) {
      case ' ':
        t->passable = tile_always;
        t->see_through = tile_always;
        break;

      case '#':
        t->code = 1;
        break;

      case '+':
        t->code = 2;
        t->activation_time = 10;
        t->passable = tile_if_active;
        t->see_through = tile_if_active;
        break;

      default:
        log_e("Invalid tile: '%c'", c);
        break;
    }
  }

  return true;
}


void free_level (Level level) {
  free(level.tiles);
}


int d (int x1, int y1, int x2, int y2) {
  return round(sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2)));
}


void turn (Actor* const actor, int degrees) {
  degrees += actor->angle;
  degrees %= 360;
  if (degrees < 0) {
    degrees = 360 + degrees;
  }

  assert(degrees >= 0);
  assert(degrees < 360);

  actor->angle = degrees;
}


void move (Actor* const actor, const int step) {
  double angle_rad = actor->angle / 180.0 * M_PI;

  actor->x += step * cos(angle_rad);
  actor->y -= step * sin(angle_rad);
}


typedef enum {
  MARK_TILE_PLAYER_ON,
  MARK_TILE_PLAYER_FACING,
  MARK_DOT,
  MARK_CAN_SEE,
  MARK_ACTOR_SIGHT,
} MarkReason;


typedef struct {
  MarkReason reason;
  int x;
  int y;
} Mark;


typedef struct {
  Mark* marks;
  int len;
  int max_len;
} MarkList;


void mark (MarkList* const list, MarkReason reason, int x, int y) {
  if (list->len == list->max_len) {
    log_e("Tile mark list already at maximum capacity: %d", list->max_len);
    return;
  }

  Mark* const mark = &list->marks[list->len++];
  mark->reason = reason;
  mark->x = x;
  mark->y = y;
}


int sign (const double number) {
  return (number > 0.0) - (number < 0.0);
}


void check_corner (Actor* const player, const double angle, const int x, const int y) {
  if (d(x, y, player->x, player->y) < player->radius) {
    
    player->x = x - player->radius * cos(angle);
    player->y = y - player->radius * sin(angle);
  }
}


int find_voronoi (const int tx, const int ty, const int ax, const int ay) {
  const double left = pc_corner(tx);
  const double right = left + TILE_SIZE;
  const double top = pc_corner(ty);
  const double bottom = top + TILE_SIZE;

  const bool in_left = ax < left;
  const bool in_top = ay < top;
  const bool in_right = ax > right;
  const bool in_bottom = ay > bottom;

  // Vertices
  if (in_top && in_left) return 1;
  else if (in_top && in_right) return 3;
  else if (in_bottom && in_left) return 7;
  else if (in_bottom && in_right) return 9;

  // Edges (must check distance to center)
  // NOTE: assume square tile
  const int dx = ax - pc(tx);
  const int dy = ay - pc(ty);

  if (abs(dx) > abs(dy)) {
    if (dx <= 0) {
      return 4;
    }
    else {
      return 6;
    }
  }
  else {
    if (dy <= 0) {
      return 2;
    }
    else {
      return 8;
    }
  }
}


double dot_product (double x1, double y1, double x2, double y2) {
  return x1 * x2 + y1 * y2;
}


double length (double x, double y) {
  return sqrt(x*x + y*y);
}


typedef struct {
  double x;
  double y;
} Vector;


// dirx, diry: push direction unit vector
// thwx, thwy: tile halfwidth vector
// r: circle radius 
// tp_len: tile-circle centerpoint distance
Vector find_push (double dirx, double diry, double thwx, double thwy, double r, double tp_len) {
  const double dp = dot_product(dirx, diry, thwx, thwy);

  const double tprox = dp * dirx;
  const double tproy = dp * diry;

  double push_len = r + dp - tp_len;
  if (push_len < 0.0) {
    push_len = 0.0;
  }

  Vector push = { dirx * push_len, diry * push_len };
  return push;
}


bool collide (MarkList* const mark_list, const Level level, Actor* const actor) {
  const int left = tc(actor->x - actor->radius);
  const int right = tc(actor->x + actor->radius);
  const int top = tc(actor->y - actor->radius);
  const int bottom = tc(actor->y + actor->radius);

  const int tr = TILE_SIZE / 2;

  bool moved = false;

  for (int y = top; y <= bottom; ++y) {
    for (int x = left; x <= right; ++x) {
      const int tcx = pc_corner(x);
      const int tcy = pc_corner(y);

      const int al = actor->x - actor->radius;
      const int at = actor->y - actor->radius;
      const int ar = actor->x + actor->radius;
      const int ab = actor->y + actor->radius;

      if (ar <= tcx) continue;
      if (ab <= tcy) continue;
      if (al >= tcx + TILE_SIZE) continue;
      if (at >= tcy + TILE_SIZE) continue;

      mark(mark_list, MARK_TILE_PLAYER_ON, x, y);
      if (!passable(&level, x, y)) {
        const double tpx = actor->x - pc(x);
        const double tpy = actor->y - pc(y);

        const double tp_len = length(tpx, tpy);

        const double dirx = tpx / tp_len;
        const double diry = tpy / tp_len;

        Vector push = { 0.0, 0.0 };

        const int voronoi = find_voronoi(x, y, actor->x, actor->y);
        switch (voronoi) {
          case 2:
            push.y = tcy - ab;
            assert(push.y < 0);
            break;

          case 4:
            push.x = tcx - ar;
            assert(push.x < 0);
            break;

          case 6:
            push.x = tcx + TILE_SIZE - al;
            assert(push.x > 0);
            break;

          case 8:
            push.y = tcy + TILE_SIZE - at;
            assert(push.y > 0);
            break;

          case 1:
            push = find_push(dirx, diry, -tr, -tr, actor->radius, tp_len);
            assert(push.x <= 0);
            assert(push.y <= 0);
            break;

          case 3:
            push = find_push(dirx, diry, tr, -tr, actor->radius, tp_len);
            assert(push.x >= 0);
            assert(push.y <= 0);
            break;

          case 7:
            push = find_push(dirx, diry, -tr, tr, actor->radius, tp_len);
            assert(push.x <= 0);
            assert(push.y >= 0);
            break;

          case 9:
            push = find_push(dirx, diry, tr, tr, actor->radius, tp_len);
            assert(push.x >= 0);
            assert(push.y >= 0);
            break;
        }
        
        const int ix = round(push.x + 0.5 * sign(push.x)); 
        const int iy = round(push.y + 0.5 * sign(push.y));

        if (ix != 0 || iy != 0) {
          actor->x += ix;
          actor->y += iy;
          moved = true;
        }
      }
    }
  }

  return moved;
}


void check_tiles (const Level* const level) {
  for (int i = 0; i < level->width * level->height; ++i) {
    Tile* const tile = &level->tiles[i];
    if (tile->flips_in > 0) {
      --tile->flips_in;
    }
    else if (tile->flips_in == 0) {
      tile->flips_in = -1;
      tile->active = !tile->active;
    }
  }
}


typedef struct {
  Actor* actors;
  int len;
  int max;
} ActorList;


bool line_of_sight_callback (int x, int y, const void* in, void* out) {
  const Level* level = in;;
  return see_through(level, x, y);
}


bool mark_actor_sight_callback (int x, int y, const void* in, void* out) {
  MarkList* mark_list = out;
  mark(mark_list, MARK_ACTOR_SIGHT, x, y);
  return true;
}


bool line_of_sight (MarkList* mark_list, const Level* level, int x1, int y1, int x2, int y2) {
  if (bresenham(x1, y1, x2, y2, line_of_sight_callback, level, NULL)) {
    bresenham(x1, y1, x2, y2, mark_actor_sight_callback, NULL, mark_list);
    return true;
  }
  else {
    return false;
  }
}


void move_actors (MarkList* mark_list, const ActorList* actor_list, Level* level, const Actor* player) {
  const int px = player->x;
  const int py = player->y;

  for (int i = 0; i < actor_list->len; ++i) {
    Actor* const actor = &actor_list->actors[i];
    const int ax = actor->x;
    const int ay = actor->y;

    // Vector from actor to player
    const double dx = px - ax;
    const double dy = py - ay;

    int to_player = atan2(-dy, dx) / M_PI * 180.0;
    if (to_player < 0) {
      to_player = 360 + to_player;
    }

    int diff = to_player - actor->angle;
    if (diff > 180) {
      diff -= 360;
    }
    else if (diff < -180) {
      diff += 360;
    }

    const int ACTOR_FOV = 180;
    if (abs(diff) < ACTOR_FOV / 2.0) {
      const bool los = line_of_sight(mark_list, level,
          tc(actor->x), tc(actor->y), tc(px), tc(py));

      if (los) {
        const int ACTOR_TURN = 3;
        if (diff > 10) {
          turn(actor, ACTOR_TURN);
        }
        else if (diff < 10) {
          turn(actor, -ACTOR_TURN);
        }

        const int ACTOR_STEP = 300;

        if (length(dx, dy) > player->radius + actor->radius + 2 * ACTOR_STEP) {
          move(actor, ACTOR_STEP); 
        }
      }
    }
  }
}


void game (int frame, Level level, MarkList* const mark_list, Actor* const player, const bool actions[], const ActorList actors) {
  const int PLAYER_TURN = 6;
  const int PLAYER_STEP = 400;

  if (actions[LEFT] | actions[RIGHT]) {
    turn(player, actions[LEFT] ? PLAYER_TURN : -PLAYER_TURN);
  }
  if (actions[FORWARD] || actions[BACKWARD]) {
    const int step = actions[FORWARD] ? PLAYER_STEP : -PLAYER_STEP;
    move(player, step);
  }

  move_actors(mark_list, &actors, &level, player);

  check_tiles(&level);

  bool moved = false;
  static const int tries = 10;
  int i = 0;
  do {
    if (i == tries) {
      log_e("Collision state still unsettled after %d tries, giving up!",
          tries);
      break;
    }
    ++i;
    moved = collide(mark_list, level, player);
    for (int i = 0; i < actors.len; ++i) {
      if (collide(mark_list, level, &actors.actors[i])) {
        moved = true;
      }
    }
  } while (moved);

  const int tx = tc(player->x);
  const int ty = tc(player->y);
  const double a = player->angle / 180.0 * M_PI;

  double fr = player->radius;
  int fx;
  int fy;

  do {
    fr += TILE_SIZE / 2;
    fx = tc(player->x + fr * cos(a));
    fy = tc(player->y - fr * sin(a));
  }
  while (fx == tx && fy == ty);
  if (can_be_activated(&level, fx, fy)) {
    if (actions[ACTIVATE]) {
      Tile* tile = tile_at(&level, fx, fy);
      tile->flips_in = tile->activation_time;
      log_d("Tile (%d, %d) is now %s", fx, fy, tile->active ? "deactivating" : "activating");
    }
    else {
      mark(mark_list, MARK_TILE_PLAYER_FACING, fx, fy);
    }
  }
}


typedef struct {
  int x;
  int y;
} Point;


int clamp (int number, int lower, int upper) {
  assert(lower <= upper);

  if (number < lower) {
    return lower;
  }
  else if (number > upper) {
    return upper;
  }
  else {
    return number;
  }
}


typedef struct {
  int ox;
  int oy;
  int width;
  int height;
  bool* tiles;
} Sight;


void free_sight (Sight* const sight) {
  free(sight->tiles);
  free(sight);
}


void sight_set (Sight* const sight, int x, int y) {
  const int sx = x - sight->ox;
  const int sy = y - sight->oy;
  assert(sx >= 0);
  assert(sy >= 0);
  assert(sx < sight->width);
  assert(sy < sight->height);
  sight->tiles[sy * sight->width + sx] = true;
}


bool sight_get (const Sight* const sight, int x, int y) {
  const int sx = x - sight->ox;
  const int sy = y - sight->oy;
  if (sx < 0 || sy < 0 || sx >= sight->width || sy >= sight->height) {
    return false;
  }
  return sight->tiles[sy * sight->width + sx] ||
    ((sx == 0 ||  sight->tiles[sy * sight->width + sx - 1]) &&
     (sx == sight->width - 1 || sight->tiles[sy * sight->width + sx + 1]) &&
     (sy == 0 || sight->tiles[(sy - 1) * sight->width + sx]) &&
     (sy == sight->width - 1 || sight->tiles[sy * sight->width + sx - 1]));
}


bool sight_callback (int x, int y, const void* in, void* out) {
  Sight* sight = out;
  const Level* level = in;

  sight_set(sight, x, y);

  return see_through(level, x, y);
}


Sight* compute_sight (const Level level, const Actor actor, int radius) {
  Sight* sight = malloc(sizeof(Sight));;
  if (sight == NULL) {
    log_e("Failed to allocate Sight: %s", strerror(errno));
    return NULL;
  }

  const int atx = tc(actor.x);
  const int aty = tc(actor.y);

  // Offset (top left tile)
  sight->ox = clamp(atx - radius, 0, level.width - 1);
  sight->oy = clamp(aty - radius, 0, level.height - 1);
  //log_d("Sight offset: (%d, %d)", sight->ox, sight->oy);

  // Table dimensions
  // Example: radius 1
  // ...
  // .@.
  // ...
  const int side = (radius + 1) * (radius + 1);

  sight->width = clamp(side, 1, level.width - sight->ox);
  sight->height = clamp(side, 1, level.height - sight->oy);
  //log_d("Sight dimensions: %d x %d", sight->width, sight->height);

  sight->tiles = calloc(sight->width * sight->height, sizeof(bool));
  if (sight->tiles == NULL) {
    log_e("Failed to allocate sight table: %s", strerror(errno));
    free(sight);
    return NULL;
  }

  int x = radius;
  int y = 0;
  int dx = 1 - radius * 2;
  int dy = 0;
  int err = 0;

  while (x >= y) {
    bresenham(atx, aty, atx + x, aty + y, sight_callback, &level, sight);
    bresenham(atx, aty, atx + y, aty + x, sight_callback, &level, sight);
    bresenham(atx, aty, atx - x, aty + y, sight_callback, &level, sight);
    bresenham(atx, aty, atx - y, aty + x, sight_callback, &level, sight);
    bresenham(atx, aty, atx - x, aty - y, sight_callback, &level, sight);
    bresenham(atx, aty, atx - y, aty - x, sight_callback, &level, sight);
    bresenham(atx, aty, atx + x, aty - y, sight_callback, &level, sight);
    bresenham(atx, aty, atx + y, aty - x, sight_callback, &level, sight);

    ++y;
    err += dy;
    dy += 2;
    if ((err << 1) + dx > 0) {
      --x;
      err += dx;
      dx += 2;
    }
  }

  return sight;
}


void print_error (void) {
  fprintf(stderr, "Error: %s\n", gluErrorString(glGetError()));
}


GLuint load_texture (const char* filename, const int texture_size) {
  FILE* file = fopen(filename, "rb");
  if (file == NULL) {
    log_e("Failed to open %s: %s", filename, strerror(errno));
    return NULL_TEXTURE;
  }

  static const int HEADER_SIZE = 8;

  png_byte header[HEADER_SIZE];
  fread(header, sizeof(png_byte), HEADER_SIZE, file);

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
      NULL, NULL, NULL);

  png_infop info_ptr = png_create_info_struct(png_ptr);
  png_infop end_info_ptr = png_create_info_struct(png_ptr);

  png_init_io(png_ptr, file);
  png_set_sig_bytes(png_ptr, HEADER_SIZE);
  png_read_info(png_ptr, info_ptr);

  const size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);
  png_byte data[texture_size * row_bytes];
  for (int row = 0; row < texture_size; ++row) {
    png_read_row(png_ptr, data + row * row_bytes, NULL);
  }

  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_size, texture_size, 0,
      GL_RGBA, GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  //print_error();
  png_destroy_read_struct(&png_ptr, &info_ptr, &end_info_ptr);

  fclose(file);

  return texture;
}


void draw_texture (const int texture, const int x, const int y, const int angle, const bool center) {
  glBindTexture(GL_TEXTURE_2D, texture);

  glLoadIdentity();
  glTranslated(x / (double)COORD_PREC, y / (double)COORD_PREC, 0.0);
  glRotated(angle, 0.0, 0.0, -1.0);

  glScaled(TEXTURE_SIZE, TEXTURE_SIZE, 1);

  if (!center) {
    glTranslated(0.5, 0.5, 0);
  }

  glBegin(GL_QUADS);

  glTexCoord2d(1.0, 0.0);
  glVertex2d(.5, .5);

  glTexCoord2d(1.0, 1.0);
  glVertex2d(.5, -.5);

  glTexCoord2d(0.0, 1.0);
  glVertex2d(-.5, -.5);

  glTexCoord2d(0.0, 0.0);
  glVertex2d(-.5, .5);

  glEnd();
}


void draw_tile (const int texture, const int x, const int y) {
  if (texture == NULL_TEXTURE) {
    log_e("Attempted to draw with NULL_TEXTURE: %d", NULL_TEXTURE);
    return;
  }
  draw_texture(texture, pc(x), pc(y), 0, true);
}


void draw_level (const Level level, const GLuint tile_textures[],
    const GLuint darkness, const Sight* const sight) {
  int i = 0;
  for (int y = 0; y < level.height; ++y) {
    for (int x = 0; x < level.width; ++x) {
      const int code = level.tiles[i].code + (level.tiles[i].active ? 1 : 0);
      const GLuint texture = tile_textures[code];
      draw_tile(texture, x, y); 
      if (!sight_get(sight, x, y)) {
        draw_tile(darkness, x, y);
      }
      ++i;
    }
  }
} 


int main (int argc, char *argv[]) {
  SDL_Init(SDL_INIT_VIDEO);

  SDL_Event event;
  bool running = true;

  const int win_w = 800;
  const int win_h = 600;

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  SDL_Surface* screen = SDL_SetVideoMode(win_w, win_h, 32, SDL_OPENGL);

  glClearColor(255, 255, 255, 0);
  glViewport(0, 0, win_w, win_h);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(0.0, win_w, win_h, 0.0);
  glMatrixMode(GL_MODELVIEW);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  GLuint tex_player = load_texture("player.png", TEXTURE_SIZE);
  GLuint tex_actor = load_texture("actor2.png", TEXTURE_SIZE);
  GLuint tex_mark = load_texture("mark.png", TEXTURE_SIZE);
  GLuint tex_dot = load_texture("dot.png", TEXTURE_SIZE);
  GLuint tex_darkness = load_texture("darkness.png", TEXTURE_SIZE);
  GLuint tex_actor_sight = load_texture("actor_sight.png", TEXTURE_SIZE);

  GLuint tile_textures[] = {
    load_texture("floor.png", TEXTURE_SIZE),
    load_texture("wall.png", TEXTURE_SIZE),
    load_texture("door.png", TEXTURE_SIZE),
    load_texture("door-open.png", TEXTURE_SIZE)
  };

  int frame = 0;

  const int ACTOR_R = 1500;

  Actor player = { .radius = ACTOR_R, .x = pc(1), .y = pc(1), .angle = 0 };

  typedef struct {
    Action action;
    SDLKey key;
  } Mapping;
  
  Mapping keymap[] = {
    { FORWARD  , SDLK_UP    }, 
    { BACKWARD , SDLK_DOWN  }, 
    { LEFT     , SDLK_LEFT  },
    { RIGHT    , SDLK_RIGHT },
    { ACTIVATE , SDLK_SPACE }
  };
  const int MAPPING_COUNT = 5;

  bool actions[ACTION_COUNT];

  memset(actions, false, ACTION_COUNT * sizeof(bool));

  Uint32 target_ticks = 0;
  const Uint32 FRAME_TICKS = 1000 / 60;
  Level level;

  load_level("level.lev", &level);

  Actor actors[10];
  ActorList actor_list = {
    .len = 0,
    .max = 10,
    .actors = actors
  };

  actors[0].radius = ACTOR_R;
  actors[0].x = pc(15);
  actors[0].y = pc(10);
  actors[0].angle = 0;
  actor_list.len = 1;

  while (running) {
    if (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          running = false;
          break;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
          for (int i = 0; i < MAPPING_COUNT; ++i) {
            if (event.key.keysym.sym == keymap[i].key) {
              actions[keymap[i].action] = (event.type == SDL_KEYDOWN);
              if (event.type == SDL_KEYDOWN) {
                fprintf(stderr, "Action %d\n", keymap[i].action);
              }
            }
          }
          break;
      }
    }

    /* fprintf(stderr, "Actions:"); */
    /* for (int i = 0; i < ACTION_COUNT; ++i) { */
    /*   if (actions[i]) { */
    /*     fprintf(stderr, " %d", i); */
    /*   } */
    /* } */
    /* fprintf(stderr, "\n"); */

    Mark marks[100];
    MarkList mark_list = { .marks = marks, .len = 0, .max_len = 100 };

    game(frame++, level, &mark_list, &player, actions, actor_list);

    const int sight_radius = 5;
    Sight* sight = compute_sight(level, player, sight_radius);

    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    draw_level(level, tile_textures, tex_darkness, sight);
    free_sight(sight);

    for (int i = 0; i < actor_list.len; ++i) {
      const Actor* const actor = &actor_list.actors[i];
      draw_texture(tex_actor, actor->x, actor->y, actor->angle, true);
    }

    draw_texture(tex_player, player.x, player.y, player.angle, true);

    for (int i = 0; i < mark_list.len; ++i) {
      const int x = marks[i].x;
      const int y = marks[i].y;
      switch (marks[i].reason) {
        case MARK_TILE_PLAYER_ON:
          draw_tile(tex_mark, x, y);
          break;
        case MARK_TILE_PLAYER_FACING:
          draw_tile(tex_dot, x, y);
          break;
        case MARK_ACTOR_SIGHT:
          draw_tile(tex_actor_sight, x, y);
          break;
      }
    }

    glDisable(GL_TEXTURE_2D);
    glEnd();

    SDL_GL_SwapBuffers();
  }

  free_level(level);

  SDL_Quit();
}

