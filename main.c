
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>

#define GL_GLEXT_PROTOTYPES
#include <SDL/SDL.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <png.h>


typedef struct {
  int x;
  int y;
  int angle;
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
  unsigned int width;
  unsigned int height;
  unsigned int* tiles;
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

  log_d("Level dimensions: %d x %d", width, height);

  level->width = width;
  level->height = height;
  level->tiles = NULL;

  return true;
}


void free_level (Level level) {
  free(level.tiles);
}


void game (int frame, const Level level, Actor* const player, const bool actions[]) {

  if (actions[LEFT] | actions[RIGHT]) {
    const int turn = actions[LEFT] ? 6 : -6;
    player->angle += turn;
  }
  if (actions[FORWARD] || actions[BACKWARD]) {
    double angle_rad = player->angle / 180.0 * M_PI;

    const double step = actions[FORWARD] ? 5 : -3;

    player->x += step * cos(angle_rad);
    player->y -= step * sin(angle_rad);
  }
}


void print_error (void) {
  fprintf(stderr, "Error: %s\n", gluErrorString(glGetError()));
}


GLuint load_texture (const char* filename, const int tile_size) {
  FILE* file = fopen(filename, "rb");
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
  png_byte data[tile_size * row_bytes];
  for (int row = 0; row < tile_size; ++row) {
    png_read_row(png_ptr, data + row * row_bytes, NULL);
  }

  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tile_size, tile_size, 0,
      GL_RGBA, GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  //print_error();
  png_destroy_read_struct(&png_ptr, &info_ptr, &end_info_ptr);

  fclose(file);

  return texture;
}


void draw_texture (const int texture_size, const int texture, const int x, const int y, const int angle, const bool center) {
  glBindTexture(GL_TEXTURE_2D, texture);

  glLoadIdentity();
  glTranslated(x, y, 0);
  glRotated(angle, 0.0, 0.0, -1.0);

  glScaled(32, 32, 1);

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


int main (int argc, char *argv[]) {
  SDL_Init(SDL_INIT_VIDEO);

  SDL_Event event;
  bool running = true;

  const int win_w = 800;
  const int win_h = 600;

  const int texture_size = 32;

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
  GLuint tex_tile = load_texture("tile.png", texture_size);
  GLuint tex_actor = load_texture("char.png", texture_size);

  int frame = 0;

  Actor player = { .x = 300, .y = 200, .angle = 0 };

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

    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    game(frame++, level, &player, actions);
    draw_texture(texture_size, tex_tile, 0, 0, 0, false);
    draw_texture(texture_size, tex_actor, player.x, player.y, player.angle, true);

    SDL_GL_SwapBuffers();
  }

  SDL_Quit();
}

