# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/mman.h>
int main(int argc, char *argv[])
{
int test=99999;
char buf[25];
int     pid=getpid();
int ee=99999;
while(1){
    memset(buf,0,25);
    read(0,buf,25);
    int new_value=atoi(buf);
    if(new_value>0)
    {
        test=new_value;
    }
    printf("test %d 0x%lx=%d\n",pid,&test,test);
    //sleep(1);
}
    return 0;
}