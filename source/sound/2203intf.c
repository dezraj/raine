#include <math.h>
#include "driver.h"
#include "ay8910.h"
#include "fm.h"

#include "sasound.h"
#include "timer.h"
#include "2203intf.h"
#include "streams.h"
#include "savegame.h"


static int stream[MAX_2203];

static const struct YM2203interface *intf;

static void *Timer[MAX_2203][2];

/* IRQ Handler */
static void IRQHandler(int n,int irq)
{
	if(intf->handler[n]) intf->handler[n](irq);
}

/* Timer overflow callback from timer.c */
void timer_callback_2203(int param)
{
	int n=param&0x7f;
	int c=param>>7;

	timer_remove(Timer[n][c]);
	Timer[n][c] = 0;
	YM2203TimerOver(n,c);
}

/* update request from fm.c */
void YM2203UpdateRequest(int chip)
{
	stream_update(stream[chip],0);
}

#if 0
/* update callback from stream.c */
static void YM2203UpdateCallback(int chip,void *buffer,int length)
{
	YM2203UpdateOne(chip,buffer,length);
}
#endif

static void TimerHandler(int n,int c,int count,double stepTime)
{
	if( count == 0 )
	{	/* Reset FM Timer */
		if( Timer[n][c] )
		{
	 		timer_remove (Timer[n][c]);
			Timer[n][c] = 0;
		}
	}
	else
	{	/* Start FM Timer */
		double timeSec = (double)count * stepTime;

		if( Timer[n][c] == 0 )
		{
		  Timer[n][c] = timer_set (timeSec , (c<<7)|n, timer_callback_2203 );
		}
	}
}

static void FMTimerInit( void )
{
	int i;

	for( i = 0 ; i < MAX_2203 ; i++ )
		Timer[i][0] = Timer[i][1] = 0;
	save_timers();
	AddSaveData_ext("2203 timers",(UINT8*)&Timer,sizeof(Timer));
}

int YM2203_sh_start(const struct YM2203interface *msound)
{
	int i;

	if (AY8910_sh_start_ym(msound)) return 1;

	intf = msound;;

	/* Timer Handler set */
	FMTimerInit();
	/* stream system initialize */
	for (i = 0;i < intf->num;i++)
	{
		int volume;
		char name[20];
		snprintf(name,20,"YM220 #%d FM",i);
		name[19] = 0;
		volume = intf->mixing_level[i]>>16; /* high 16 bit */
		stream[i] = stream_init(name,audio_sample_rate,16,i,YM2203UpdateOne/*YM2203UpdateCallback*/);
		/* volume setup */
		volume = intf->mixing_level[i]>>16; /* high 16 bit */
		if( volume > 255 ) volume = 255;
		stream_set_volume(stream[i],volume);
		// stream[i] = stream_initm(name,volume,audio_sample_rate,i,YM2203UpdateOne/*YM2203UpdateCallback*/);
	}
	/* Initialize FM emurator */
	if (YM2203Init(intf->num,intf->baseclock,audio_sample_rate,TimerHandler,IRQHandler) == 0)
	{
		/* Ready */
		return 0;
	}
	/* error */
	/* stream close */
	return 1;
}

void YM2203_sh_stop(void)
{
	YM2203Shutdown();
	AY8910_sh_stop_ym();
}

void YM2203_sh_reset(void)
{
	int i;

	for (i = 0;i < intf->num;i++)
		YM2203ResetChip(i);
}



READ_HANDLER( YM2203_status_port_0_r ) { return YM2203Read(0,0); }
READ_HANDLER( YM2203_status_port_1_r ) { return YM2203Read(1,0); }
READ_HANDLER( YM2203_status_port_2_r ) { return YM2203Read(2,0); }
READ_HANDLER( YM2203_status_port_3_r ) { return YM2203Read(3,0); }
READ_HANDLER( YM2203_status_port_4_r ) { return YM2203Read(4,0); }

READ_HANDLER( YM2203_read_port_0_r ) { return YM2203Read(0,1); }
READ_HANDLER( YM2203_read_port_1_r ) { return YM2203Read(1,1); }
READ_HANDLER( YM2203_read_port_2_r ) { return YM2203Read(2,1); }
READ_HANDLER( YM2203_read_port_3_r ) { return YM2203Read(3,1); }
READ_HANDLER( YM2203_read_port_4_r ) { return YM2203Read(4,1); }

WRITE_HANDLER( YM2203_control_port_0_w )
{
	YM2203Write(0,0,data);
}
WRITE_HANDLER( YM2203_control_port_1_w )
{
	YM2203Write(1,0,data);
}
WRITE_HANDLER( YM2203_control_port_2_w )
{
	YM2203Write(2,0,data);
}
WRITE_HANDLER( YM2203_control_port_3_w )
{
	YM2203Write(3,0,data);
}
WRITE_HANDLER( YM2203_control_port_4_w )
{
	YM2203Write(4,0,data);
}

WRITE_HANDLER( YM2203_write_port_0_w )
{
	YM2203Write(0,1,data);
}
WRITE_HANDLER( YM2203_write_port_1_w )
{
	YM2203Write(1,1,data);
}
WRITE_HANDLER( YM2203_write_port_2_w )
{
	YM2203Write(2,1,data);
}
WRITE_HANDLER( YM2203_write_port_3_w )
{
	YM2203Write(3,1,data);
}
WRITE_HANDLER( YM2203_write_port_4_w )
{
	YM2203Write(4,1,data);
}

WRITE_HANDLER( YM2203_word_0_w )
{
	if (offset)
		YM2203_write_port_0_w(0,data);
	else
		YM2203_control_port_0_w(0,data);
}

WRITE_HANDLER( YM2203_word_1_w )
{
	if (offset)
		YM2203_write_port_1_w(0,data);
	else
		YM2203_control_port_1_w(0,data);
}

int YM2203_get_stream_num( int num ){
  return stream[num];
}
