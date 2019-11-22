#include<stdlib.h>
#include<string.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/mman.h>
#include<unistd.h>
#include <stdio.h>
#define SHELL 		"/bin/sh"

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
        int ret=1,i;
        unsigned int uSize=0;
        unsigned int uSize_temp=0;
        unsigned char buffer[64];
        unsigned char cbuffer;
        FILE *kernel_fd,*new_fd;
	unsigned char *command = malloc(1024);

        if(argc < 3 )
        {
                printf(" USAGE : rsa_tool KERNEL_IMAGE NEW_IMAGE\n");
                return 0;
        }

        kernel_fd = fopen(argv[1],"r");
        if( kernel_fd == NULL )
        {
                printf(" Can`t open Kernel Image : %d\n",argv[1]);
                return 0;
        }

	strcpy(command,"rm -rf ");
	strcat(command,argv[2]);
	runShell(command);

        new_fd = fopen(argv[2],"a+");
        if( new_fd == NULL )
        {
               printf(" Can`t open new Image : %d\n",argv[1]);
               return 0;
        }

        /* read uImage header */
        ret = fread(&buffer,sizeof(unsigned char),64,kernel_fd);
        if( ret < 0)
        {
                printf(" Can`t read kernel Image : %d\n",argv[1]);
                return 0;
        }

        /* DTB address */
        uSize = buffer[15] + (buffer[14] << 8)+ (buffer[13] << 16);
        printf(" %s HEADER SIZE : %d\n",argv[1],uSize);
	uSize += 64;		/* padding */
	if(uSize%64 != 0)
	{
	        uSize /= 64;
	        uSize += 1;	
	        uSize *= 64;    /* align */
	}
	uSize +=256;	/* rsa data */

        fseek(kernel_fd,0,SEEK_SET);
        fseek(new_fd,0,SEEK_SET);

        while(ret > 0)
        {
                ret = fread(&cbuffer,sizeof(unsigned char),1,kernel_fd);
                if( ret <= 0)
                {
                        cbuffer=0xA5;
                        uSize_temp=uSize-uSize_temp;
                        for(i=0;i<uSize_temp;i++)
                                fwrite(&cbuffer,sizeof(unsigned char),1,new_fd);
                        break;
                }
                fwrite(&cbuffer,sizeof(unsigned char),1,new_fd);
                uSize_temp++;
        }

        printf(" RSA DUMMY %s SIZE  : %d %d\n",argv[1],uSize, uSize_temp);

        fclose(kernel_fd);
        fclose(new_fd);
}
