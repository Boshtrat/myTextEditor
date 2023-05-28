#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

//Raw mode does not echo input to the terminal. It is useful for password input
void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &orig_termios);

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

    //TCSAFLUSH argument specifies when to apply the change
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() 
{
    enableRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
    {
        if (iscntrl(c))
            printf("%d\r\n", c);
        else
            printf("%d ('%c')\r\n", c, c);

    }
    return 0;
}
