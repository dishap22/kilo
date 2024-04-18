#include <stdlib.h> // atexit()
#include <termios.h> // struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH
#include <unistd.h> // read(), STDIN_FILENO

struct termios orig_termios;

void disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disableRawMode);

  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO); //prints each key typed to terminal in canonical mode, turned off here for raw mode (also used while typing pass in terminal)

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
  enableRawMode();

  char c;
  while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q'); //reads input character by character till q is pressed
  return 0;
}
