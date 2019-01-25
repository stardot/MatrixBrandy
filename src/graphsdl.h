#ifndef GRAPHSDL_INC
#define GRAPHSDL_INC

extern void get_sdl_mouse(int32 values[]);
extern void warp_sdlmouse(int32 x, int32 y);
extern void sdl_mouse_onoff(int state);
extern void set_wintitle(char *title);
extern void fullscreenmode(int onoff);
extern void setupnewmode(int32 mode, int32 xres, int32 yres, int32 cols, int32 mxscale, int32 myscale, int32 xeig, int32 yeig);
extern void star_refresh(int flag);
extern int get_refreshmode(void);
extern void mode7flipbank(void);
extern void mode7renderscreen(void);
extern int32 osbyte42(int x);
extern void osbyte112(int x);
extern void osbyte113(int x);
extern int32 osbyte134_165(int32 a);
extern int32 osbyte135(void);
extern int32 osbyte250(void);
extern int32 osbyte251(void);
extern void reset_sysfont(int x);
extern void hide_cursor(void);
//extern void reveal_cursor(void);
extern void osword10(int32 x);
extern void sdl_screensave(char *fname);
extern void sdl_screenload(char *fname);
extern void reset_vdu14lines(void);
extern void swi_swap16palette(void);

#endif
