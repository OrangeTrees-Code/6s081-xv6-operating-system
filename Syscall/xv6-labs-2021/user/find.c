#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
int find(char *path, char *fileName);

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(2, "find [path] [file name]\n");
        exit(1);
    }

    if (find(argv[1], argv[2]) < 0)
    {
        fprintf(2, "find: cannot find %s\n", argv[1]);
        exit(2);
    }

    exit(0);
}

char *
fmtname(char *path)
{
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
    return buf;
}

// Regexp matcher from Kernighan & Pike,
// The Practice of Programming, Chapter 9.

int matchhere(char*, char*);
int matchstar(int, char*, char*);

int
match(char* re, char* text) {
    if (re[0] == '^')
        return matchhere(re + 1, text);
    do {  // must look at empty string
        if (matchhere(re, text))
            return 1;
    } while (*text++ != '\0');
    return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char* re, char* text) {
    if (re[0] == '\0')
        return 1;
    if (re[1] == '*')
        return matchstar(re[0], re + 2, text);
    if (re[0] == '$' && re[1] == '\0')
        return *text == '\0';
    if (*text != '\0' && (re[0] == '.' || re[0] == *text))
        return matchhere(re + 1, text + 1);
    return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char* re, char* text) {
    do {  // a * matches zero or more instances
        if (matchhere(re, text))
            return 1;
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}

// int nameMatch(char* name,char* pattern) {
    // int fd_c2p[2];
    // pipe(fd_c2p);

    // int fd_p2c[2];
    // pipe(fd_p2c);
    // int pid = fork();

    // if (pid == 0) {
    //     close(0);
    //     dup(fd_p2c[0]);
    //     close(fd_p2c[1]);
    //     close(1);
    //     dup(fd_c2p[1]);
    //     close(fd_c2p[0]);
    //     char* argv = { pattern,0 };
    //     exec("/grep", argv);
    //     fprintf(2, "find: exev error!\n");
    //     exit(1);
    // }
    // else if (pid > 0) {
    //     close(fd_c2p[1]);
    //     close(fd_p2c[0]);
    //     write(fd_p2c[1], name, strlen(name));
    //     char match[128];
    //     read(fd_c2p[0], match, 128);
    //     close(fd_c2p[0]);
    //     close(fd_p2c[1]);
    //     wait(0);
    //     if (!strcmp(match, name)) {
    //         return 0;
    //     }
    //     else {
    //         return -1;
    //     }
    // }
    // else {
    //     fprintf(2, "find: fork failed!\n");
    //     return 1;
    // }

    ///////////////////////////////////////////////////////////////////////////////////////////////


// }

int find(char* path, char* fileName)
{
    int fd;
    char path_buf[512] = {0};
    struct stat st;

    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        exit(2);
    }

    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        exit(3);
    }
    char *p;
    struct dirent de;
    int found = -1;
    struct stat st2;
    switch (st.type)
    {
    case T_FILE:
        if(!strcmp(fileName,fmtname(path))){
            fprintf(1, "%s\n", path);
            return 0;
        }
        return -1;
    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(path_buf))
        {
            printf("find: path too long\n");
            break;
        }
        strcpy(path_buf, path);
        p = path_buf + strlen(path_buf);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0)
            {
                continue;
            }
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0){
                continue;
            }
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if (!strcmp(fileName,de.name))
            {
                found = 0;
                fprintf(1, "%s\n", path_buf);
            }
            if(stat(path_buf,&st2)<0){
                continue;
            }
            if(st2.type == T_DIR){
                if(find(path_buf, fileName)==0){
                    found = 0;           
                }
            }
        }
        break;
    }
    close(fd);
    return found;
}