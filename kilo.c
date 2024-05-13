/*** includes ***/

#include <ctype.h> // iscntrl()
#include <errno.h> // errno, EAGAIN
#include <stdio.h> // printf(), perror()
#include <stdlib.h> // atexit(), exit()
#include <termios.h> // struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH, ICANON, ISIG, IXON, IEXTEN, ICRNL, OPOST, BRKINT, INPCK, ISTRIP, CS8, VMIN, VTIME
#include <unistd.h> // read(), STDIN_FILENO

/*** data ***/

struct termios orig_termios; // store original attributes

/*** terminal ***/

void die(const char *s)
{
  perror(s);
  exit(1);
}

void disableRawMode() 
{
  // reset to original settings
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr"); 
}

void enableRawMode() 
{
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr"); // get original settings
  atexit(disableRawMode); 
 
  struct termios raw = orig_termios; // copy to current settings
  raw.c_iflag &= ~(BRKINT| ICRNL | INPCK | ISTRIP | IXON);
  // Ctrl+S stops data from being transmitted to terminal, produces XOFF control character
  // Ctrl+Q resumes data transmission, produces XON control character
  // IXON turns these off, is an input flag, hence the I. ISIG, ICANON, IEXTEN are exceptions, not input flags but start with I
  // ICRNL leads to any carriage return (Ctrl+M) being convered into newline characters. Disabling this. CR = Carriage Return, NL = Newline.
  // When BRKINT is turned on, a break condition will cause a SIGINT singal to be sent to the program (like Ctrl+C)
  // INPCK enables parity checking (irrelevant for modern terminal emulators, kept for convention)
  // ISTRIP causes 8th bit of input byte to be stripped (likely already off, toggled here for convetion)

  raw.c_oflag &= (OPOST);
  // Default terminal converts "\n" to "\r\n". Disabling this output processing.
  
  raw.c_cflag |= (CS8);
  // CS8 not a flag, bit mask with multiple bits. Set using OR. Sets character size to 8 bits per byte.

  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 
  // ECHO flag prints each key typed to terminal turned off here for raw mode (also used while typing pass in terminal)
  // flag stored as 00000000000000000000000000001000 in binary
  // ~ sets value to 11111111111111111111111111110111
  // & sets value to retain original values in each position except 4th bit becoems 0 (turn off ECHO)
  // ICANON flag reads char by char instead of line by line
  // ISIG turns off SIGINT signal sent by Ctrl+C and SIGTSTP signal sent by Ctrl+Z
  // IEXTEN disables Ctrl+V setting that waits for another character to be typed and then sends this character
  
  raw.c_cc[VMIN] = 0; // Sets min bytes of input needed before read() can return, set to 0 so read() returns as soon as input provided
  raw.c_cc[VTIME] = 1; // Sets maximum time to wait before read() returns. Is specified in tenths of a second. Here set to 1/10 seconds or  100 milliseconds
  // If read times out, 0 will be returned. (Typically returns number of bytes read)

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // apply changes
}

/*** init ***/

int main() 
{
  enableRawMode();

  while(1) //reads input character by character till q is pressed
  {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
    if (iscntrl(c)) // checks if a character is a non-printable control character
    {
      printf("%d\r\n", c);
    }
    else
    {
      printf("%d ('%c')\r\n", c, c);
    }
    if (c == 'q') break;
  }
  return 0;
}