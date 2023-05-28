/** includes **/

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

/** defines **/

#define CTRL_KEY(k) ((k) & 0x1f)

/** data **/

struct termios orig_termios;

/** terminal **/

void die(const char *s)
{
    //perror pritns a descriptive error message with errno global variable
    perror(s);
    exit(1);
}

void disableRawMode() 
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}


//Raw mode does not echo input to the terminal. It is useful for password input
void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");

    //whether it exits by main or exit, it ensures that the terminal is reset
    atexit(disableRawMode);
    struct termios raw = orig_termios;

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

char editorReadKey() 
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        //Won't treat EAGAIN to make it work in Cygwin
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
            die("read");
    }
    return c;
}

/** output **/

void editorRefreshScreen()
{
    //x1b is the escape character
    //Escape sequence command take arguments, here we use VT100 escape sequences
    //<esc>[2J clears the entire screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    //Reposition the cursor : first row and first column
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/** input **/

void editorProcessKeypress()
{
    char c = editorReadKey();

    switch(c)
    {
        case CTRL_KEY('q'):
            exit(0);
            break;
    }
}

/** init **/

int main() 
{
    enableRawMode();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
