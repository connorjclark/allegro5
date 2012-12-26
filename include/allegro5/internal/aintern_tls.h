#ifndef __al_included_allegro5_aintern_tls_h
#define __al_included_allegro5_aintern_tls_h

#ifdef __cplusplus
   extern "C" {
#endif


/* XXX do it properly */
#if defined(ALLEGRO_MACOSX) || defined(ALLEGRO_IPHONE) || defined(ALLEGRO_ANDROID) || defined(ALLEGRO_RASPBERRYPI)
// Do some one-time initialisation for the thread support
void _al_pthreads_tls_init(void);
#endif


int *_al_tls_get_dtor_owner_count(void);


#ifdef __cplusplus
   }
#endif

#endif

/* vim: set ts=8 sts=3 sw=3 et: */
