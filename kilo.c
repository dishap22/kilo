#include <termios.h> // struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH
#include <unistd.h> // read(), STDIN_FILENO

void enableRawMode() {
  struct termios raw;

  tcgetattr(STDIN_FILENO, &raw);

  raw.c_lflag &= ~(ECHO); //prints each key typed to terminal in canonical mode, turned off here for raw mode (also used while typing pass in terminal)

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
  enableRawMode();

  char c;
  while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q'); //reads input character by character till q is pressed
  return 0;
}
