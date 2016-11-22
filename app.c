#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <unistd.h>



#define PT2262_PATH "/dev/input/event1"

int fd_mada[4],fd_pt2262;

int wt_mada(int *fd, int *value)
{
	int i,ret;
	char value_buf[8];
	for(i=0; i<4; i++)
	{	
		sprintf(value_buf, "%d", value[i]);
		printf("mada_value[%d]:%s\n",i,value_buf);
		ret = write(fd[i],value_buf,strlen(value_buf));
		if(ret < 0)
			goto fail;
	}
	return ret;
fail:
	printf("write failed.\n");
	return ret;
}
int main(int argc,char * * argv)
{
	char path_tmp[16];
	int ret,i,j,mada_value[4];

	struct input_event event;

	ret = open(PT2262_PATH, O_RDWR);
	if(ret < 0)
		goto fail2;
	else
		fd_pt2262 = ret;
	
	for(i=0;i<4;i++)
	{
		sprintf(path_tmp, "/dev/mada%d", i);
		ret = open(path_tmp, O_RDWR);
		if(ret < 0)
			goto fail1;
		fd_mada[i] = ret;
	}

	while(1)
	{
		read(fd_pt2262, &event, sizeof(struct input_event));
		if(event.type == EV_KEY)
		switch(event.code)
		{
			case KEY_A:
				mada_value[0]=1;mada_value[1]=999;mada_value[2]=1;mada_value[3]=999;break;
			case KEY_B:
				mada_value[0]=1;mada_value[1]=1;mada_value[2]=1;mada_value[3]=1;break;
			case KEY_C:
				mada_value[0]=1;mada_value[1]=999;mada_value[2]=999;mada_value[3]=1;break;
			case KEY_D:
				mada_value[0]=999;mada_value[1]=1;mada_value[2]=1;mada_value[3]=999;break;
			default:
				break;
		}
		else
			{
			wt_mada(fd_mada, mada_value);
			sleep(1);
			mada_value[0]=1;mada_value[1]=1;mada_value[2]=1;mada_value[3]=1;
			wt_mada(fd_mada, mada_value);
			}
	}	
fail1:
	close(fd_pt2262);
	for(j=0; j<i; j++)
	{
		close(fd_mada[j]);
	}
fail2:
	return ret;
}
