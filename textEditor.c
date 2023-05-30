#define _POSIX_C_SOURCE 200809L

/** includes **/

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>

/** defines **/

#define EDITOR_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
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

/** data **/

typedef struct erow {
    int size;
    char *chars;
} erow;

struct editorConfig {
    int cx;
    int cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    struct termios orig_termios;
};

struct editorConfig E;

/** terminal **/

void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    //perror prints a descriptive error message with errno global variable
    perror(s);
    exit(1);
}

void disableRawMode() 
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}


//Raw mode does not echo input to the terminal. It is useful for password input
void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");

    //whether it exits by main or exit, it ensures that the terminal is reset
    atexit(disableRawMode);
    struct termios raw = E.orig_termios;

    //Read terminal attributes into raw
    tcgetattr(STDIN_FILENO, &raw);

    //CtrlM produces CR, and disables CtrlS & CtrlQ, avoiding pausing transmission of data
    //BRKINT will cause SIGINT when pressing CtrlC
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    //Disables output processing
    raw.c_oflag &= ~(OPOST);

    //Set character size (CS) to 8 bits per byte
    raw.c_cflag |= ~(CS8);

    //Disables echo printing, canonical mode , CtrlV and CtrlC & CtrlZ signals
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    //Sets minimum number of bytes of input needed before read() can return
    raw.c_cc[VMIN] = 0;

    //Sets max amount of time to wait before read() returns. (1/10 of seconds, 100ms)
    raw.c_cc[VTIME] = 1;

    //TCSAFLUSH argument specifies when to apply the change
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

int editorReadKey() 
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        //Won't treat EAGAIN to make it work in Cygwin
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == '\x1b')
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                        //HOME_KEY and END_KEY can be written multiple ways
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
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
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
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
        return c;
}

int getCursorPosition(int *rows, int *cols)
{
    if (write(STDOUT_FILENO, "\x1b[6n", 4))
        return -1;

    char buf[32];
    unsigned int i = 0;

    //Get response buffer (Cursor Position Report), ex: <esc>[24;80R 
    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        //Sending two escapes sequence : C (Cursor Forward) and B (Cursor Down) with larges values to be at right bottom
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[99B", 12))
            return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations **/

void editorAppendRow(char *s, size_t len)
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}

/** file i/o **/

void editorOpen(char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    linelen = getline(&line, &linecap, fp);
    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
        //We strip off \n and \r because we know each erow represents one line of text
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;

        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

/** append buffer **/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}


/** output **/

void editorScroll()
{
    //Enable vertical scrolling
    if (E.cy < E.rowoff)
        E.rowoff = E.cy;

    if (E.cy >= E.rowoff + E.screenrows)
        E.rowoff = E.cy - E.screenrows + 1;

    //Enable horizontal scrolling
    if (E.cx < E.coloff)
        E.coloff = E.cx;

    if (E.cx >= E.coloff + E.screencols)
        E.coloff = E.cx - E.screencols + 1;
}

void editorDrawRows(struct abuf *ab)
{
    //Put tildes on the left, like Vim
    for (int y = 0; y < E.screenrows; y++)
    {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows)
        {
            if (y >= E.numrows)
            {
                //Display welcome message only if program is started without argument
                if (E.numrows == 0 && y == E.screenrows / 3)
                {
                    char welcome[80];
                    int welcomelen = snprintf(welcome, sizeof(welcome),
                        "My Editor -- version %s", EDITOR_VERSION);
                    if (welcomelen > E.screencols)
                        welcomelen = E.screencols;
                    int padding = (E.screencols - welcomelen) / 2;
                    if (padding)
                        {
                        abAppend(ab, "~", 1);
                        padding--;
                    }
                    while (padding--)
                        abAppend(ab, " ", 1);
                    abAppend(ab, welcome, welcomelen);
                }
                else
                    abAppend(ab, "~", 1);
            }
        }
        else
        {
            int len = E.row[filerow].size - E.coloff;
            if (len < 0)
                len = 0;
            if (len > E.screencols)
                len = E.screencols;
            abAppend(ab, &E.row[filerow].chars[E.coloff], len);
        }
        //(Erase In Line) erases the part of the line to the right of the cursor
        abAppend(ab, "\x1b[K", 3);
        //In order to have a tilde on last line
        if (y < E.screenrows - 1)
            abAppend(ab, "\r\n", 2);
    }
}

void editorRefreshScreen()
{

    editorScroll();
    //Buffer for one big write to update once, avoiding flicker effect
    struct abuf ab = ABUF_INIT;

    //Hides the cursor (Reset Mode)
    abAppend(&ab, "\x1b[?25l", 6);

    //Reposition the cursor : first row and first column
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    //Redraw cusor (Set Mode)
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/** input **/

void editorMoveCursor(int key)
{
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch(key)
    {
        case ARROW_RIGHT:
            if (row && E.cx < row->size)
                E.cx++;
            //Going right at end of line moves at start of next line
            else if (row && E.cx == row->size)
            {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows)
                E.cy++;
            break;
        case ARROW_LEFT:
            if (E.cx != 0)
                E.cx--;
            //Going left at start of line moves to end of previous line
            else if (E.cy > 0) 
            {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0)
                E.cy--;
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
        E.cx = rowlen;
}

void editorProcessKeypress()
{
    int c = editorReadKey();

    switch(c)
    {
        case CTRL_KEY('q'):
            //Not using atexit() because error message would get erased right after printing it with die()
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            E.cx = E.screencols - 1;
            break;

        case PAGE_UP:
        {
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(ARROW_UP);
        }
        break;
        case PAGE_DOWN:
        {
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(ARROW_DOWN);
        }
        break;

        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/** init **/

void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    //Will initiliaze fields in E struct
    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
}

int main(int argc, char **argv) 
{
    enableRawMode();
    initEditor();
    if (argc >= 2)
        editorOpen(argv[1]);

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
