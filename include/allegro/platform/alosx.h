/*         ______   ___    ___ 
 *        /\  _  \ /\_ \  /\_ \ 
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___ 
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *      MacOS X specific header defines.
 *
 *      By Angelo Mottola.
 *
 *      See readme.txt for copyright information.
 */


#ifndef ALOSX_H
#define ALOSX_H

#ifndef ALLEGRO_MACOSX
   #error bad include
#endif

#ifndef SCAN_DEPEND
   #include <stdio.h>
   #include <stdlib.h>
   #include <fcntl.h>
   #include <unistd.h>
   #include <signal.h>
   #include <pthread.h>
   
   #undef TRUE
   #undef FALSE
   #import <ApplicationServices/ApplicationServices.h>
   #import <Cocoa/Cocoa.h>
   #import <Carbon/Carbon.h>
   #undef TRUE
   #undef FALSE
   #undef assert
   #define TRUE  -1
   #define FALSE 0
#endif


/* The following code comes from alunix.h */
/* Magic to capture name of executable file */
extern int    __crt0_argc;
extern char **__crt0_argv;

#ifndef ALLEGRO_NO_MAGIC_MAIN
   #define ALLEGRO_MAGIC_MAIN
   #define main _mangled_main
   #undef END_OF_MAIN
   #define END_OF_MAIN() void *_mangled_main_address = (void*) _mangled_main;
#else
   #undef END_OF_MAIN
   #define END_OF_MAIN() void *_mangled_main_address;
#endif

/* System driver */
#define SYSTEM_MACOSX           AL_ID('O','S','X',0)
AL_VAR(SYSTEM_DRIVER, system_macosx);

/* Timer driver */
#define TIMERDRV_UNIX_PTHREADS  AL_ID('P','T','H','R')
AL_VAR(TIMER_DRIVER, timerdrv_unix_pthreads);

/* Keyboard driver */
#define KEYBOARD_MACOSX         AL_ID('O','S','X','K')
AL_VAR(KEYBOARD_DRIVER, keyboard_macosx);

/* Mouse driver */
#define MOUSE_MACOSX            AL_ID('O','S','X','M')
AL_VAR(MOUSE_DRIVER, mouse_macosx);

/* Gfx drivers */
#define GFX_QUARTZ_WINDOW       AL_ID('Q','Z','W','N')
#define GFX_QUARTZ_FULLSCREEN   AL_ID('Q','Z','F','L')
AL_VAR(GFX_DRIVER, gfx_quartz_window);
AL_VAR(GFX_DRIVER, gfx_quartz_full);


#endif
