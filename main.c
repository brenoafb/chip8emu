#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#define STACK_SIZE 64
#define WIDTH 64
#define HEIGHT 32
#define SCALE 16
#define TICK (1000000 / 60) // 60hz

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '*' : ' '), \
  (byte & 0x40 ? '*' : ' '), \
  (byte & 0x20 ? '*' : ' '), \
  (byte & 0x10 ? '*' : ' '), \
  (byte & 0x08 ? '*' : ' '), \
  (byte & 0x04 ? '*' : ' '), \
  (byte & 0x02 ? '*' : ' '), \
  (byte & 0x01 ? '*' : ' ') 

SDL_Renderer *renderer;

uint16_t v[0xf + 1];
uint16_t I;
uint16_t pc;
uint16_t delay;
uint16_t sound;
uint8_t  mem[0x10000];
uint16_t stack[STACK_SIZE];
uint16_t sp;
uint8_t screen[HEIGHT][WIDTH];

void cycle();

void clear_screen();

int setup_screen(SDL_Window **window, SDL_Renderer **renderer, int width, int height);
void draw_byte(uint8_t b, int x, int y);
void draw_pixel(int x, int y, uint8_t p);
void draw_screen();

int main(int argc, char *argv[]) {
  srand(time(NULL));
  if (argc != 2) {
    return 1;
  }

  SDL_Window *window = NULL;
  SDL_Event event;
  renderer = NULL;

  if (!setup_screen(&window, &renderer, SCALE * WIDTH, SCALE * HEIGHT)) {
    return 0;
  }
  
  SDL_RenderPresent(renderer);
  FILE *fp;

  struct stat info;
  stat(argv[1], &info);

  printf("Reading %lld byte file into memory\n", info.st_size);

  fp = fopen(argv[1], "rb");

  int len = 0;

  size_t blocks_read = fread(mem + 0x200, info.st_size, 1, fp);
  fclose(fp);  

  if (blocks_read != 1) {
    printf("Error reading file\n");
    return 2;
  }

  clear_screen();
  draw_screen();
  draw_screen();  
  
  I = 0;
  pc = 0x200;
  struct timeval start, end;

  gettimeofday(&start, NULL);
  while (1) {
    gettimeofday(&end, NULL);
    if (end.tv_usec - start.tv_usec >= TICK) {
      delay -= 1;
      sound -= 1; // TODO play sound if register reaches 0
      gettimeofday(&start, NULL);      
    }
    cycle();
    draw_screen();
    pc += 2;
    if (SDL_PollEvent(&event) && event.type == SDL_QUIT) {
      break;
    }
  }

  SDL_DestroyWindow(window);
  SDL_Quit();  

  return 0;
}

void cycle() {
  uint16_t op = mem[pc] << 8 | mem[pc+1];
  // printf("%04x\t", op);
  uint16_t vx  = (op & 0x0f00) >> 8;
  uint16_t vy  = (op & 0x00f0) >> 4;
  uint16_t nnn = (op & 0x0fff);  
  uint16_t nn  = (op & 0x00ff);
  uint16_t n   = (op & 0x000f);

  if ((op & 0xf000) == 0) {
    // 0 instructions
    if ((op & 0x0f00) == 0) {
      switch (op) {
      case 0x00e0:
	clear_screen();
	break;
      case 0x00ee:
	sp--;
	pc = stack[sp];
	break;
      }
    } else {
      // printf("call (bin) 0x%x\n", nnn);
      // TODO (probably not needing this)
    }
  }

  if ((op & 0xf000) == 0x1000) {
    // printf("goto 0x%x\n", nnn);
    pc = nnn;
  }

  if ((op & 0xf000) == 0x2000) {
    //    printf("call (lang) 0x%x\n", nnn);
    stack[sp] = pc;
    sp++;
    pc = nnn-1;
  }

  if ((op & 0xf000) == 0x3000) {
    // printf("skip v%x == 0x%x\n", vx, nn);
    if (v[vx] == nn) { pc += 2; }
  }

  if ((op & 0xf000) == 0x4000) {
    // printf("skip v%x != 0x%x\n", vx, nn);
    if (v[vx] != nn) { pc += 2; }    
  }

  if ((op & 0xf000) == 0x5000) {
    // printf("skip v%x == v%x\n", vx, vy);
    if (v[vx] == v[vy]) { pc += 2; }        
  }


  if ((op & 0xf000) == 0x6000) {
    // printf("v%x = %x\n", vx, nn);
    v[vx] = n;
  }

  if ((op & 0xf000) == 0x7000) {
    // printf("v%x += %x\n", vx, nn);
    v[vx] += n;
  }

  uint16_t hobx = v[vx] & 0x80;
  uint16_t hoby = v[vy] & 0x80;  

  if ((op & 0xf000) == 0x8000) {
    switch (op & 0xf) {
    case 0:
      // printf("v%x = v%x\n", vx, vy);
      v[vx] = v[vy];
      break;
    case 1:
      // printf("v%x = v%x | v%x\n", vx, vx, vy);
      v[vx] |= v[vy];
      break;
    case 2:
      // printf("v%x = v%x & v%x\n", vx, vx, vy);
      v[vx] &= v[vy];
      break;
    case 3:
      // printf("v%x = v%x ^ v%x\n", vx, vx, vy);
      v[vx] ^= v[vy];
      break;
    case 4:
      // printf("v%x += v%x (with carry)\n", vx, vy);
      v[vx] += v[vy];
      if (hobx != hoby) {
	// no way to overflow if values have different signs
	break;
      }
      if ((v[vx] & 0x80) != hobx) {
	// vx after addition has different sign than vx before and vy.
	// means that we overflowed
	v[0xf] = 1;
      }
      break;
    case 5:
      // printf("v%x -= v%x (with carry)\n", vx, vy);
      v[vx] -= v[vy];
      if (hobx != hoby) {
	// no way to overflow if values have different signs
	break;
      }
      if ((v[vx] & 0x80) == hobx) {
	// vx after subtraction has the same sign as vx before and vy.
	// means that we did not underflow
	v[0xe] = 1;
      }
      break;
    case 6:
      // printf("v%x = v%x >> 1\n", vx, vy);
      v[vx] = v[vy] >> 1;
      break;      
    case 7:
      // printf("v%x = v%x - v%x (with carry)\n", vx, vy, vx);
      v[vx] = v[vy] - v[vy];
      if (hobx != hoby) {
	// no way to overflow if values have different signs
	break;
      }
      if ((v[vx] & 0x80) != hobx) {
	// vx after addition has different sign than vx before and vy.
	// means that we overflowed
	v[0xf] = 1;
      }
      break;
    case 0xe:
      // printf("v%x = v%x << 1\n", vx, vy);
      v[vx] = v[vy] << 1;
      break;
    default:
      // printf("illegal\n");
      break;
    }
  }

  if ((op & 0xf000) == 0x9000) {
    // printf("skip if v%x != v%x\n", vx, vy);
    if (v[vx] != v[vy]) { pc += 2; }
  }

  if ((op & 0xf000) == 0xa000) {
    // printf("I = 0x%x\n", nnn);
    I = nnn;
  }

  if ((op & 0xf000) == 0xb000) {
    // printf("jmp 0x%x + v0\n", nnn);
    if (v[vx] != v[vy]) { pc += 2; };
  }
  
  if ((op & 0xf000) == 0xc000) {
    // printf("v%x = rand(%x)\n", vx, nn);
    uint16_t r = rand();
    v[vx] = r | nn;
  }

  if ((op & 0xf000) == 0xd000) {
    for (int i = 0; i < n; i++) {
      uint8_t byte = mem[I + i];
      int x = vx;
      int y = vy + i;
      draw_byte(byte, x, y);
    }
  }

  if ((op & 0xf000) == 0xe000) {
    switch (op & 0x00ff) {
    case 0x9e: // TODO
      // printf("skip if v%x pressed\n", vx);
      break;
    case 0xa1:
      // printf("skip if v%x not pressed\n", vx);
      // TODO read keypress
      pc += 2;
      break;
    default:
      // printf("illegal\n");
      break;
    }
  }

  if ((op & 0xf000) == 0xf000) {
    switch (op & 0x00ff) {
    case 0x07:
      // printf("v%x = delay\n", vx);
      v[vx] = delay;
      break;
    case 0x0a: // TODO
      // printf("v%x = key\n", vx);
      break;
    case 0x15:
      // printf("delay = v%x\n", vx);
      delay = v[vx];
      break;
    case 0x18:
      // printf("sound = v%x\n", vx);
      sound = v[vx];
      break;
    case 0x1e:
      // printf("i += v%x\n", vx);
      I += v[vx];
      break;
    case 0x29: // TODO
      // printf("i = sprite v%x\n", vx);
      break;
    case 0x33: // TODO
      // printf("i, i+1, i+2 = bcd(v%x)\n", vx);
      break;
    case 0x55:
      // printf("i... = v0..v%x\n", vx);
      for (int i = 0; i < vx; i++) {
	mem[I + i] = v[i] & 0xff;
	mem[I + i + 1] = (v[i] & 0xff00) >> 8;
      }
      break;
    case 0x65:
      // printf("v0..v%x = i..\n", vx);
      for (int i = 0; i < vx; i++) {
	v[i] = mem[I + i];
      }
      break;
    }
  }  
}

int setup_screen(SDL_Window **window, SDL_Renderer **renderer, int width, int height) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL could not be initialized. SDL_Error: %s\n", SDL_GetError());
    return 0;
  }

  SDL_CreateWindowAndRenderer(width, height, 0, window, renderer);


  if (window == NULL) {
    printf("Window could not be created. SDL_Error: %s\n", SDL_GetError());
    return 0;
  }

  if (renderer == NULL) {
    printf("Window could not be created. SDL_Error: %s\n", SDL_GetError());
    return 0;
  }

  SDL_RenderClear(*renderer);

  return 1;
}

void draw_byte(uint8_t b, int x, int y) {
  for (int i = 0; i < 8; i++) {
    uint8_t mask = 1 << i;
    uint8_t bit = (b & mask) >> i;
    screen[y][x+i] = (screen[y][x+i] & 1) ^ bit;
  }
}

void clear_screen() {
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      screen[y][x] = 0;
    }
  }
}

void draw_screen() {
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      uint8_t p = screen[y][x];
      draw_pixel(x, y, p);
      /* if (p) { */
      /* 	printf("*"); */
      /* } else { */
      /* 	printf(" "); */
      /* } */
    }
    /* printf("\n"); */
  }
  /* printf("\n\n"); */
  SDL_RenderPresent(renderer);
}

void draw_pixel(int x, int y, uint8_t p) {
  SDL_SetRenderDrawColor(renderer, 255 * p, 255 * p, 255 * p, 255);      

  for (int i = 0; i < SCALE; i++) {
    for (int j = 0; j < SCALE; j++) {
      SDL_RenderDrawPoint(renderer, x * SCALE + i, y * SCALE + j);
    }
  }
}
