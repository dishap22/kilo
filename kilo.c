/*** includes ***/

#include <ctype.h> // iscntrl()
#include <errno.h> // errno, EAGAIN
#include <stdio.h> // printf(), perror()
#include <stdlib.h> // atexit(), exit()
#include <sys/ioctl.h> // ioctl(), TIOCGWINSZ, struct winsize
#include <termios.h> // struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH, ICANON, ISIG, IXON, IEXTEN, ICRNL, OPOST, BRKINT, INPCK, ISTRIP, CS8, VMIN, VTIME
#include <unistd.h> // read(), STDIN_FILENO, write(), STDOUT_FILENO

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct editorConfig 
{
  int screenrows;
  int screencols;
  struct termios orig_termios; // store original attributes
};

struct editorConfig E;

/*** terminal ***/

void die(const char *s)
{
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() 
{
  // reset to original settings
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr"); 
}

void enableRawMode() 
{
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); // get original settings
  atexit(disableRawMode); 
 
  struct termios raw = E.orig_termios; // copy to current settings
  raw.c_iflag &= ~(BRKINT| ICRNL | INPCK | ISTRIP | IXON);
  // Ctrl+S stops data from being transmitted to terminal, produces XOFF control character
  // Ctrl+Q resumes data transmission, produces XON control character
  // IXON turns these off, is an input flag, hence the I. ISIG, ICANON, IEXTEN are exceptions, not input flags but start with I
  // ICRNL leads to any carriage return (Ctrl+M) being convered into newline characters. Disabling this. CR = Carriage Return, NL = Newline.
  // When BRKINT is turned on, a break condition will cause a SIGINT singal to be sent to the program (like Ctrl+C)
  // INPCK enables parity checking (irrelevant for modern terminal emulators, kept for convention)
  // ISTRIP causes 8th bit of input byte to be stripped (likely already off, toggled here for convention)

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

char editorReadKey()
{
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
  {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

int getCursorPosition(int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}

int getWindowSize(int *rows, int *cols)
{
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) 
  {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  }
  else
  {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** output ***/

void editorDrawRows()
{
  int y;
  for (y = 0; y < E.screenrows; y++)
  {
    write(STDOUT_FILENO, "~", 1);
    if (y < E.screenrows - 1) {
      write(STDOUT_FILENO, "\r\n", 2);
    }
  }
}

void editorRefreshScreen()
{
  write(STDOUT_FILENO, "\x1b[2J", 4); 
  // 4 means writing 4 bytes out to terminal
  // byte 1: \x1b (escape character, or 27 in decimal)
  // other 3 bytes: [2J
  // Escape sequences always begin as \x1b[
  // J command clears the screen
  // 2 is an argument to the escape sequence, comes before the actual command.
  // <esc>[1J = clear screen up to where cursor is
  // <esc>[0J = clear screen from cursor to end - this is also the default argument so <es>[J has the same effect
  // <esc>[2J = clear the entire screen
  write(STDOUT_FILENO, "\x1b[H", 3);
  // position cursor 
  // by default, row col args are 1 so this has same effect as <esc>[1;1H
  // can modify to move cursor to other areas

  editorDrawRows();
  write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input ***/

void editorProcessKeypress()
{
  char c = editorReadKey();

  switch(c)
  {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
  }
}

/*** init ***/

void initEditor() 
{
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() 
{
  enableRawMode();
  initEditor();

  while(1) //reads input character by character till q is pressed
  {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}