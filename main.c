
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

static const int ACTOR_TURN = 3;
static const int ACTOR_STEP = 300;
static const int ACTOR_FOV = 180;


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
  int base_x;
  int base_y;
  int base_angle;
  int tx;
  int ty;
  int t_angle;
  int give_up_at;
} Actor;


void init_actor (Actor* actor, int x, int y, int angle, int radius) {
  actor->x = x;
  actor->y = y;
  actor->angle = angle;
  actor->radius = radius;
  actor->base_x = x;
  actor->base_y = y;
  actor->base_angle = angle;
  actor->tx = -1;
  actor->ty = -1;
  actor->t_angle = -1;
  actor->give_up_at = -1;
}


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
  MARK_ACTOR_PATH,
  MARK_ACTOR_SPOTTED,
  MARK_ACTOR_CHASING,
  MARK_ACTOR_LOST,
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


bool collide_actor_actor (Actor* a, Actor* b) {
  const double dx = b->x - a->x;
  const double dy = b->y - a->y;

  const double d_len = length(dx, dy);
  const double overlap = a->radius + b->radius - d_len;

  if (overlap > 0.0) {
    const double push_x = 0.5 * overlap * dx / d_len;
    const double push_y = 0.5 * overlap * dy / d_len;

    a->x -= round(push_x + 0.5 * sign(push_x));
    a->y -= round(push_y + 0.5 * sign(push_y));
    b->x += round(push_x + 0.5 * sign(push_x));
    b->y += round(push_y + 0.5 * sign(push_y));

    return true;
  }

  return false;
}


bool collide_level_actor (MarkList* const mark_list, const Level level, Actor* const actor) {
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

      //mark(mark_list, MARK_TILE_PLAYER_ON, x, y);
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


typedef struct Point_ {
  int x;
  int y;
  struct Point_* next;
} Point;


Point* add_point (Point* head, int x, int y) {
  Point* p = malloc(sizeof(Point));
  p->x = x;
  p->y = y;
  p->next = head;
  return p;
}


Point* remove_point (Point* head, int x, int y) {
  if (head == NULL) {
    return NULL;
  }
  else if (head->x == x && head->y == y) {
    Point* new_head = head->next;
    free(head);
    return new_head;
  }
  else {
    for (Point* i = head; i->next != NULL; i = i->next) {
      if (i->next->x == x && i->next->y == y) {
        Point* new_next = i->next->next;
        free(i->next);
        i->next = new_next;
        break;
      }
      
    }
    return head;
  }
}


bool has_point (Point* head, int x, int y) {
  while (head != NULL) {
    if (head->x == x && head->y == y) {
      return true;
    }
    head = head->next;
  }
  return false;
}


void free_point_list (Point* point) {
  if (point != NULL) {
    free_point_list(point->next);
    free(point);
  }
}


Point* remove_last_point (Point* point) {
  if (point == NULL) {
    return NULL;
  }
  else {
    Point* new_next = point->next;
    free(point);
    return point->next;
  }
}


int find_path_h (int x1, int y1, int x2, int y2) {
  const int dx = x2 - x1;
  const int dy = y2 - y1;
  return dx*dx + dy*dy;
}


Point* neighbors (const Level* level, int x, int y) {
  bool p[8];

  // 4 0 5
  // 1   2
  // 6 3 7

  struct {
    int x;
    int y;
  } coords[8] = {
    {  0, -1 },
    { -1,  0 },
    {  1,  0 },
    {  0,  1 },

    { -1, -1 },
    {  1, -1 },
    { -1,  1 },
    {  1,  1 },
  };

  // Drop nonexistent tiles
  p[0] = y > 0;
  p[1] = x > 0;
  p[2] = x < level->width;
  p[3] = y < level->height;
  p[4] = p[0] && p[1];
  p[5] = p[0] && p[2];
  p[6] = p[1] && p[3];
  p[7] = p[2] && p[3];

  // Drop unpassable side tiles
  for (int i = 0; i < 4; ++i) {
    p[i] = p[i] && passable(level, x + coords[i].x, y + coords[i].y);
  }

  // Drop corner tiles like X here:
  // .#X
  // .@#
  // ...
  /* p[4] = p[0] || p[1]; */ 
  /* p[5] = p[0] || p[2]; */ 
  /* p[6] = p[1] || p[3]; */ 
  /* p[7] = p[2] || p[3]; */ 
  // Actually, drop these too:
  // .#X
  // .@.
  // ...
  p[4] = p[0] && p[1]; 
  p[5] = p[0] && p[2]; 
  p[6] = p[1] && p[3]; 
  p[7] = p[2] && p[3]; 

  // Drop unpassable corner tiles
  for (int i = 4; i < 8; ++i) {
    p[i] = p[i] && passable(level, x + coords[i].x, y + coords[i].y);
  }

  // Make the final list
  Point* n = NULL;
  for (int i = 0; i < 8; ++i) {
    if (p[i]) {
      n = add_point(n, x + coords[i].x, y + coords[i].y);
    }
  }

  return n;
}


bool dfs (const Level* level, Point** path, int x1, int y1, int x2, int y2) {
  if (x1 >= 0 && y1 >= 0 && x1 < level->width && y1 < level->height &&
      !has_point(*path, x1, y1) && passable(level, x1, y1)) {
    *path = add_point(*path, x1, y1);

    if (x1 == x2 && y1 == y2) {
      return true;
    }

    Point* n = neighbors(level, x1, y1); 

    while (n != NULL) {
      Point* best = n;
      int best_h = find_path_h(n->x, n->y, x2, y2);
      for (Point* i = n->next; i != NULL; i = i->next) {
        const int i_h = find_path_h(i->x, i->y, x2, y2);
        if (i_h < best_h) {
          best = i;
          best_h = i_h;
        }
      }

      if (dfs(level, path, best->x, best->y, x2, y2)) {
        return true;
      }
      else {
        n = remove_point(n, best->x, best->y);
      }
    }

    *path = remove_last_point(*path);
  }

  return false;
}


Point* a_star (const Level* level, int x1, int y1, int x2, int y2) {
  Point* closed = NULL;
  Point* open = add_point(NULL, x1, y1);
  struct {
    int g;
    int f;
    int from_x;
    int from_y;
  } data[level->width][level->height];

  data[x1][y1].g = 0;
  data[x1][y1].f = find_path_h(x1, y1, x2, y2);

  while (open != NULL) {
    Point* cur = open;
    int min_f = data[open->x][open->y].f;
    for (Point* i = open; i != NULL; i = i->next) {
      const int f = data[i->x][i->y].f;
      if (f < min_f) {
        cur = i;
        min_f = f;
      }
    }

    const int cx = cur->x;
    const int cy = cur->y;

    if (cx == x2 && cy == y2) {
      int x = cx;
      int y = cy;
      Point* path = add_point(NULL, x, y);
      while (x != x1 || y != y1) {
        path = add_point(path, data[x][y].from_x, data[x][y].from_y);
        x = path->x;
        y = path->y;
      }

      free_point_list(closed);
      free_point_list(open);

      return path;
    }

    open = remove_point(open, cx, cy);
    closed = add_point(closed, cx, cy);

    Point* n = neighbors(level, cx, cy);
    for (Point* i = n; i != NULL; i = i->next) {
      /* This check should be done by neighbors() */
      /* if (!passable(level, i->x, i->y)) { */
      /*   continue; */
      /* } */

      const int tg = data[cx][cy].g + find_path_h(cx, cy, i->x, i->y);
      if (has_point(closed, i->x, i->y) && tg > data[i->x][i->y].g) {
        continue;
      }

      if (!has_point(open, i->x, i->y) || tg < data[i->x][i->y].g) {
        data[i->x][i->y].from_x = cx;
        data[i->x][i->y].from_y = cy;
        data[i->x][i->y].g = tg;
        data[i->x][i->y].f = tg + find_path_h(i->x, i->y, x2, y2);
        if (!has_point(open, i->x, i->y)) {
          open = add_point(open, i->x, i->y);
        }
      }
    }
  }

  free_point_list(open);
  free_point_list(closed);

  return NULL;
}


Point* find_path (MarkList* mark_list, const Level* level, int x1, int y1, int x2, int y2) {
  Point* path = NULL;
  //dfs(level, &path, x1, y1, x2, y2);
  path = a_star(level, x1, y1, x2, y2);
  for (Point* i = path; i != NULL; i = i->next) {
    mark(mark_list, MARK_ACTOR_PATH, i->x, i->y);
  }
  return path;
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
    //bresenham(x1, y1, x2, y2, mark_actor_sight_callback, NULL, mark_list);
    return true;
  }
  else {
    return false;
  }
}


int angle_dir (int angle) {
  if (angle < -180) {
    angle += 360;
  }
  else if (angle > 180) {
    angle -= 360;
  }
  return angle;
}


int angle_diff (int a, int b) {
  return angle_dir(b - a);
}


int angle_vector_diff (int angle, int dx, int dy) {
  int angle_2 = atan2(-dy, dx) / M_PI * 180.0;
  if (angle_2 < 0) {
    angle_2 += 360;
  }
  return angle_diff(angle, angle_2);
}


bool seek_target (MarkList* mark_list, const Level* level, Actor* actor, int x, int y, int min_d) {
  const int ax = actor->x;
  const int ay = actor->y;

  bool found = true;
  Point* path = find_path(mark_list, level, tc(ax), tc(ay), tc(x), tc(y));
  if (path != NULL) {
    Point* target = path;
    if (target->next != NULL) {
      target = target->next;
    }

    const int nx = pc(target->x);
    const int ny = pc(target->y);

    int diff = angle_vector_diff(actor->angle, nx - ax, ny - ay);
    const int dist = d(ax, ay, x, y);
    const int close = dist < min_d + ACTOR_STEP;

    if (abs(diff) > 30) {
      turn(actor, sign(diff) * 2 * ACTOR_TURN);
      found = false;
    }
    else if (abs(diff) > 10) {
      turn(actor, sign(diff) * ACTOR_TURN);
      found = false;
    }
    if (abs(diff) < 90 && !close) {
      move(actor, ACTOR_STEP); 
      found = false;
    }
  }
  free_point_list(path);
  return found;
}


bool should_give_up (int frame, const Actor* actor) {
  return actor->give_up_at >= 0 && actor->give_up_at <= frame;
}


void return_to_base (Actor* actor) {
  actor->tx = actor->base_x;
  actor->ty = actor->base_y;
  actor->t_angle = actor->base_angle;
  actor->give_up_at = -1;
}


bool has_target (const Actor* actor) {
  return actor->tx != -1 && actor->ty != -1;
}


bool is_chasing (const Actor* actor) {
  return has_target(actor) && (actor->tx != actor->base_x || actor->ty != actor->base_y);
}


void move_actors (int frame, MarkList* mark_list, const ActorList* actor_list, const Level* level, const Actor* player) {
  const int px = player->x;
  const int py = player->y;

  for (int i = 0; i < actor_list->len; ++i) {
    Actor* const actor = &actor_list->actors[i];

    const int ax = actor->x;
    const int ay = actor->y;

    // Vector from actor to player
    const double dx = px - ax;
    const double dy = py - ay;

    int diff = angle_vector_diff(actor->angle, dx, dy);
    const bool los = abs(diff) < ACTOR_FOV / 2.0 && line_of_sight(mark_list, level, tc(ax), tc(ay), tc(px), tc(py));
    if (los) {
      mark(mark_list, MARK_ACTOR_SPOTTED, ax, ay - actor->radius);
      actor->tx = px;
      actor->ty = py;
      actor->give_up_at = -1;
    }

    if (actor->give_up_at != -1) {
      mark(mark_list, MARK_ACTOR_LOST, ax, ay - actor->radius);
    }

    if (should_give_up(frame, actor)) {
      return_to_base(actor);
    }

    if (actor->tx >= 0 && actor->ty >= 0) {
      const bool found = seek_target(mark_list, level,
          actor, actor->tx, actor->ty, actor->radius + (los ? player->radius : 0));
      if (found) {
        if (actor->tx != actor->base_x && actor->ty != actor->base_y) {
          actor->give_up_at = frame + 60;
        }
        actor->tx = -1;
        actor->ty = -1;
      }
    }

    if (actor->tx == -1 && actor->ty == -1 && actor->t_angle != -1) {
      const int ad = angle_diff(actor->angle, actor->t_angle);
      if (ad == 0) {
        actor->t_angle = -1;
      }
      turn(actor, clamp(ad, -ACTOR_TURN, ACTOR_TURN));
    }

    if (!los && is_chasing(actor)) {
      mark(mark_list, MARK_ACTOR_CHASING, ax, ay - actor->radius);
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

  move_actors(frame, mark_list, &actors, &level, player);

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
    moved = collide_level_actor(mark_list, level, player);
    for (int i = 0; i < actors.len; ++i) {
      if (collide_level_actor(mark_list, level, &actors.actors[i])) {
        moved = true;
      }
    }

    for (int i = 0; i < actors.len; ++i) {
      if (collide_actor_actor(player, &actors.actors[i])) {
        moved = true;
      }
    }

    for (int i = 0; i < actors.len; ++i) {
      for (int j = i + 1; j < actors.len; ++j) {
        if (collide_actor_actor(&actors.actors[i], &actors.actors[j])) {
          moved = true;
        }
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

  glTexCoord2d(1.0, 1.0);
  glVertex2d(.5, .5);

  glTexCoord2d(1.0, 0.0);
  glVertex2d(.5, -.5);

  glTexCoord2d(0.0, 0.0);
  glVertex2d(-.5, -.5);

  glTexCoord2d(0.0, 1.0);
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
  //GLuint tex_actor_sight = load_texture("actor_sight.png", TEXTURE_SIZE);
  GLuint tex_actor_path = load_texture("actor_path.png", TEXTURE_SIZE);
  GLuint tex_actor_spotted = load_texture("actor_spotted.png", TEXTURE_SIZE);
  GLuint tex_actor_chasing = load_texture("actor_chasing.png", TEXTURE_SIZE);
  GLuint tex_actor_lost = load_texture("actor_lost.png", TEXTURE_SIZE);

  GLuint tile_textures[] = {
    load_texture("floor.png", TEXTURE_SIZE),
    load_texture("wall.png", TEXTURE_SIZE),
    load_texture("door.png", TEXTURE_SIZE),
    load_texture("door-open.png", TEXTURE_SIZE)
  };

  int frame = 0;

  const int ACTOR_R = 1500;

  Actor player;
  init_actor(&player, pc(1), pc(1), 0, ACTOR_R);

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

  init_actor(&actors[0], pc(15), pc(10), 180, ACTOR_R);
  ++actor_list.len;

  init_actor(&actors[1], pc(16), pc(5), 90, ACTOR_R);
  ++actor_list.len;

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

    const int sight_radius = 10;
    Sight* sight = compute_sight(level, player, sight_radius);

    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    draw_level(level, tile_textures, tex_darkness, sight);

    for (int i = 0; i < actor_list.len; ++i) {
      const Actor* const actor = &actor_list.actors[i];
      /* if (sight_get(sight, tc(actor->x), tc(actor->y))) { */
        draw_texture(tex_actor, actor->x, actor->y, actor->angle, true);
      /* } */
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
        case MARK_ACTOR_PATH:
          draw_tile(tex_actor_path, x, y);
          break;

        case MARK_ACTOR_SPOTTED:
          //if (sight_get(sight, tc(x), tc(y))) {
            draw_texture(tex_actor_spotted, x, y - 0.6 * TILE_SIZE, 0, true);
          //};
          break;

        /* case MARK_ACTOR_CHASING: */
        /*   //if (sight_get(sight, tc(x), tc(y))) { */
        /*     draw_texture(tex_actor_chasing, x, y - 0.6 * TILE_SIZE, 0, true); */
        /*   //}; */
        /*   break; */

        case MARK_ACTOR_LOST:
          //if (sight_get(sight, tc(x), tc(y))) {
            draw_texture(tex_actor_lost, x, y - 0.6 * TILE_SIZE, 0, true);
          //};
          break;
      }
    }

    free_sight(sight);

    glDisable(GL_TEXTURE_2D);
    glEnd();

    SDL_GL_SwapBuffers();
  }

  free_level(level);

  SDL_Quit();
}

