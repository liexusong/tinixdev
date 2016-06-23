//sudo apt-get install libncurses5-dev libncursesw5-dev
  
#include <sys/stat.h>
#include <curses.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>

#define dbgprint(format,args...) \
        fprintf(stderr, "fun:%s-L%d: "format, __FUNCTION__, __LINE__, ##args);
//编辑记录
#define     MAX_EDIT    2048
typedef struct _edit_record {
    long            offset;
    unsigned char   orgbyte;
    unsigned char   newbyte;
}edit_record;
edit_record er[MAX_EDIT];
int erc = 0,ercmax;//编辑记录组，记录计数，最大计数

//快捷键缓冲，快捷键只在浏览模式下有效
char shortcuts=0x20;

//全局变量
extern int  LINES,COLS; //环境变量
int         WORK_LINES,WORK_COLS; // 工作终端行列 
long        fpt=0,lx=0,ly=0,x=0,y=0; //当前文件偏移，当前逻辑坐标，当前终端坐标
char        buff[3100];
char        tips[50] = {0}; //全局提示
FILE        *fp;
long        fsz;

#define    MODE_VIEWONLY    0 //只读模式
#define    MODE_VIEW        1 //阅读模式
#define    MODE_EDIT        2 //编辑模式
#define    MODE_COMMAND     3 //命令模式
#define    AREA_HEX         0
#define    AREA_ASCII       1
#define    COLOR_VIEW       (COLOR_PAIR(1)|A_BOLD)
#define    COLOR_EDIT       (COLOR_PAIR(2)|A_BOLD)
#define    COLOR_RECD       (COLOR_PAIR(3)|A_UNDERLINE|A_BOLD)
char mode=MODE_VIEW,area=AREA_HEX,hexpos=0; //当前模式，光标所在区域

bool initial();
void uninit();
bool filestat(char *filename);
void msgloop(); 
void refrebuf();   //更新屏幕
void setstatus();  //仅更新状态栏，及编辑区域彩色，用于lmove
void edithex(int byte);     //hex 区域编辑事件
void editasc(int byte);     //ascii 区域编辑事件
bool addrecd(long offset, unsigned char orgbyte, unsigned char newbyte); //新增编辑记录
bool execommand(char *cmd); 	//返回假则退出
bool writefile(char *filename); //将编辑更新至文件
bool doshortcut(int key);      	//响应快捷键
void undolast(bool all);        //撤销上次编辑,all为真则撤销全部
void redolast(bool all);        //重做上次编辑,all为真则重做全部
void gobottom();                //跳至最后一个字节
void gooffset(char *offset);    //跳至offset
void lmove(int newy, int newx); //移动逻辑光标,自动触发更新状态栏

int main(int argc, char **argv)
{
    if(argc != 2 ) 
    {
        printf("USAGE: %s [file]\n", argv[0]);
        return -1;
    }
    else
    {
        if (filestat(argv[1]))
        {
            printf("%s\n", tips);
            return -1;
        }
        fp = fopen(argv[1], "r+b");
        if(fp == NULL)
        {
            mode = MODE_VIEWONLY;
            fp = fopen(argv[1], "rb");
            if(fp == NULL)
            {
                printf("Open file [%s] fail : %s \n",argv[1], strerror(errno));
                return -1;
            }
            fseek(fp, 0, SEEK_END);
            fsz = ftell(fp);
        }
        else
        {
            mode = MODE_VIEW;
            fseek(fp, 0, SEEK_END);
            fsz = ftell(fp);
        }
        if (fsz == 0)
        {
            printf("Is an empty file.\n");
            return -1;
        }
    }

    if ( initial() )  // 启动 curses 模式
    {
        refrebuf(); // 显示数据
        msgloop();
        uninit();
    }
    else
    {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

void uninit()
{
    clear();
    refresh();
    endwin();
}

bool initial()
{
    initscr();
    WORK_LINES = LINES;
    WORK_COLS  = COLS;
    
    // 工作区行列初始化
    if ( WORK_LINES < 5 || WORK_COLS < 78)
    {
        endwin();
        printf ("I can not work well on LINES %d COLS %d !\n", 
        WORK_LINES, 
        WORK_COLS);
        return false;
    } 
    
    //颜色初始化 
    if(!has_colors() || start_color() == ERR)
    { 
        endwin(); 
        printf( "Terminal does not support color.\n"); 
        return false;
    } 

    init_pair(1,   COLOR_WHITE,   COLOR_GREEN); //浏览模式 绿底白字
    init_pair(2,   COLOR_WHITE,   COLOR_RED);   //编辑模式 红底白字
    init_pair(3,   COLOR_YELLOW,  COLOR_BLACK); //标记位置 黑底黄字
    /*init_pair(3,   COLOR_CYAN,   COLOR_BLACK); 
    init_pair(4,   COLOR_WHITE,   COLOR_BLACK); 
    init_pair(5,   COLOR_MAGENTA,   COLOR_BLACK); 
    init_pair(6,   COLOR_BLUE,   COLOR_BLACK);*/ 

    cbreak();
    nonl();
    noecho();
    intrflush(stdscr,FALSE); 
    keypad(stdscr,TRUE);
    refresh();

    return true;
}

bool filestat(char *filename)
{
    struct stat info;
    int ret;
    ret = lstat (filename, &info);
    if (ret != -1)
    {
        if (S_ISLNK(info.st_mode)) {strcpy(tips, "Is a symbol link.");return 0;}
        if (S_ISREG(info.st_mode)) {strcpy(tips, "Is a normal file.");return 0;}
        if (S_ISDIR(info.st_mode)) {strcpy(tips, "Is a directory.");return -1;}
        if (S_ISCHR(info.st_mode)) {strcpy(tips, "Is a character device.");return -1;}
        if (S_ISBLK(info.st_mode)) {strcpy(tips, "Is a block device.");return -1;}
        strcpy(tips, "Is a unknow device.");
        return -1;
    }
    else
    {
        sprintf(tips, "%-49s", strerror(errno));
        return -1;
    }
}

void msgloop()
{
    int ch;
    char command[30];
    
    do { 
        ch=getch();
        if (ch == '\t')         // 区域切换，优先级最高
        {
            shortcuts = 0x20;   //快捷键缓冲清空
            hexpos = 0;         //hex编辑位置回归
            if (area == AREA_HEX) area = AREA_ASCII;
            else area = AREA_HEX; 
            lmove(ly, lx);
            continue;
        }
        if (ch == 27 || ch == KEY_F(1)) //模式切换，优先级最高
        {
            shortcuts = 0x20;   //快捷键缓冲清空
            if (mode != MODE_VIEWONLY) mode = MODE_VIEW;
            hexpos = 0;         //hex编辑位置回归
            tips[0] = 0;
            lmove(ly, lx);      //用于更新状态栏
            continue;
        }
        if (ch == '\r')         // 命令模式，优先级最高
        {
            shortcuts = 0x20;   //快捷键缓冲清空
            int orgmode = mode;
            mode = MODE_COMMAND;
            tips[0] = 0;
            lmove(ly, lx);      //用于更新状态栏 
            move(WORK_LINES-1, 11);
            echo();
            getnstr(command, 29);
            noecho();
            if (!execommand(command))
                return;
            mode = orgmode;
            refrebuf();
            continue;
        }
        if (mode == MODE_VIEW || mode == MODE_VIEWONLY) // 浏览模式，优先级第二
        {
            switch(ch) {
                case 'w':
                case 'W':
                case KEY_NPAGE:  //下一页
                    if (fsz > fpt+(WORK_LINES-2)*16 && fpt+(WORK_LINES-2)*16 > 0) //可能溢出
                    {
                        fpt = fpt+(WORK_LINES-2)*16;
                        if ((fsz - fpt) <= (ly)*16+lx) 
                            lmove(0,0); //如果光标超出当前页范围，则置光标0,0
                        refrebuf();
                    }//文件未显示完全,则滚动
                    break;
                case 'b':
                case 'B':
                case KEY_PPAGE:
                    if (fpt >= (WORK_LINES-2)*16) 
                    {
                        fpt = fpt - (WORK_LINES-2)*16;
                        refrebuf();
                    }
                    else if (fpt > 0) 
                    {
                        fpt = 0;
                        refrebuf();
                    } //不足一页则回到0位置
                    break;
                case '.':   //向下滚动一行
                    if (fsz > fpt+(WORK_LINES-2)*16 && fpt+(WORK_LINES-2)*16 > 0) //可能溢出
                    {
                        fpt = fpt + 16;
                        if ((fpt+ly*16+lx)>fsz-1) lx = (fsz-1) % 16;
                        refrebuf();
                    }//文件未显示完全,则滚动
                    break;
                case ',':   //向上滚动一行
                    if (fpt > 0) {fpt = fpt - 16;refrebuf();}
                    break;
                case KEY_UP:
                case 'k':
                case 'K':
                    lmove(ly-1,lx);
                    break;
                case KEY_DOWN:
                case 'j':
                case 'J':
                    lmove(ly+1,lx);
                    break;
                case KEY_RIGHT:
                case 'l':
                case 'L':
                    lmove(ly,lx+1);
                    break;
                case KEY_LEFT:
                case 'h':
                case 'H':
                    lmove(ly,lx-1);
                    break;
                case 'i':
                case 'I':
                    if (mode != MODE_VIEWONLY) mode = MODE_EDIT;
                    tips[0] = 0;
                    lmove(ly, lx); //用于更新状态栏
                    break;
                default:
                    if (!doshortcut(ch)) return;
                    lmove(ly, lx); //用于更新状态栏
                    break;
            }
            continue;
        }
        if (mode == MODE_EDIT && area == AREA_HEX) // hex区域编辑模式，优先级第三
        {
            shortcuts = 0x20;   //快捷键缓冲清空
            switch(ch) {
                //case 'w':
                //case 'W':
                case KEY_NPAGE:  //下一页
                    hexpos = 0; //hex编辑位置回归
                    if (fsz > fpt+(WORK_LINES-2)*16 && fpt+(WORK_LINES-2)*16 > 0) //可能溢出
                    {
                        fpt = fpt+(WORK_LINES-2)*16;
                        if ((fsz - fpt) <= ly*16+lx) 
                            lmove(0,0); //如果光标超出当前页范围，则置光标0,0
                        refrebuf();
                    }//文件未显示完全,则滚动
                    break;
                //case 'b':
                //case 'B':
                case KEY_PPAGE:
                    hexpos = 0; //hex编辑位置回归
                    if (fpt >= (WORK_LINES-2)*16) 
                    {
                        fpt = fpt - (WORK_LINES-2)*16;
                        refrebuf();
                    }
                    else if (fpt > 0) 
                    {
                        fpt = 0;
                        refrebuf();
                    } //不足一页则回到0位置
                    break;
                case '.':   //向下滚动一行
                    hexpos = 0; //hex编辑位置回归
                    if (fsz > fpt+(WORK_LINES-2)*16 && fpt+(WORK_LINES-2)*16 > 0) //可能溢出
                    {
                        fpt = fpt + 16;
                        if ((fpt+ly*16+lx)>fsz-1) lx = (fsz-1) % 16;
                        refrebuf();
                    }//文件未显示完全,则滚动
                    break;
                case ',':   //向上滚动一行
                    hexpos = 0; //hex编辑位置回归
                    if (fpt > 0) {fpt = fpt - 16;refrebuf();}
                    break;
                case KEY_UP:
                case 'k':
                case 'K':
                    hexpos = 0; //hex编辑位置回归
                    lmove(ly-1,lx);
                    break;
                case KEY_DOWN:
                case 'j':
                case 'J':
                    hexpos = 0; //hex编辑位置回归
                    lmove(ly+1,lx);
                    break;
                case KEY_RIGHT:
                case 'l':
                case 'L':
                    hexpos = 0; //hex编辑位置回归
                    lmove(ly,lx+1);
                    break;
                case KEY_LEFT:
                case 'h':
                case 'H':
                    hexpos = 0; //hex编辑位置回归
                    lmove(ly,lx-1);
                    break;
                default:
                    edithex(ch);
                    break;
            }
            continue;
        }
        if (mode == MODE_EDIT && area == AREA_ASCII) // asc区编辑模式，优先级第四
        {
            shortcuts = 0x20;   //快捷键缓冲清空
            if (ch < 128)
                if (isprint(ch)) editasc(ch);
            continue;
        }
    } while (1);
}



void refrebuf()
{
    unsigned char *buf;
    char prtbuf[300];
    int rsz,i,j,line=0;
    unsigned offset;

    //clear();
    
    offset = fpt;
    buf = buff;
    fseek(fp, fpt, SEEK_SET);
    rsz = fread(buff, 1, (WORK_LINES-2)*16, fp);
    
    // 满行显示
    while(rsz >= 16)
    {
        sprintf(prtbuf, 
            " %08X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X |",
            offset,  buf[0],  buf[1],  buf[2],  buf[3],  buf[4],
            buf[5],  buf[6],  buf[7],  buf[8],  buf[9],  buf[10],
            buf[11], buf[12], buf[13], buf[14], buf[15]);
        for (i = 0; i < 16; i++)
        {
            char ascii[2]={0,0};
            if (isprint(buf[i]))
                ascii[0] = buf[i];
            else
                ascii[0] = '.';
            strcat(prtbuf, ascii);
        }
        
        mvaddstr(++line, 0, prtbuf);
        buf = buf + 16;
        rsz = rsz - 16;
        offset = offset + 16;
    }

    // 不足满行
    if ( rsz != 0)
    {
        sprintf(prtbuf, "%77s", " ");
        mvaddstr(++line, 0, prtbuf);
        
        sprintf(prtbuf, "|%08X: ", offset);
        mvaddstr(line, 0, prtbuf);
        for (i = 0; i<rsz; i++)
        {
            sprintf(prtbuf, "%02X ", buf[i]);
            if(i > 7) mvaddstr(line, 11+i*3+1, prtbuf);
            else      mvaddstr(line, 11+i*3,   prtbuf);
            
            mvaddch(line, 60, '|');
            if (isprint(buf[i]))
            {
                mvaddch(line, 61+i, buf[i]);
            }
            else
            {
                mvaddch(line, 61+i, '.');
            }
        }
    }
    
    //补齐空白区
    for (i = line+1; i< WORK_LINES-1 ; i++)
    {
        sprintf(prtbuf, "%78s", " ");
        mvaddstr(i, 0, prtbuf);
    }
    
    /*
    // 相对位置反白
    int rx,ry;
    
    attron(A_REVERSE);
    if (area == AREA_ASCII)
    {
        rx = lx*3+10;if(lx > 7) rx++;
        ry = ly+1;
        sprintf(prtbuf, "%02X", buff[ly*16+lx]);
        mvaddstr(ry, rx, prtbuf);
    }
    else
    {
        rx = 60 + lx;
        ry = ly + 1;
        if (isprint(buff[ly*16+lx]))
        {
            mvaddch(ry, rx, buff[ly*16+lx]);
        }
        else
        {
            mvaddch(ry, rx, '.');
        }
    }
    attroff(A_REVERSE);
    */
    
    //setstatus(); // 更新状态栏
    lmove(ly, lx);
    refresh();  //刷新curses屏幕缓存
}

void lmove(int newy, int newx)
{
    if(newy < 0) //ly坐标向上出界
    {
        if (fpt > 0) //且文件指针大于0,自动上滚一行
        {
            fpt -= 16;
            refrebuf();
            return;
        }
        else
        {
            return;
        }
    }
    
    if(newy >= (WORK_LINES-2)) //ly坐标向下出界
    {
        if(fsz > fpt+(WORK_LINES-2)*16 && fpt+(WORK_LINES-2)*16 > 0) //且文件内容未显示完全
        {
            fpt += 16;
            if ((fpt+ly*16+lx)>fsz-1) lx = (fsz-1) % 16;
            refrebuf(); //自动下滚一行
            return;
        }
        else
        {
            return;
        }
    }
    if (newx < 0 || newx > 15) return; // lx向左或向右出界
    
    if (newy*16+newx < fsz-fpt)  // 如果newx newy在屏幕buf之内
    {
        if (area == AREA_HEX)
        {
            ly = newy;
            lx = newx;
            y = newy + 1;
            x = newx*3 + 11 + hexpos;
            if (newx>7) x++;
        }
        else
        {
            ly = newy;
            lx = newx;
            y = newy + 1;
            x = lx + 61;
        }
    }

    setstatus(); // 更新状态栏
    move(y, x);
    return;
}


void setstatus()
{
    char    status[100] = {0};
    char    sta_mode[15]= {0};
    
    switch (mode){
        case MODE_VIEWONLY:
            strcat(sta_mode, "|VIEW-ONLY ");
            break;
        case MODE_VIEW:
            if( area == AREA_HEX) strcat(sta_mode, "|VIEW-HEX  ");
            else strcat(sta_mode, "|VIEW-ASC  ");
            break;
        case MODE_EDIT:
            if( area == AREA_HEX) strcat(sta_mode, "|EDIT-HEX  ");
            else strcat(sta_mode, "|EDIT-ASC  ");
            break;
        case MODE_COMMAND:
            strcat(sta_mode, "|COMMAND : ");
            break;
    }
    sprintf(status, "%s%-49s|%08X%7d%%|", 
        sta_mode,
        tips,
        (int)(fpt+ly*16+lx),
        (int)((fpt+ly*16+lx)*100/fsz));
    
    // 绘制进度条
    int line;
    for (line = 1; line < WORK_LINES; line++)
    {
        mvaddch(line, 77, '|');
        mvaddch(line, 0, '|');
    }

    //绘制状态栏，及进度
    if (mode == MODE_VIEW || mode == MODE_VIEWONLY)
    {
        attron(COLOR_VIEW);
        mvaddstr(WORK_LINES-1,0,status);
        mvaddch(WORK_LINES-1, 58, shortcuts);
        mvaddch((fpt+ly*16+lx)*(WORK_LINES-3)/fsz+1, 77, ' ');
        mvaddch((fpt+ly*16+lx)*(WORK_LINES-3)/fsz+2, 77, ' ');
        mvaddstr(0,0,
        "|Offset  |  0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F |Ascii           |");
        attroff(COLOR_VIEW);
    }
    else if (mode == MODE_EDIT)
    {
        attron(COLOR_EDIT);
        mvaddstr(WORK_LINES-1,0,status);
        mvaddch((fpt+ly*16+lx)*(WORK_LINES-3)/fsz+1, 77, ' ');
        mvaddch((fpt+ly*16+lx)*(WORK_LINES-3)/fsz+2, 77, ' ');
        mvaddstr(0,0,
        "|Offset  |  0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F |Ascii           |");
        attroff(COLOR_EDIT);
    }
    else
    {
        mvaddstr(WORK_LINES-1,0,status);
    }
    
    
    int index,newx,newy,rx,ry;
    
    /* 
    此处处理彩色 
    */
    
    attron(COLOR_RECD);
    for (index = 0; index < erc; index++)
    {
        if (er[index].offset >= fpt && er[index].offset < fpt+(WORK_LINES-2)*16)
        {
            newx = er[index].offset % 16;
            newy = (int)((er[index].offset-fpt) / 16);
            rx = newx*3+11;if(newx > 7) rx++;
            ry = newy+1;
            sprintf(status, "%02X", er[index].newbyte);
            mvaddstr(ry, rx, status);
            rx = 61 + newx;
            if (isprint(er[index].newbyte))
            {
                mvaddch(ry, rx, er[index].newbyte);
            }
            else
            {
                mvaddch(ry, rx, '.');
            }
        }
    }
    attroff(COLOR_RECD);
}


bool execommand(char *cmd)
{
    char arg1[30] = {0};
    char arg2[30] = {0};
    
    int index; // 处理掉命令里的不可见字符
    for (index = 0; index < strlen(cmd); index++)
        if (!isprint(cmd[index])) cmd[index] = '.';
    
    sscanf(cmd, "%s %s", arg1, arg2);
    if (strlen(arg1) == 1)
    {
        switch(arg1[0]) {
            case 'q':
                if (erc == 0) return false;
                sprintf(tips, "%d bytes changed (Q to override)", erc);
                break;
            case 'Q':
                return false;
            case 'G':
                gobottom();
                break;
            case 'g':
                gooffset(arg2);
                break;
            case 'w':
                writefile(arg2);
                break;
            case 'c':
                undolast(false);
                break;
            case 'C':
                undolast(true);
                break;
            case 'r':
                redolast(false);
                break;
            case 'R':
                redolast(true);
                break;
            default:
                sprintf(tips, "Unknow Command: arg1 %s arg2 %s", arg1, arg2);
        }
    }
    else
    {
        sprintf(tips, "Invalid Command: %s", cmd);
    }
    return true;
}

void edithex(int byte)
{
    unsigned char new,org;
    int  index;

    if (erc == 0)
    {
        org = buff[ly*16+lx];
    }
    else
    {
        for (index = 0; index < erc; index++)
        {
            if (er[index].offset == ly*16+lx+fpt)
            {
                org = er[index].newbyte;
                break;
            }
            if (index == erc-1)
                org = buff[ly*16+lx];
        }
    }


    if (hexpos == 0)
    {
        if (byte >= '0' && byte <= '9')
        {
            new = org & 0x0f;
            new = new | ((byte - '0')<<4);
        }
        else if ((byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F'))
        {
            new = org & 0x0f;
            new = new | (((byte | 0x20)-'A'+10)<<4);
        }
        else
        {
            sprintf(tips, "You pressed %d", byte);
            //主要用于更新状态栏
            lmove(ly, lx);
            return;
        }
        
        if(addrecd(fpt+ly*16+lx, org, new))
        {
            hexpos = 1;
            lmove(ly, lx);
        }  
    }
    else
    {
        if (byte >= '0' && byte <= '9')
        {
            new = org & 0xf0;
            new = new | ((byte - '0')&0x0f);
        }
        else if ((byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F'))
        {
            new = org & 0xf0;
            new = new | (((byte | 0x20)-'A'+10)&0x0f);
        }
        else
        {
            sprintf(tips, "You pressed %d", byte);
            //主要用于更新状态栏
            lmove(ly, lx);
            return;
        }  
        if(addrecd(fpt+ly*16+lx, org, new))
        {
            hexpos = 0;
            if (lx<15) lmove(ly, lx+1);
            else {lx=0;lmove(ly+1, lx);}
        }        
    }
    //主要用于更新状态栏
    lmove(ly, lx);
    return;
}

void editasc(int byte)
{
    if (addrecd(fpt+ly*16+lx, buff[ly*16+lx], byte))
    {
        if (lx<15) 
        {
            lmove(ly, lx+1);
        }
        else 
        {
            lx=0;
            lmove(ly+1, lx);
        }
        return;
    }
    //主要用于更新状态栏
    lmove(ly, lx);
    return;
}


bool addrecd(long offset, unsigned char orgbyte, unsigned char newbyte)
{
    int index;
    
    for (index = 0; index < erc; index++)
    {
        if (er[index].offset == offset)
        {
            er[index].newbyte = newbyte;
            return true;
        }
    }
    
    if (erc == MAX_EDIT)
    {
        sprintf (tips,"Edited bytes %d,buff is full.", MAX_EDIT);
        return false;
    }
    
    er[erc].offset = offset;
    er[erc].newbyte = newbyte;
    er[erc].orgbyte = orgbyte;
    erc++;
    ercmax = erc;
    
    return true;
}


bool writefile(char *filename)
{
    if (erc == 0)
    {
        strcpy(tips, "Write what?");
        return false;
    }
    
    if (strlen(filename) == 0)
    {
        int index;
        for (index = 0; index < erc; index++)
        {
            fseek(fp, er[index].offset, SEEK_SET);
            if (fwrite(&(er[index].newbyte), 1, 1, fp) != 1)
            {
                sprintf(tips, "Error: %s", strerror(errno));
                return false;
            }
        }
        sprintf(tips, "%d bytes written.", erc);
    }
    else
    {
        strcpy(tips, "Hello world!");
    }
    erc = 0;
    return true;
}


bool doshortcut(int key)
{
    char cmd[2] = " ";
    if (isprint(key))
    {
        if (shortcuts != key)
        {
            shortcuts = key;
        }
        else if (shortcuts == key)
        {
            cmd[0] = shortcuts;
            if (!execommand(cmd)) return false;
            shortcuts = 0x20;
            refrebuf();
        }
        else
        {
            shortcuts = 0x20;
        }
    }
    else
    {
        shortcuts = 0x20;
    }
    return true;
}

void undolast(bool all)
{
    if (erc == 0)
    {
        strcpy(tips, "Undo what?");
    }
    else
    {
        if (all == true)
        {
            sprintf(tips, "Undo all changes, %d bytes.", erc);
            erc = 0;
        }
        else
        {
            if (er[erc-1].offset >= fpt && er[erc-1].offset < fpt + (WORK_LINES-2)*16) 
            //如果目标位置在本页内
            {
                lmove((int)(er[erc-1].offset-fpt)/16, (er[erc-1].offset-fpt)%16);
            }
            else
            {
                fpt = er[erc-1].offset &0xfffffff0;
                lmove(0, er[erc-1].offset - fpt);
            }
            erc --;
            strcpy(tips, "Undo last changes.");
        }
    }
}

void redolast(bool all)
{
    if (erc == ercmax)
    {
        strcpy(tips, "Redo what?");
    }
    else
    {
        if (all == true)
        {
            sprintf(tips, "Redo all changes, %d bytes.", ercmax - erc);
            erc = ercmax;
        }
        else
        {
            if (er[erc].offset >= fpt && er[erc].offset < fpt + (WORK_LINES-2)*16) 
            //如果目标位置在本页内
            {
                lmove((int)(er[erc].offset-fpt)/16, (er[erc].offset-fpt)%16);
            }
            else
            {
                fpt = er[erc].offset &0xfffffff0;
                lmove(0, er[erc].offset - fpt);
            }
            erc ++;
            strcpy(tips, "Redo last changes.");
        }
    }
}

void gooffset(char *arg2)
{
    unsigned param_num = 0;
    
    if (strlen(arg2) == 0) //如果g不带参则默认g 0 
    {
        lmove(0, 0);
        fpt = 0;
        strcpy(tips, "Go Top.");
    }
    else
    {
        param_num = strtoul(arg2, NULL, 0);
        if (param_num > 0 && param_num < fsz)
        {
            if (param_num >= fpt && param_num < fpt + (WORK_LINES-2)*16) 
            //如果目标位置在本页内
            {
                lmove((int)(param_num-fpt)/16, (param_num-fpt)%16);
                sprintf(tips, "Go offset %08X", param_num);
            }
            else
            {
                fpt = param_num &0xfffffff0;
                lmove(0, param_num - fpt);
                sprintf(tips, "Go offset %08X", param_num);
            }
        }
        else
        {
            sprintf(tips, "Invalid offset %s", arg2);
        }
    }
}

void gobottom()
{
    if (fpt + (WORK_LINES-2)*16 >= fsz) //在屏幕内
    {
        lmove((int)(fsz-fpt-1)/16, (fsz-fpt-1)%16);
        strcpy(tips, "Go Bottom.");
    }
    else // 不再屏幕内跳到最后一页
    {
        fpt = (fsz-(WORK_LINES-2)*16 + 16)&0xfffffff0;
        lmove((int)(fsz-fpt-1)/16, (fsz-fpt-1)%16);
        strcpy(tips, "Go Bottom.");
    }
}
