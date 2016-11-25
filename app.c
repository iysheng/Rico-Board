#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <unistd.h>
#include <linux/string.h>

typedef enum
{	
	false,true
}bool;

#define PT2262_PATH "/dev/input/event1"
#define RADAR_PATH "/dev/radar0"
#define MOTOR_PATH "/dev/motor0"
#define MOTOR_F 1500
#define MOTOR_L 1000
#define MOTOR_R 2000
#define DIRECTION_NUM 1

int fd_mada[4],fd_pt2262,fd_radar,fd_motor;
int motor_f = MOTOR_F;
int motor_l = MOTOR_L;	
int motor_r = MOTOR_R;
int direction_num = DIRECTION_NUM+1;

bool robot_mode = false;

int wt_mada(int *fd, int *value)
{
	int i,ret;
	char value_buf[8];
	for(i=0; i<4; i++)
	{	
		sprintf(value_buf, "%d", value[i]);
		//printf("mada_value[%d]:%s\n",i,value_buf);
		ret = write(fd[i],value_buf,strlen(value_buf));
		if(ret < 0)
			goto fail;
	}
	return ret;
fail:
	printf("write failed.\n");
	return ret;
}

int wt_motor(int fd, int value)
{
	char value_buf[8];
	int ret;
	sprintf(value_buf, "%d", value);
	ret = write(fd, value_buf, strlen(value_buf));
	return ret;
}

int get_direction(void)
{
	char value_left[8];
	char value_right[8];
	int value_l,value_r,ret;
	char value[8];
	sprintf(value, "%d", motor_l);
	write(fd_motor, value, strlen(value));
	sleep(1);
	memset(value_left, 0, sizeof(value_left));
	memset(value_right, 0, sizeof(value_right));
	ret = read(fd_radar, value_left, sizeof(value_left));
	if(ret < 0)
	{
		printf("get_direction left err.\n");
		goto fail;
	}
	else
	{
		value_l = atoi(value_left);
		//printf("int value_l is %d.\n", value_l);
		//printf("\n************************char value_l is %s.\n", value_left);
	}

	sprintf(value, "%d", motor_r);
	write(fd_motor, value, strlen(value));
	sleep(1);
	//printf("motor_r is %s.\n", value);
	ret = read(fd_radar, value_right, sizeof(value_right));
	if(ret < 0)
	{
		printf("get_direction right err.\n");
		goto fail;
	}
	else
	{
		value_r = atoi(value_right);
		//printf("value_r is %s.\n", value_right);
	}
	ret = (value_r > value_l) ? motor_r : motor_l;
	
fail:
	printf("\n**********************************return is %d.\n", ret);
	return ret;
}

void paulse(void)
{
	int mada_value_tmp[4];
	mada_value_tmp[0]=1;mada_value_tmp[1]=1;mada_value_tmp[2]=1;mada_value_tmp[3]=1;
	wt_mada(fd_mada, mada_value_tmp);
		
}


int main(int argc,char * * argv)
{
	char path_tmp[16];
	char length_buf[16];
	
	int ret,i,j,mada_value[4],length;

	struct input_event event;

	ret = open(PT2262_PATH, O_RDWR|O_NONBLOCK);
	if(ret < 0)
		goto fail1;
	else
		fd_pt2262 = ret;

	ret = open(MOTOR_PATH, O_RDWR);
	if(ret < 0)
		goto fail2;
	else
		fd_motor = ret;

	ret = open(RADAR_PATH, O_RDWR);
	if(ret < 0)
		goto fail3;
	else
		fd_radar= ret;
	
	for(i=0;i<4;i++)
	{
		sprintf(path_tmp, "/dev/mada%d", i);
		ret = open(path_tmp, O_RDWR);
		if(ret < 0)
			goto fail4;
		fd_mada[i] = ret;
	}

	while(1)
	{
		read(fd_pt2262, &event, sizeof(struct input_event));
		if((event.value == 1)&&(event.type == EV_KEY)&&(event.code == 0x0f))
		{
			robot_mode = !robot_mode;//切换工作模式
		}
		if((event.value == 1)&&(event.type == EV_KEY)&&(robot_mode == true))
		{
			switch(event.code)
			{
				case 0x10:
					mada_value[0]=1;mada_value[1]=999;mada_value[2]=1;mada_value[3]=999;break;
				case 0x12:
					mada_value[0]=1;mada_value[1]=999;mada_value[2]=999;mada_value[3]=1;break;
				case 0x13:
					mada_value[0]=999;mada_value[1]=1;mada_value[2]=1;mada_value[3]=999;break;
				case 0x11:
					mada_value[0]=1;mada_value[1]=1;mada_value[2]=1;mada_value[3]=1;break;
				default:
					break;
			}
			wt_mada(fd_mada, mada_value);
		}
		else if(robot_mode == false)
		{
			memset(length_buf,0,sizeof(length_buf));
			ret = read(fd_radar,length_buf,sizeof(length_buf));
			if(ret < 0)
			{
				printf("read radar failed.\n");
				goto fail4;
			}
			//printf("read length is %s.\n",length_buf);
			length = atoi(length_buf);
			if(length < 50)
			{
				ret = get_direction();
				if(ret == motor_l)
				{
					mada_value[0]=999;mada_value[1]=1;mada_value[2]=1;mada_value[3]=999;wt_mada(fd_mada, mada_value);	
				}
				else if(ret == motor_r)
				{
					mada_value[0]=1;mada_value[1]=999;mada_value[2]=999;mada_value[3]=1;wt_mada(fd_mada, mada_value);
				}
				else
				{
					printf("valid get_direction return.\n");
				}
				sleep(1);				
			}
			else
			{
				wt_motor(fd_motor,MOTOR_F);
				mada_value[0]=1;mada_value[1]=999;mada_value[2]=1;mada_value[3]=999;wt_mada(fd_mada, mada_value);
			}
		}
		printf("robot_mode is %d.\n", robot_mode);
	}
	
fail4:
	printf("fd_radar err.\n");
	close(fd_radar);
	for(j=0; j<i; j++)
	{
		close(fd_mada[j]);
	}
fail3:
	printf("fd_motor err.\n");
	close(fd_motor);
fail2:
	printf("fd_pt2262 err.\n");
	close(fd_pt2262);
fail1:
	return ret;
}
