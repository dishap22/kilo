/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h> // iscntrl()
#include <errno.h> // errno, EAGAIN
#include <fcntl.h> // open(), O_RDWR, O_CREAT
#include <stdio.h> // printf(), perror(), snprintf(), FILE, fopen(), getline(), vsnprintf()
#include <stdarg.h> // va_list, va_start(), va_end()
#include <stdlib.h> // atexit(), exit(), realloc(), free(), malloc()
#include <string.h> // memcpy(), strlen(), strdup(), memmove(), strerror()
#include <sys/ioctl.h> // ioctl(), TIOCGWINSZ, struct winsize
#include <sys/types.h> // ssize_t
#include <termios.h> // struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH, ICANON, ISIG, IXON, IEXTEN, ICRNL, OPOST, BRKINT, INPCK, ISTRIP, CS8, VMIN, VTIME
#include <time.h> // time_t, time()
#include <unistd.h> // read(), STDIN_FILENO, write(), STDOUT_FILENO, ftruncate(), close()

/*** defines ***/
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8 // configurable tab length
#define KILO_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey 
{
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** data ***/
typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow; // data type to store row of text in editor

struct editorConfig 
{
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios; // store original attributes
};

struct editorConfig E;

/*** prototypes ***/
void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt);

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

int editorReadKey()
{
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
  {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  if (c == '\x1b') 
  {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b'; // if time out, assume esc character

    if (seq[0] == '[') 
    {
      if (seq[1] >= '0' && seq[1] <= '9')
      {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~')
        {
          switch (seq[1])
          {
            case '1': return HOME_KEY; 
            case '3': return DEL_KEY; // delete key sends <esc>[3~
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      }
      else
      {
        switch (seq[1]) 
        {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT; // mapping arrow keys to wasd keys
          case 'H': return HOME_KEY; // Home key could be sent as <esc>[1~, <esc>[7~, <esc>[H, or <esc>OH
          case 'F': return END_KEY; // End key could be sent as <esc>[4~, <esc>[8~, <esc>[F, or <esc>OF
        }
      }
    }
    else if (seq[0] == 'O') 
    {
      switch (seq[1]) 
      {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b';
  }
  else
  {
    return c;
  }
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

/*** row operations ***/
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row -> chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }
  return rx;
}

void editorUpdateRow(erow *row) {
  int tabs = 0; 
  int j;
  for (j = 0; j < row -> size; j++)
    if (row -> chars[j] == '\t') tabs++;

  free(row -> render);
  row -> render = malloc(row -> size + tabs * (KILO_TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row -> size; j++)
  {
    if (row -> chars[j] == '\t')
    {
      row -> render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row -> render[idx++] = ' ';
    } else {
      row -> render[idx++] = row -> chars[j];
    }
  }
  row -> render[idx] = '\0';
  row -> rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len)
{
  if (at < 0 || at > E.numrows) return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row -> render);
  free(row -> chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row -> size) at = row -> size;
  row -> chars = realloc(row -> chars, row -> size + 2);
  memmove(&row -> chars[at + 1], &row -> chars[at], row -> size - at + 1);
  row -> size++;
  row -> chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row -> chars = realloc(row -> chars, row -> size + len + 1);
  memcpy(&row -> chars[row -> size], s, len);
  row -> size += len;
  row -> chars[row -> size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row -> size) return;
  memmove(&row -> chars[at], &row -> chars[at + 1], row -> size - at);
  row -> size--;
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/
void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewline() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row -> chars[E.cx], row -> size - E.cx);
    row = &E.row[E.cy];
    row -> size = E.cx;
    row -> chars[row -> size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar() {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row -> chars, row -> size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/*** file i/o ***/
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars,  E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  linelen = getline(&line, &linecap, fp);

  while ((linelen = getline(&line, &linecap, fp)) != -1)
  {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s (ESC to cancel)");
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
  }

  int len; 
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  // O_RDWR opens for reating and writing
  // O_CREAT creates file if not exists
  // 0644 is the standard permissions typically used for text files, gives the owner permission to read/write the file, everyone else can only read
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
  // set file's size to specified length. if larger, cut off extra data at end. if shorter, add 0 btyes to make it that length
  // typically, file overwritten by passing O_TRUNC to open(): truncates file completely, making it empty before new data written
  // made safer by manually calling ftruncate() as all data would have been gone if we used open() and write() failed, not as in this case
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
  char *new = realloc(ab -> b, ab -> len + len);

  if (new == NULL) return;
  memcpy(&new[ab -> len], s, len);
  ab -> b = new;
  ab -> len += len;
}

void abFree(struct abuf *ab)
{
  free (ab -> b);
}

/*** output ***/
void editorScroll() {
  E.rx = E.cx;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff)
  {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editorDrawRows(struct abuf *ab)
{
  int y;
  for (y = 0; y < E.screenrows; y++)
  {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows)
    {
      if (E.numrows == 0 && y == E.screenrows / 3) // only show welcome message if no file
      {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding)
        {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      }
      else 
      {
        abAppend(ab, "~", 1);
      }
    } 
    else 
    {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }

    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4); // switch to inverted colors
  // 1: bold
  // 4: underscore
  // 5: blink
  // 7: inverted colors
  // alternatively, could use all, e.g. <esc>[1;4;5;7m
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3); // return to normal formatting
  // argument of 0 clears all attributes previously assigned
  // 0 is default argument, so can directly use <esc>[m
  abAppend(ab, "\r\n", 2); // add line for status bar and messages
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3); // clear message bar
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;

  // only display message if less than 5 seconds old
  if (msglen && time(NULL) - E.statusmsg_time < 5) 
    abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen()
{
  editorScroll();
  
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6); // hide cursor before printing
  // 4 means writing 4 bytes out to terminal
  // byte 1: \x1b (escape character, or 27 in decimal)
  // other 3 bytes: [2J
  // Escape sequences always begin as \x1b[
  // J command clears the screen
  // 2 is an argument to the escape sequence, comes before the actual command.
  // <esc>[1J = clear screen up to where cursor is
  // <esc>[0J = clear screen from cursor to end - this is also the default argument so <es>[J has the same effect
  // <esc>[2J = clear the entire screen
  abAppend(&ab, "\x1b[H", 3);
  // position cursor 
  // by default, row col args are 1 so this has same effect as <esc>[1;1H
  // can modify to move cursor to other areas

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6); // show cursor after printing
  
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  // ... argument allows function to be variadic, i.e. taking any number of arguments
  // last argument before ... must be passed to va_start() so address of next args known
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  // call va_arg(), pass type of next arg
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/
char *editorPrompt(char *prompt) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
  }
}

void editorMoveCursor(int key)
{
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key)
  {
  case ARROW_LEFT:
    if (E.cx != 0) 
    {
      E.cx--;
    } else if (E.cy > 0) { // allow user to move to prev line if at start of a later line
        E.cy--;
        E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row -> size) {
      E.cx++;
    } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
    } // allow user to move to next line if at end of a prev line
    break;
  case ARROW_UP:
    if (E.cy != 0) 
    {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows) 
    {
      E.cy++;
    }
    break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row -> size : 0;
  if (E.cx > rowlen)
  {
    E.cx = rowlen;
  }
}

void editorProcessKeypress()
{
  static int quit_times = KILO_QUIT_TIMES;

  int c = editorReadKey();

  switch(c)
  {
    case '\r':
      editorInsertNewline();
      break;

    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }

        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;  
    
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    case CTRL_KEY('l'):
    case '\x1b':
      break;

    default: 
      editorInsertChar(c);
      break;
  }

  quit_times = KILO_QUIT_TIMES;
}

/*** init ***/

void initEditor() 
{
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0; // by default, scrolled to top of file
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0'; // initialize to an empty string so no message displayed by default
  E.statusmsg_time = 0; // will contain time stamp when set by a status message

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2; // save last 2 lines for status bar and messages
}

int main(int argc, char *argv[]) 
{
  enableRawMode();
  initEditor();
  if (argc >= 2)
  { 
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

  while(1) //reads input character by character till q is pressed
  {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}