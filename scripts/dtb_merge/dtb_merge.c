#include<stdlib.h>
#include<string.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/mman.h>
#include<unistd.h>
#include <stdio.h>
#define SHELL 		"/bin/sh"
#define DTB_BUFFER_SIZE		1024 * 128

int runShell (const char *command)
{
        int status;
        pid_t pid;
        if ((pid = vfork()) == 0)
        {
                execl (SHELL, SHELL, "-c", command, NULL);
                _exit (1);
        }
        else if (pid < 0)
        {
                status = -1;
        }
        else
        {
                status = 0;
                if (waitpid (pid, &status, 0) != pid)
                {
                        status = -1;
                }
        }
        return status;
}

int main(int argc,char *argv[])
{
        int ret=1;
	int j=2;
        unsigned char *buffer;
        FILE *new_fd,*dtb_fd;
	unsigned char *command = malloc(1024);

        if(argc < 3 )
        {
                printf(" USAGE : dtb_merge out_put_file dtb1 dtb2 ..\n");
                return 0;
        }

        new_fd = fopen(argv[1],"w");
        if( new_fd == NULL )
        {
                printf(" Can`t open out put Image : %s\n",argv[1]);
                return 0;
        }
	
	buffer = (unsigned char *)malloc(DTB_BUFFER_SIZE);

	while(argc>j)
	{
		memset(buffer, 0, DTB_BUFFER_SIZE );

		printf(" READ : %s\n",argv[j]);
		dtb_fd = fopen(argv[j],"r");
        	if( dtb_fd == NULL )
	        {
	                printf(" Can`t open dtb Image : %s\n",argv[j]);
	                return 0;
	        }

	       	/* dtb read */
        	ret = fread(buffer,sizeof(unsigned char),DTB_BUFFER_SIZE,dtb_fd);
	        if( ret < 0)
	        {
	                printf(" Can`t read dtb Image : %d\n",argv[j]);
	                return 0;
	        }
                fwrite(buffer,sizeof(unsigned char),DTB_BUFFER_SIZE,new_fd);
		j++;
		fclose(dtb_fd);
	}
	fclose(new_fd);	

}
