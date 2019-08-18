#ifndef SOUND_SDL_H_INC

#define SOUND_SDL_H_INC

extern void init_sound(void);

extern void sdl_sound(int32, int32, int32, int32, int32);
extern void sdl_sound_onoff(int32);

extern void  sdl_wrbeat(int32);
extern int32 sdl_rdbeat(void);
extern int32 sdl_rdbeats(void);
extern void  sdl_wrtempo(int32);
extern int32 sdl_rdtempo(void);
extern void  sdl_voice(int32, char *);
extern void  sdl_voices(int32);
extern void  sdl_star_voices(void);
extern void  sdl_stereo(int32, int32);
extern void  sdl_volume(int32);

#endif
