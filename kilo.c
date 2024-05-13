#include <stdlib.h> // atexit()
#include <termios.h> // struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH, ICANON
#include <unistd.h> // read(), STDIN_FILENO

struct termios orig_termios; // store original attributes

void disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); // reset to original settings
}

void enableRawMode() {
  tcgetattr(STDIN_FILENO, &orig_termios); // get original settings
  atexit(disableRawMode); 
 
  struct termios raw = orig_termios; // copy to current settings
  raw.c_lflag &= ~(ECHO | ICANON); 
  // ECHO flag prints each key typed to terminal turned off here for raw mode (also used while typing pass in terminal)
  // flag stored as 00000000000000000000000000001000 in binary
  // ~ sets value to 11111111111111111111111111110111
  // & sets value to retain original values in each position except 4th bit becoems 0 (turn off ECHO)
  // ICANON flag reads char by char instead of line by line
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); // apply changes
}

int main() {
  enableRawMode();

  char c;
  while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q'); //reads input character by character till q is pressed
  return 0;
}
