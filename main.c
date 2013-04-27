
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
static const int TILE_SIZE = 32;


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
  NOP,
} Action;
const int ACTION_COUNT = NOP + 1;


typedef struct {
  int width;
  int height;
  int* tiles;
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

  level->tiles = malloc(width * height * sizeof(int));
  if (!level->tiles) {
    log_e("Memory allocation failed: %s", strerror(errno));
    return false;
  }

  int i = 0;
  while (!feof(file)) {
    char c = fgetc(file);
    if (c == '\n') {
      continue;
    }
    level->tiles[i++] = c == '#' ? 1 : 0;
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


typedef enum {
  MARK_TILE_PLAYER_ON,
  MARK_DOT,
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


bool passable (const int tile) {
  return tile == 0;
}


int tile_at (const Level level, const int x, const int y) {
  assert(x >= 0);
  assert(x < level.width);
  assert(y >= 0);
  assert(y < level.height);
  return level.tiles[y * level.width + x];
}


int sign (const int number) {
  return (number > 0) - (number < 0);
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

  const double push_len = r + dp - tp_len;

  Vector push = { dirx * push_len, diry * push_len };
  return push;
}


void game (int frame, const Level level, MarkList* const mark_list, Actor* const player, const bool actions[]) {
  if (actions[LEFT] | actions[RIGHT]) {
    turn(player, actions[LEFT] ? 6 : -6);
  }
  if (actions[FORWARD] || actions[BACKWARD]) {
    double angle_rad = player->angle / 180.0 * M_PI;

    const double step = actions[FORWARD] ? 5 : -5;

    player->x += step * cos(angle_rad);
    player->y -= step * sin(angle_rad);
  }

  const int left = tc(player->x - player->radius);
  const int right = tc(player->x + player->radius);
  const int top = tc(player->y - player->radius);
  const int bottom = tc(player->y + player->radius);

  const int tr = TILE_SIZE / 2;

  for (int y = top; y <= bottom; ++y) {
    for (int x = left; x <= right; ++x) {
      mark(mark_list, MARK_TILE_PLAYER_ON, x, y);
      if (!passable(tile_at(level, x, y))) {
        const double tx = pc(x);
        const double ty = pc(y);

        const double tpx = player->x - tx;
        const double tpy = player->y - ty;

        const double tp_len = length(tpx, tpy);

        const double dirx = tpx / tp_len;
        const double diry = tpy / tp_len;

        Vector push = { 0.0, 0.0 };

        const int voronoi = find_voronoi(x, y, player->x, player->y);
        switch (voronoi) {
          case 2:
            push.y = pc_corner(y) - player->y - player->radius;
            break;

          case 4:
            push.x = pc_corner(x) - player->x - player->radius;
            break;

          case 6:
            push.x = pc_corner(x) + TILE_SIZE - player->x + player->radius;
            break;

          case 8:
            push.y = pc_corner(y) + TILE_SIZE - player->y + player->radius;
            break;

          case 1:
            push = find_push(dirx, diry, -tr, -tr, player->radius, tp_len);
            break;

          case 3:
            push = find_push(dirx, diry, tr, -tr, player->radius, tp_len);
            break;

          case 7:
            push = find_push(dirx, diry, -tr, tr, player->radius, tp_len);
            break;

          case 9:
            push = find_push(dirx, diry, tr, tr, player->radius, tp_len);
            break;
        }
        
        player->x += round(push.x);
        player->y += round(push.y);
      }
    }
  }
}


void print_error (void) {
  fprintf(stderr, "Error: %s\n", gluErrorString(glGetError()));
}


GLint load_texture (const char* filename, const int texture_size) {
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

  GLint texture;
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
  glTranslated(x, y, 0);
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


void draw_level (const Level level, const GLint tile_textures[]) {
  int i = 0;
  for (int y = 0; y < level.height; ++y) {
    for (int x = 0; x < level.width; ++x) {
      const int tile = level.tiles[i];
      const GLint texture = tile_textures[tile];
      draw_tile(texture, x, y); 
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
  glEnable(GL_TEXTURE_2D);

  GLint tex_floor = load_texture("floor.png", TEXTURE_SIZE);
  GLint tex_wall = load_texture("wall.png", TEXTURE_SIZE);
  GLint tex_actor = load_texture("actor.png", TEXTURE_SIZE);
  GLint tex_mark = load_texture("mark.png", TEXTURE_SIZE);
  GLint tex_dot = load_texture("dot.png", TEXTURE_SIZE);

  GLint tile_textures[] = { tex_floor, tex_wall };

  int frame = 0;

  Actor player = { .radius = 16, .x = pc(1), .y = pc(1), .angle = 0 };

  typedef struct {
    Action action;
    SDLKey key;
  } Mapping;
  
  Mapping keymap[] = {
    { FORWARD  , SDLK_UP    }, 
    { BACKWARD , SDLK_DOWN  }, 
    { LEFT     , SDLK_LEFT  },
    { RIGHT    , SDLK_RIGHT }
  };
  const int MAPPING_COUNT = 4;

  bool actions[ACTION_COUNT];

  memset(actions, false, ACTION_COUNT * sizeof(bool));

  Uint32 target_ticks = 0;
  const Uint32 FRAME_TICKS = 1000 / 60;
  Level level;

  load_level("level.lev", &level);

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

    game(frame++, level, &mark_list, &player, actions);

    glClear(GL_COLOR_BUFFER_BIT);

    draw_level(level, tile_textures);

    for (int i = 0; i < mark_list.len; ++i) {
      switch (marks[i].reason) {
        case MARK_TILE_PLAYER_ON:
          draw_tile(tex_mark, marks[i].x, marks[i].y);
          break;
        case MARK_DOT:
          draw_texture(tex_dot, marks[i].x, marks[i].y, 0, true);
          break;
      }
    }

    draw_texture(tex_actor, player.x, player.y, player.angle, true);

    SDL_GL_SwapBuffers();
  }

  SDL_Quit();
}


