/*
** This file is part of the Brandy Basic VI Interpreter.
** soundsdl.c by David Hawes.
**
*/

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <SDL.h>
#include <SDL_audio.h>
#include "basicdefs.h"
#include "target.h"
#include "mos.h"
#include "screen.h"
#include "soundsdl.h"

/*

Middle C is 261.63 Hz - A above middle C is 440Hz (432Hz)

The pitch number 53 is the number for middle C. Pitch is represented by a number from 1 to 255, as follows:

              Octave number

   Note   1    2     3     4     5     6

   A          41    89   137   185   233
   A#         45    93   141   189   237
   B      1   49    97   145   193   241
   C      5   53   101   149   197   245
   C#     9   57   105   153   201   249
   D     13   61   109   157   205   253
   D#    17   65   113   161   209
   E     21   69   117   165   213
   F     25   73   121   169   217
   F#    29   77   125   173   221
   G     33   81   129   177   225
   G#    37   85   133   181   229

Octave 2 is the one containing middle C.

It is also possible to represent pitch by a number from 0x100 (256) to 32767 (0x7FFF), in which case middle C is 0x4000.
*/


static int snd_nvoices=1;
static int snd_beats=0;
static int snd_tempo=0;
static unsigned int snd_tempo_basetime=0;
static int snd_ison=0;
static int snd_volume=127;

static unsigned int snd_inited = 0;
static int snd_paused = 0;

static SDL_AudioSpec desiredSpec;


typedef struct sndent {
  signed int count;
  unsigned short int step;
  unsigned char vol, chant;
} sndent;


#define SNDTABWIDTH 64

static sndent sndtab[8][SNDTABWIDTH];

static unsigned char snd_rd[8],snd_wr[8];
static unsigned short int soffset[8], *poffset;
static int sactive=0;
static unsigned char ssl[8],ssr[8];
static const unsigned char chantype[10]={0,0,4,1,1,5,2,2,2,3};
static unsigned char chanvoice[8];
static unsigned char sintab[1025];
static unsigned int steptab[312];
static unsigned int sndtime[8];

static void audio_callback(void *unused, Uint8 *ByteStream, int Length) {
  /* Length is length of buffer in bytes */
  int i,vl,vr,ilen,tmp,s;
  unsigned int *ptr;
  int cm1,bit;
  sndent *snd, *tptr;

  ilen = (Length)>>2;
  ptr = (unsigned int*)ByteStream;
  for(i=0; i<ilen; i++)
    ptr[i]=0x80808080;

  if(sactive == 0){
    SDL_PauseAudio(1);
    snd_paused = 1;
    return;
  } else {
    for(bit=1, cm1=0; cm1 < snd_nvoices; cm1++, bit<<=1) {
      if(sactive & bit) {
        snd = & sndtab[cm1][snd_rd[cm1]];
        poffset = & soffset[cm1];

        s = (snd->vol)*snd_volume;
        vl = s >> (7 + ssl[cm1]);
        vr = s >> (7 + ssr[cm1]);

        if((vl>0 || vr>0) && snd->step>0 )
        switch(snd->chant) {

          case 0 : /* WaveSynth beep :- sine wave */
            for(i=0; i<Length; i++){
              *poffset += snd->step;
              s = sintab[(*poffset)>>6]-128;

              tmp=((int)ByteStream[i])+((vl*s)>>7);
              if(tmp<0)tmp=0;else if(tmp>255)tmp=255;
              ByteStream[i] = tmp;

              i++;

              tmp=((int)ByteStream[i])+((vr*s)>>7);
              if(tmp<0)tmp=0;else if(tmp>255)tmp=255;
              ByteStream[i] = tmp;
    
            }
          break;

          case 1 : /* stringlib :- square wave */
            for(i=0; i<Length; i++) {
              *poffset += snd->step;

              if(*poffset & 0x8000) {
                tmp = ByteStream[i] + vl;
                if(tmp>255)tmp=255;
                ByteStream[i] = tmp;

                i++;

                tmp = ByteStream[i] + vr;
                if(tmp>255)tmp=255;
                ByteStream[i] = tmp;
              } else {
                tmp = ByteStream[i] - vl;
                if(tmp < 0) tmp= 0;
                ByteStream[i] = tmp;

                i++;
                tmp = ByteStream[i] - vr;
                if(tmp < 0) tmp= 0;
                ByteStream[i] = tmp;
              }
            }
          break;
  
          case 2 : /* percussion :- square wave with vibrato */
            for(i=0; i<Length; i++) {
              *poffset += snd->step;
              if(i & 0x100){
                i += 0xff;
                *poffset += (((snd->step)<<7) - snd->step); 
              } else {
                if(*poffset & 0x8000) {
                  tmp = ByteStream[i] + vl;
                  if(tmp>255)tmp=255;
                  ByteStream[i] = tmp;

                  i++;

                  tmp = ByteStream[i] + vr;
                  if(tmp>255)tmp=255;
                  ByteStream[i] = tmp;
                } else {
                  tmp = ByteStream[i] - vl;
                  if(tmp < 0) tmp= 0;
                  ByteStream[i] = tmp;

                  i++;

                  tmp = ByteStream[i] - vr;
                  if(tmp < 0) tmp= 0;
                  ByteStream[i] = tmp;
                }
              }
            }
          break;

          case 3 : /* Percussion noise :-  pink noise */
          {
            static unsigned int rnd=0x1b3;
            int step=1,mask=2047,m;

            while(mask > snd->step) {
              mask >>= 1;
            }
            m = mask>>1;

            for(i=0; i<Length; i++){
              if((i & 63) == 0 ) {
               step = snd->step + (rnd & mask) - m;
               if( step < 1) step = 5;
               rnd += (rnd>>3)+1;
               rnd += (rnd<<4)+1;
              }

              *poffset += step;

              if(*poffset & 0x8000) {
                tmp = ByteStream[i] + vl;
                if(tmp>255)tmp=255;
                ByteStream[i] = tmp;

                i++;

                tmp = ByteStream[i] + vr;
                if(tmp>255)tmp=255;
                ByteStream[i] = tmp;
              } else {
                tmp = ByteStream[i] - vl;
                if(tmp < 0) tmp= 0;
                ByteStream[i] = tmp;

               i++;

                tmp = ByteStream[i] - vr;
                if(tmp < 0) tmp= 0;
                ByteStream[i] = tmp;
              }
            }
          }
          break;

          case 4: /*  triangle wave */
            for(i=0; i<Length; i++) {
              *poffset += snd->step;
              s = *poffset;
              if(s >= 32768) s= 65535 -s;
              s -= 16384;

              tmp=((int)ByteStream[i])+((vl*s)>>14);
              if(tmp>255)tmp=255;else if(tmp<0) tmp=0;
              ByteStream[i] = tmp;

              i++;

              tmp=((int)ByteStream[i])+((vr*s)>>14);
              if(tmp>255)tmp=255;else if(tmp<0) tmp=0;
              ByteStream[i] = tmp;
            }
          break;

          case 5: /*  saw tooth wave */
    
           for(i=0; i<Length; i++){
             *poffset += snd->step;
             s = (*poffset) - 32768;
    
             tmp=((int)ByteStream[i])+((vl*s)>>15);
             if(tmp>255)tmp=255;else if(tmp<0) tmp=0;
             ByteStream[i] = tmp;
    
             i++;
    
             tmp=((int)ByteStream[i])+((vr*s)>>15);
             if(tmp>255)tmp=255;else if(tmp<0) tmp=0;
             ByteStream[i] = tmp;
          }
          break;
    
          default:
          break;
        }
        snd->count -= Length;
        if(snd->count <= 0) {
          snd->count = 0;
          snd_rd[cm1] = (snd_rd[cm1]+1)&(SNDTABWIDTH-1); /* move to next sound in list */
          tptr= & sndtab[cm1][snd_rd[cm1]];
          if( tptr->count <= 0) { /* deactivate this channel if the next entry is empty */
            sactive &= ~bit;
          }
        }
        /* pause sound system if all channels are inactive */
        if( (sactive & ((1<<snd_nvoices)-1)) == 0) {
          sactive = 0;
          SDL_PauseAudio(1);
          snd_paused = 1;
        }
      }
    }
  }
}

static void clear_sndtab() {
  int i;

  memset(sndtab, 0, sizeof(sndtab));
  for (i=0; i< 8;i++) {
    snd_rd[i]=1;
    snd_wr[i]=0;
  }
}

void init_sound(){
  int s,i,rv;
  double fhz;

#ifdef DEBUG
  if (basicvars.debug_flags.sound) fprintf(stderr,"init_sound called\n");
#endif

  SDL_InitSubSystem(SDL_INIT_AUDIO);

  desiredSpec.freq     = 20480;
  desiredSpec.format   = AUDIO_U8;
  desiredSpec.channels = 2;
  desiredSpec.samples  = 2048;
  desiredSpec.callback = audio_callback;
  desiredSpec.userdata = (void*)0;

  rv=SDL_OpenAudio(&desiredSpec, (SDL_AudioSpec *)0);
  if(rv < 0){ 
    fprintf(stderr,"init_sound: Failed to open audio device\n");
    snd_inited = 0;
    snd_ison = 0;
    return;
  }

  snd_inited = (unsigned int)basicvars.centiseconds;

  for(i=0; i<8; i++){
    /* init all voices as 'synth wave' */
    chanvoice[i]=1;
    /* stereo centred */
    ssl[i]=0;
    ssr[i]=0;
    sndtime[i] = 0;
  }

  /* init sintab */
  for(i=0; i<=256; i++){
    s=(int)floor(128.0+(127.5*sin(((double)i)*M_PI*1.0/512.0)));
    sintab[i]     = s;
    sintab[512-i] = s;
    sintab[512+i] = 255-s;
    sintab[1024-i]= 255-s;
  }

/* init step tab */
  for(i=255;i<312;i++){
    fhz = 440.0*pow(2.0, ((double)(i-89))*(1.0/48.0));
    steptab[i] = (unsigned int)floor((fhz * (((double)0xffffffffu)/20480.0))+0.5);

#ifdef DEBUG
  if (basicvars.debug_flags.sound) fprintf(stderr,"fhz is %12.4f steptab[%3d] is %9u\n",fhz,i,steptab[i]);
#endif
  }
  for(i=254; i>=0; i--){
    steptab[i] = steptab[i+48] >> 1;

#ifdef DEBUG
    if (basicvars.debug_flags.sound) fprintf(stderr,"steptab[%3d] is %9u\n",i,steptab[i]);
#endif
  }

  clear_sndtab();
  for(i=0;i<8;i++) soffset[i]=0;
  SDL_Delay(40); /* Allow time for sound system to start. */
  SDL_PauseAudio(1);

  snd_paused = 1;
  snd_ison = 1;

  snd_tempo = 0;
  snd_beats = 0;
  sdl_voices(4);
}

void sdl_sound(int32 channel, int32 amplitude, int32 pitch, int32 duration, int32 delay){
// channel &0xxx - sound generator  &000h ssss xxxf cccc  hold, sync, flush, channel
// channel &1xxx - sound generator  &000h xxxx xxxx xxxx  hold, rest ignored
// channel &20xx - Watford speech
// channel &21xx
//      to &FDxx - other things
// channel &FExx - MIDI control
// channel &FFxx - BBC speech

  unsigned int step;
  int tvol;
  int cht;
  int cm1;
  sndent *snd;

  double e,f;
  int t,diff,pl;

  unsigned int tnow=0;

  if(channel & 0xE000) return;
  if(!snd_inited) init_sound();
  if(!snd_ison ) return;

  channel &= 31;
  if(duration <= 0 || channel < 1 || channel > snd_nvoices) return;

  cm1 = channel-1;

  if(pitch >  25766 ) pitch= 25766;
  if(pitch < -10240 ) pitch=-10240;

  if(pitch < 0 ) {
    step = (((-pitch)<< 16)+10240)/20480;
  } else if(pitch < 256) {
    step = steptab[pitch] >> 16; 
  } else {
    e= (((double)(pitch-0x1c00))*(48.0/4096.0))+89.0;
    f=floor(e);
    e -= f;
    t=(int)f;
    diff =  floor(0.5 + (1.0/65536.0)*e*((double)(steptab[t+1] - steptab[t])));
    step = (steptab[t] >> 16) + diff;
#ifdef DEBUG
    if (basicvars.debug_flags.sound) fprintf(stderr,"t is %3d step is %d e is %6.3f diff is %5d\n",t, step, e, diff);
#endif
  }

  if(step > 32767) step = 32767;

#ifdef DEBUG
  if (basicvars.debug_flags.sound) fprintf(stderr,"sdl_sound called: cm1 (%2d) amplitude (%3d) pitch (%5d) duration (%3d) delay (%d) step is %d\n",cm1, amplitude, pitch, duration, delay, step);

  if (basicvars.debug_flags.sound) fprintf(stderr,"sdl_sound: step is %d delay is %d is_on %d paused %d\n",step, delay, snd_ison, snd_paused);
#endif

  tvol= 0;
  if (amplitude < -15) amplitude= -15;
  else if(amplitude > 383) amplitude= 383;

  if(amplitude < 0 && amplitude >= -15) tvol = (1-amplitude)<<3;
  else if(amplitude >=256 && amplitude <= 383) tvol= ((t=((amplitude-255)*3)-77)<0) ? steptab[t+96] >> 26 : steptab[t] >> 24;
  else tvol = amplitude >> 1;

  cht=chantype[chanvoice[cm1]];

  if(duration > 32768) duration = 32768;

  if( snd_tempo > 0 && snd_beats > 1 && delay > 0 ) {
    int beat = sdl_rdbeat();

    if (delay <= beat || delay >= snd_beats) {
      delay = -1;
    } else {
      delay = ((delay - beat) << 12 ) / (snd_tempo*5) ; /* 5 to convert centiseconds into 20th */
    }
  }

  if(delay > 32768) delay = 32768;
#ifdef DEBUG
  if (basicvars.debug_flags.sound) fprintf(stderr,"sdl_sound tvol %3d step is %5d snd_wr[%d] = %2d snd_rd[%d] = %2d sndtime[%d] %4d tnow %4d sactive %2x\n", tvol, step, cm1, snd_wr[cm1], cm1, snd_rd[cm1], cm1, sndtime[cm1], tnow, sactive);
#endif

  if(!step) tvol = 0;

  if(delay)while(((snd_rd[cm1]-snd_wr[cm1]-2)&(SNDTABWIDTH-1)) <= 2) usleep(50000);

  tnow = ((unsigned int)basicvars.centiseconds - snd_inited )/5; /* divide by 5 to covert centiseconds to 20ths */

  if(sndtime[cm1] < tnow )
     sndtime[cm1] = tnow;

  SDL_LockAudio();

  if(delay > 0  &&  (pl = tnow+delay-sndtime[cm1]) > 0 ){

    snd_wr[cm1] = (snd_wr[cm1]+1)&(SNDTABWIDTH-1);

    snd = &sndtab[cm1][snd_wr[cm1]];

    snd->step    = 0; /* play silence during delay */
    snd->count   = pl << 11;
    snd->vol     = 0;
    snd->chant   = 0;

    sndtime[cm1] += pl;

    delay = -1;
  } else {
    snd = &sndtab[cm1][snd_wr[cm1]];
  }

  if ((delay != 0) || (snd->count == 0) ){
    if( delay > 0 && snd_wr[cm1] == snd_rd[cm1] && snd->count > (delay << 11) ) {
      snd->count = (delay << 11);
      sndtime[cm1] = tnow + delay;
    }
    /* move to next entry */
    snd_wr[cm1] = (snd_wr[cm1]+1)&(SNDTABWIDTH-1);
    snd = &sndtab[cm1][snd_wr[cm1]];

    sndtime[cm1] += duration;

  } else { 
    if ( delay == 0 ) {
     int r;
     r = snd_rd[cm1];
     snd = & sndtab[cm1][r]; /* over write playing entry */
     snd_wr[cm1] = r;
     sndtime[cm1] = tnow + duration;
    } else sndtime[cm1] += duration;
  }

  snd->step    = step;
  snd->count   = duration << 11;
  snd->vol     = tvol;
  snd->chant   = cht;

  sndtab[cm1][(snd_wr[cm1]+1) & (SNDTABWIDTH-1)].count = 0; /* clear next entry */

  sactive |= (1 << cm1);

  SDL_UnlockAudio();

#ifdef DEBUG
  if (basicvars.debug_flags.sound) fprintf(stderr,"sdl_sound: step is %d cm1 %d type %d tvol %d sactive %d\n",step, cm1, cht, tvol, sactive);
#endif

  if( snd_ison && snd_paused){
   SDL_PauseAudio(0);
   snd_paused = 0;
  } 
}

void sdl_sound_onoff(int32 onoff){
#ifdef DEBUG
  if (basicvars.debug_flags.sound) fprintf(stderr, "sdl_sound_onoff(%d) called ison %d paused %d \n",onoff, snd_ison, snd_paused);
#endif

  if(onoff && !snd_ison ) {
    if(!snd_inited) init_sound();
    snd_ison = 1;
  } else if ( !onoff && snd_ison) {
    SDL_LockAudio();
    clear_sndtab();
    SDL_UnlockAudio();

    snd_ison = 0;
    SDL_PauseAudio(1);
    snd_paused = 1;
  }
}

void sdl_wrbeat(int32 beats){
  if(!snd_inited) init_sound();

  if( beats < 0) beats = 0;

  snd_beats = beats;
  snd_tempo_basetime = ((unsigned int)basicvars.centiseconds - snd_inited );
}

int32 sdl_rdbeat(){
  int beat;

  if(!snd_inited) init_sound();

  if( snd_beats <= 1 || snd_tempo <= 0)
    return 0;

  beat = ((  ((unsigned int)basicvars.centiseconds - snd_inited ) - snd_tempo_basetime ) * snd_tempo ) >> 12;
 
  if( beat <= 0 ) return 0;

  if ( beat >= snd_beats )
    beat = (beat % snd_beats);

  return beat;
}

int32 sdl_rdbeats() {
  if(!snd_inited) init_sound();
  return snd_beats;
}

void sdl_wrtempo(int32 tempo) {
  if(!snd_inited) init_sound();

  if(tempo < 0) tempo = 0;

  snd_tempo = tempo;
  snd_tempo_basetime =((unsigned int)basicvars.centiseconds - snd_inited );
}

int32 sdl_rdtempo() {
  if(!snd_inited) init_sound();
  return snd_tempo;
}

static char *voicetab[10]={
  "",
  "WaveSynth-Beep",
  "StringLib-Soft",
  "StringLib-Pluck",
  "StringLib-Steel",
  "StringLib-Hard",
  "Percussion-Soft",
  "Percussion-Medium",
  "Percussion-Snare",
  "Percussion-Noise"
};

void sdl_voice(int32 channel, char *name) {
  int i,ch, n;

#ifdef DEBUG
  if (basicvars.debug_flags.sound) fprintf(stderr,"sdl_voice called: channel (%d) name \"%s\"\n",channel, name);
#endif

  if(!snd_inited) init_sound();

  n=0;
  if ( (ch= *name) >= '1' && ch <= '9'){
    n= ch-'0';
  } else {
    for(i=1;i<=9;i++)
      if( strncmp(name,voicetab[i], strlen(name)+1) == 0) {
        n = i;
        break;
      }
  }
  if(channel >=1 && channel <=8 && n>=1 && n<=9) chanvoice[channel-1] = n;

#ifdef DEBUG
  if (basicvars.debug_flags.sound) fprintf(stderr,"sdlvoice - channel number is %d\n",n);
#endif
}
 
/*
*voice
            Voice      Name
   1          1   WaveSynth-Beep
              2   StringLib-Soft
              3   StringLib-Pluck
              4   StringLib-Steel
              5   StringLib-Hard
              6   Percussion-Soft
              7   Percussion-Medium
              8   Percussion-Snare
              9   Percussion-Noise
*/

void sdl_star_voices() {
  int i,v;
 
  if(!snd_inited) init_sound();
  emulate_printf("        Voice      Name\r\n");
  for(i=1;i<=9;i++) {
    for(v=1;v<=8;v++) {
      if(v <= snd_nvoices && chanvoice[v-1] == i)
        emulate_printf("%d",v);
      else
        emulate_printf(" ");
    }
    emulate_printf(" %d %s\r\n", i, voicetab[i]);
  }
  emulate_printf("^^^^^^^^  Channel Allocation Map\r\n");
}

void  sdl_voices(int32 channels) {
  int i,c,n;

  if(!snd_inited) init_sound();

  n=8;
  for(i=1;i<=4;i+=i) {
    if(i >= channels) {
      n = i;
      break;
    }
  }
  if( n >= 1 && n <= 8 ) {
    snd_nvoices = n;
    /* if nvoices is reduced then the sndtab entries need to be cleared or they will be played if it is increased later */
    SDL_LockAudio();
    sactive &= ((1<snd_nvoices)-1);
    for(c = n; c < 8; c++) {
      snd_rd[c] = 1;
      snd_wr[c] = 0;
      sndtime[c]  = 0;
      for(i = 0; i < SNDTABWIDTH; i++) {
        sndtab[c][i].count = 0;
        sndtab[c][i].vol   = 0;
      }
    }
    SDL_UnlockAudio();
  }
}

void sdl_stereo(int32 channel, int32 position) {
  int cm1;
  if(!snd_inited) init_sound();
  cm1 = channel-1;
/*
 -127 to -80 full left
  -79 to -48  2/3 left
  -47 to -16  1/3 left
  -15 to +15 Centre

*/
  /* centre - both channels full volume */
  ssl[cm1] = 0;
  ssr[cm1] = 0;

  if(position < 0){
    /* left full and right reduced */
    if ( position <= -80 ) { ssr[cm1]= 8; return;}
    if ( position <= -48 ) { ssr[cm1]= 2; return;}
    if ( position <= -16 ) { ssr[cm1]= 1; return;}
  } else {
    /* right full and left reduced */
    if ( position >=  80 ) { ssl[cm1]= 8; return;}
    if ( position >=  48 ) { ssl[cm1]= 2; return;}
    if ( position >=  16 ) { ssl[cm1]= 1; return;}
  }
}

void sdl_volume(int32 vol) {
  if(!snd_inited) init_sound();

  if(vol <   0) vol = 0;
  if(vol > 127) vol = 127;
  snd_volume = vol;
}
