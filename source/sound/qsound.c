/***************************************************************************

  Capcom System QSound(tm)
  ========================

  Driver by Paul Leaman (paul@vortexcomputing.demon.co.uk)
		and Miguel Angel Horna (mahorna@teleline.es)

  A 16 channel stereo sample player.

  QSpace position is simulated by panning the sound in the stereo space.

  Register
  0	 xxbb	xx = unknown bb = start high address
  1	 ssss	ssss = sample start address
  2	 pitch
  3	 unknown (always 0x8000)
  4	 loop offset from end address
  5	 end
  6	 master channel volume
  7	 not used
  8	 Balance (left=0x0110  centre=0x0120 right=0x0130)
  9	 unknown (most fixed samples use 0 for this register)

  Many thanks to CAB (the author of Amuse), without whom this probably would
  never have been finished.

  If anybody has some information about this hardware, please send it to me
  to mahorna@teleline.es or 432937@cepsz.unizar.es.
  http://teleline.terra.es/personal/mahorna

***************************************************************************/

#include <math.h>
#include "sasound.h"
#include "loadroms.h"
#include "qsound.h"
#include "driver.h"
#include <string.h> // memset
#include "mz80help.h"
#include "savegame.h"
#include "streams.h"

/*
Two Q sound drivers:
DRIVER1 Based on the Amuse source
DRIVER2 Miguel Angel Horna (mahorna@teleline.es)
*/
#define QSOUND_DRIVER1	  1
/*
I don't know whether this system uses 8 bit or 16 bit samples.
If it uses 16 bit samples then the sample ROM loading macros need
to be modified to work with non-intel machines.
*/
#define QSOUND_8BIT_SAMPLES 1

/*
Debug defines
*/
#define LOG_QSOUND  0

/* Typedefs & defines */

#define QSOUND_DRIVER2 !QSOUND_DRIVER1

/* 8 bit source ROM samples */
typedef signed char QSOUND_SRC_SAMPLE;

#define QSOUND_CLOCKDIV 166			 /* Clock divider */
#define QSOUND_CHANNELS 16
typedef INT16 QSOUND_SAMPLE;

struct QSOUND_CHANNEL
{
	int bank;	   /* bank (x16)	*/
	int address;	/* start address */
	int pitch;	  /* pitch */
	int reg3;	   /* unknown (always 0x8000) */
	int loop;	   /* loop address */
	int end;		/* end address */
	int vol;		/* master volume */
	int pan;		/* Pan value */
	int reg9;	   /* unknown */

	/* Work variables */
	int key;		/* Key on / key off */

#if QSOUND_DRIVER1
	int lvol;	   /* left volume */
	int rvol;	   /* right volume */
	int lastdt;	 /* last sample value */
	int offset;	 /* current offset counter */
#else
	int factor;		   /*step factor (fixed point 8-bit)*/
	int mixl,mixr;		/*mixing factor (fixed point)*/
	int cursor;		   /*current sample position (fixed point)*/
	int lpos;			 /*last cursor pos*/
	int lastsaml;		 /*last left sample (to avoid any calculation)*/
	int lastsamr;		 /*last right sample*/
	QSOUND_SRC_SAMPLE *buffer;
#endif
};


/* Private variables */
static int qsound_stream;				/* Audio stream */
static struct QSOUND_CHANNEL qsound_channel[QSOUND_CHANNELS];
static int qsound_data;				  /* register latch data */
static QSOUND_SRC_SAMPLE *qsound_sample_rom;	/* Q sound sample ROM */
static int qsound_sample_rom_length;

#if QSOUND_DRIVER1
static int qsound_pan_table[33];		 /* Pan volume table */
static float qsound_frq_ratio;		   /* Frequency ratio */
#endif

/* Function prototypes */
void qsound_update( int num, INT16 **buffer, int length );
void qsound_set_command(int data, int value,int factor);

#if QSOUND_DRIVER2
void setchannel(int channel,signed short *buffer,int length,int vol,int pan);
void setchloop(int channel,int loops,int loope);
void stopchan(int channel);
void calcula_mix(int channel);
#endif

int qsound_sh_start(const struct QSound_interface *intf)
{
  if (audio_sample_rate == 0) return 0;

  qsound_sample_rom = (QSOUND_SRC_SAMPLE *)load_region[intf->region];
  qsound_sample_rom_length = get_region_size(intf->region);

  memset(qsound_channel, 0, sizeof(qsound_channel));

#if QSOUND_DRIVER1
  int i;
  qsound_frq_ratio = ((float)intf->clock / (float)QSOUND_CLOCKDIV) /
    (float) audio_sample_rate;
  qsound_frq_ratio *= 16.0;

  /* Create pan table */
  for (i=0; i<33; i++)
    {
      qsound_pan_table[i]=(int)((256/sqrt(32)) * sqrt(i));
    }
#endif

#if LOG_QSOUND
  logerror("Pan table\n");
  for (i=0; i<33; i++)
    logerror("%02x ", qsound_pan_table[i]);
#endif
  {
    /* Allocate stream */
#define CHANNELS ( 2 )
    char buf[CHANNELS][40];
    const char *name[CHANNELS];
    int  vol[2];
    name[0] = buf[0];
    name[1] = buf[1];
    sprintf( buf[0], "%s L", "qsound" );
    sprintf( buf[1], "%s R", "qsound" );
    vol[0]=MIXER(intf->mixing_level[0], MIXER_PAN_LEFT);
    vol[1]=MIXER(intf->mixing_level[1], MIXER_PAN_RIGHT);
    qsound_stream = stream_init_multim(
				       CHANNELS,
				       name,
				       vol,
				       audio_sample_rate,
				       0,
				       qsound_update );
  }

#if QSOUND_DRIVER1
  AddSaveData(ASCII_ID('Q','S','N','D'),
	      (UINT8*)&qsound_channel,
	      sizeof(qsound_channel));
#else
  AddSaveData(ASCII_ID('Q','S','N','D'),
	      (UINT8*)&qsound_channel,
	      sizeof(qsound_channel)-sizeof(QSOUND_SRC_SAMPLE *));
#endif
  return 0;
}

void qsound_sh_stop (void)
{
	if (audio_sample_rate == 0) return;
}

WRITE_HANDLER( qsound_data_h_w )
{
  // if (data) fprintf(stderr,"qh %x\n",data);
	qsound_data=(qsound_data&0xff)|(data<<8);
}

WRITE_HANDLER( qsound_data_l_w )
{
  // if (data) fprintf(stderr,"ql %x\n",data);
	qsound_data=(qsound_data&0xff00)|data;
}

WRITE_HANDLER( qsound_cmd_w1 )
{
	qsound_set_command(data, qsound_data,1);
}

WRITE_HANDLER( qsound_cmd_w3 )
{
  // fprintf(stderr,"qsound cmd %x [%x]\n",data,z80pc);
	qsound_set_command(data, qsound_data,3);
}

READ_HANDLER( qsound_status_r )
{
	/* Port ready bit (0x80 if ready) */
	return 0x80;
}

void qsound_set_command(int data, int value, int factor)
{
	int ch=0,reg=0;
	if (data < 0x80)
	{
		ch=data>>3;
		reg=data & 0x07;
	}
	else
	{
		if (data < 0x90)
		{
			ch=data-0x80;
			reg=8;
		}
		else
		{
			if (data >= 0xba && data < 0xca)
			{
				ch=data-0xba;
				reg=9;
			}
			else
			{
				/* Unknown registers */
				ch=99;
				reg=99;
			}
		}
	}

	switch (reg)
	{
		case 0: /* Bank */
			ch=(ch+1)&0x0f;	/* strange ... */
			qsound_channel[ch].bank=(value&0xff)<<16; // sfz3mix requires ff here instead of the default 7f
#ifdef MAME_DEBUG
			if (!value & 0x8000)
			{
				char baf[40];
				sprintf(baf,"Register3=%04x",value);
				usrintf_showmessage(baf);
			}
#endif

			break;
		case 1: /* start */
			qsound_channel[ch].address=value;
			break;
		case 2: /* pitch */
#if QSOUND_DRIVER1
			qsound_channel[ch].pitch=(long)
					((float)value * qsound_frq_ratio );
#else
			qsound_channel[ch].factor=((float) (value*(6)) /
									  (float) audio_sample_rate)*256.0;

#endif
			if (!value)
			{
				/* Key off */
				qsound_channel[ch].key=0;
			}
			break;
		case 3: /* unknown */
			qsound_channel[ch].reg3=value;
#ifdef MAME_DEBUG
			if (value != 0x8000)
			{
				char baf[40];
				sprintf(baf,"Register3=%04x",value);
				usrintf_showmessage(baf);
			}
#endif
			break;
		case 4: /* loop offset */
			qsound_channel[ch].loop=value;
			break;
		case 5: /* end */
			qsound_channel[ch].end=value;
			break;
		case 6: /* master volume */
			if (value==0)
			{
				/* Key off */
				qsound_channel[ch].key=0;
			}
			else if (qsound_channel[ch].key==0)
			{
				/* Key on */
				qsound_channel[ch].key=1;
#if QSOUND_DRIVER1
				qsound_channel[ch].offset=0;
				qsound_channel[ch].lastdt=0;
#else
				qsound_channel[ch].cursor=qsound_channel[ch].address <<8 ;
				qsound_channel[ch].buffer=qsound_sample_rom+
										 qsound_channel[ch].bank;
#endif
			}
			qsound_channel[ch].vol=value*factor;
#if QSOUND_DRIVER2
			calcula_mix(ch);
#endif
			break;

		case 7:  /* unused */
#ifdef MAME_DEBUG
			{
				char baf[40];
				sprintf(baf,"UNUSED QSOUND REG 7=%04x",value);
				usrintf_showmessage(baf);
			}
#endif

			break;
		case 8:
			{
#if QSOUND_DRIVER1
			   int pandata=(value-0x10)&0x3f;
			   if (pandata > 32)
			   {
					pandata=32;
			   }
			   qsound_channel[ch].rvol=qsound_pan_table[pandata];
			   qsound_channel[ch].lvol=qsound_pan_table[32-pandata];
#endif
			   qsound_channel[ch].pan = value;
#if QSOUND_DRIVER2
			   calcula_mix(ch);
#endif
			}
			break;
		 case 9:
			qsound_channel[ch].reg9=value;
/*
#ifdef MAME_DEBUG
			{
				char baf[40];
				sprintf(baf,"QSOUND REG 9=%04x",value);
				usrintf_showmessage(baf);
			}
#endif
*/
			break;
	}
#if LOG_QSOUND
	logerror("QSOUND WRITE %02x CH%02d-R%02d =%04x\n", data, ch, reg, value);
#endif
}

#if QSOUND_DRIVER1

/* Driver 1 - based on the Amuse source */

void qsound_update( int num, INT16 **buffer, int length )
{
	int i,j;
	int rvol, lvol, count;
	struct QSOUND_CHANNEL *pC=&qsound_channel[0];
	QSOUND_SAMPLE  *datap[2];

	if (audio_sample_rate == 0) return;

	datap[0] = buffer[0];
	datap[1] = buffer[1];
	memset( datap[0], 0x00, length * sizeof(QSOUND_SAMPLE) );
	memset( datap[1], 0x00, length * sizeof(QSOUND_SAMPLE) );

	for (i=0; i<QSOUND_CHANNELS; i++)
	{
		if (pC->key)
		{
			QSOUND_SAMPLE *pOutL=datap[0];
			QSOUND_SAMPLE *pOutR=datap[1];
			rvol=(pC->rvol*pC->vol)>>8;
			lvol=(pC->lvol*pC->vol)>>8;

			for (j=length-1; j>=0; j--)
			{
				count=(pC->offset)>>16;
				pC->offset &= 0xffff;
				if (count)
				{
					pC->address += count;
					if (pC->address >= pC->end)
					{
						if (!pC->loop)
						{
							/* Reached the end of a non-looped sample */
							pC->key=0;
							break;
						}
						/* Reached the end, restart the loop */
						pC->address = (pC->end - pC->loop) & 0xffff;
					}
					// pC->lastdt=pST[pC->address];
					pC->lastdt=qsound_sample_rom[(pC->bank+pC->address)%(qsound_sample_rom_length)];
				}

				(*pOutL) += ((pC->lastdt * lvol) >> 6);
				(*pOutR) += ((pC->lastdt * rvol) >> 6);
				pOutL++;
				pOutR++;
				pC->offset += pC->pitch;
			}
		}
		pC++;
	}

}

#else

/* ----------------------------------------------------------------
		QSound Sample Mixer (Slow)
		Miguel Angel Horna mahorna@teleline.es

 ------------------------------------------------------------------ */


void calcula_mix(int channel)
{
	int factl,factr;
	struct QSOUND_CHANNEL *pC=&qsound_channel[channel];
	int vol=pC->vol>>5;
	int pan=((pC->pan&0xFF)-0x10)<<3;
	pC->mixl=vol;
	pC->mixr=vol;
	factr=pan;
	factl=255-factr;
	pC->mixl=(pC->mixl * factl)>>8;
	pC->mixr=(pC->mixr * factr)>>8;
#if QSOUND_8BIT_SAMPLES
	pC->mixl<<=8;
	pC->mixr<<=8;
#endif
}

void qsound_update(int num,INT16 **buffer,int length)
{
	int i,j;
	QSOUND_SAMPLE *bufL,*bufR, sample;
	struct QSOUND_CHANNEL *pC=qsound_channel;

	memset( buffer[0], 0x00, length * sizeof(QSOUND_SAMPLE) );
	memset( buffer[1], 0x00, length * sizeof(QSOUND_SAMPLE) );

	for(j=0;j<QSOUND_CHANNELS;++j)
	{
		  bufL=(QSOUND_SAMPLE *) buffer[0];
		  bufR=(QSOUND_SAMPLE *) buffer[1];
		  if(pC->key)
		  {
				for(i=0;i<length;++i)
				{
						   int pos=pC->cursor>>8;
						   if(pos!=pC->lpos)	/*next sample*/
						   {
								sample=pC->buffer[pos];
								pC->lastsaml=(sample*pC->mixl)>>8;
								pC->lastsamr=(sample*pC->mixr)>>8;
								pC->lpos=pos;
						   }
						   (*bufL++)+=pC->lastsaml;
						   (*bufR++)+=pC->lastsamr;
						   pC->cursor+=pC->factor;
						   if(pC->loop && (pC->cursor>>8) > pC->end)
						   {
								 pC->cursor=(pC->end-pC->loop)<<8;
						   }
						   else if((pC->cursor>>8) > pC->end)
								   pC->key=0;
				 }
		  }
		  pC++;
	 }
}
#endif


/**************** end of file ****************/
