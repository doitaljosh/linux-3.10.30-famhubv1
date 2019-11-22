#include<stdlib.h>
#include<string.h>
#include<sys/stat.h>
#include<fcntl.h>
#include <stdio.h>
#define SHELL 		"/bin/sh"
#define DTB_BUFFER_SIZE		1024 * 128
#define __u32			unsigned int
#define ___swab32(x) \
        ((__u32)( \
                (((__u32)(x) & (__u32)0x000000ffUL) << 24) | \
                (((__u32)(x) & (__u32)0x0000ff00UL) <<  8) | \
                (((__u32)(x) & (__u32)0x00ff0000UL) >>  8) | \
                (((__u32)(x) & (__u32)0xff000000UL) >> 24) ))

int main(int argc,char *argv[])
{
        int ret=1;
	int j=2;
        unsigned int size=0,dtb_size=0;
        unsigned int usize=0xFFFFFFFF;
        FILE *fd;
	struct stat st;

        if(argc < 3 )
        {
                printf(" USAGE : dtb_size Image dtb\n");
                return 0;
        }

	stat(argv[2],&st);
	dtb_size = st.st_size;	

        fd = fopen(argv[1],"rw+");
        if( fd == NULL )
        {
                printf(" Can`t open out put Image : %s\n",argv[1]);
                return 0;
        }
	
	fseek(fd,12,SEEK_SET);
        ret = fread(&usize,sizeof(unsigned int),1,fd);
	size = ___swab32(usize);
	size += dtb_size;
	printf(" SIZE = %x\n",size);
	usize = ___swab32(size);
	fseek(fd,12,SEEK_SET);
	ret = fwrite(&usize,sizeof(unsigned int),1,fd);
	fclose(fd);	
}
