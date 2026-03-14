#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/stat.h"
#include "user/user.h"

int forkAndExec(char** argv){
    int pid;
    pid = fork();
    if(pid == 0){
        exec(argv[0], argv);
        fprintf(2, "%s: exec failed\n", argv[0]);
        exit(-3);
    }
    else if(pid>0){
        int status;
        wait(&status);
        return status;
    }
    else{
        fprintf(2, "xagrs :fork failed\n");
        return -2;
    }
}

char** makeArgv(char** Argv,char** argv){
    int i = 0;
    for (; argv[i] != (void *)0; i++)
    {
        Argv[i] = argv[i];
    }
    return &Argv[i];
}

int main(int argc,char** argv){
    if(argc<2){
        fprintf(2, "xargs [cmdline]\n");
        exit(1);
    }

    char line[512];
    char ch;
    int i = 0;
    char *Argv[MAXARG] = {0};
    char** p = makeArgv(Argv, argv+1);
    while (read(0, &ch, 1) > 0)
    {
        if(ch=='\n'){
            line[i] = '\0';
            *p = line;
            if (forkAndExec(Argv) < 0)
            {
                fprintf(2, "xargs: forkAndExec error\n");
            }
            i = 0;
        }
        else{
            line[i++] = ch;
        }
    }

    if(i>0){
        line[i] = '\0';
        *p = line;
        if (forkAndExec(Argv) < 0)
        {
            fprintf(2, "xargs: forkAndExec error\n");
        }
    }

    exit(0);
}