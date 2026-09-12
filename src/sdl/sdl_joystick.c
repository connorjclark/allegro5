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
 *      SDL joystick driver.
 *
 *      See LICENSE.txt for copyright information.
 */
#include "allegro5/allegro.h"
#include "allegro5/internal/aintern.h"
#include "allegro5/internal/aintern_system.h"
#include "allegro5/internal/aintern_thread.h"
#include "allegro5/platform/allegro_internal_sdl.h"

ALLEGRO_DEBUG_CHANNEL("SDL")

/* This driver normally runs as part of the SDL platform port, where the SDL
 * system driver owns SDL_Init and pumps SDL events into
 * _al_sdl_joystick_event via its heartbeat.
 *
 * In standalone mode (ALLEGRO_CFG_SDL2_JOYSTICK, on an otherwise native
 * platform such as X11 or OSX), this driver owns the SDL joystick subsystem
 * itself: it initializes just SDL's joystick/gamecontroller subsystems and
 * pumps their events from a background thread. Nothing else in Allegro uses
 * SDL in this configuration.
 */
#if defined(ALLEGRO_CFG_SDL2_JOYSTICK) && !defined(ALLEGRO_SDL)
#define SDL2_JOYSTICK_STANDALONE
#endif

typedef struct ALLEGRO_JOYSTICK_SDL
{
   SDL_JoystickID id;
   ALLEGRO_JOYSTICK allegro;
   SDL_Joystick *sdl;
   SDL_GameController *sdl_gc;
   bool dpad_state[4];
} ALLEGRO_JOYSTICK_SDL;

static ALLEGRO_JOYSTICK_DRIVER *vt;
static int num_joysticks; /* number of joysticks known to the user */
static _AL_VECTOR joysticks = _AL_VECTOR_INITIALIZER(ALLEGRO_JOYSTICK_SDL *); /* of ALLEGRO_JOYSTICK_SDL pointers */

#ifdef SDL2_JOYSTICK_STANDALONE
static _AL_THREAD standalone_thread;
static _AL_MUTEX standalone_mutex; /* zero-init: unlocked until _al_mutex_init */
static _AL_COND standalone_cond;
static int standalone_init_state; /* 0 = pending, 1 = ok, -1 = failed */
#endif

/* In standalone mode the event pump runs on a background thread while
 * reconfigure/state queries run on app threads, so the joysticks vector
 * needs guarding. In the SDL platform port everything runs under the SDL
 * system driver's pumping and these are no-ops.
 */
static void joysticks_lock(void)
{
#ifdef SDL2_JOYSTICK_STANDALONE
   _al_mutex_lock(&standalone_mutex);
#endif
}

static void joysticks_unlock(void)
{
#ifdef SDL2_JOYSTICK_STANDALONE
   _al_mutex_unlock(&standalone_mutex);
#endif
}

static bool compat_5_2_10(void) {
   /* Mappings. */
   return _al_get_joystick_compat_version() < AL_ID(5, 2, 11, 0);
}

static ALLEGRO_JOYSTICK_SDL *get_joystick_from_allegro(ALLEGRO_JOYSTICK *allegro)
{
   for (int i = 0; i < (int)_al_vector_size(&joysticks); i++) {
      ALLEGRO_JOYSTICK_SDL *joy = *(ALLEGRO_JOYSTICK_SDL **)_al_vector_ref(&joysticks, i);
      if (&joy->allegro == allegro)
         return joy;
   }
   ASSERT(false);
   return NULL;
}

static ALLEGRO_JOYSTICK_SDL *get_joystick_opt(SDL_JoystickID id)
{
   for (int i = 0; i < (int)_al_vector_size(&joysticks); i++) {
      ALLEGRO_JOYSTICK_SDL *joy = *(ALLEGRO_JOYSTICK_SDL **)_al_vector_ref(&joysticks, i);
      if (joy->id == id)
         return joy;
   }
   return NULL;
}

static ALLEGRO_JOYSTICK_SDL *get_or_insert_joystick(SDL_JoystickID id)
{
   ALLEGRO_JOYSTICK_SDL **slot;
   ALLEGRO_JOYSTICK_SDL *joy;

   for (int i = 0; i < (int)_al_vector_size(&joysticks); i++) {
      slot = _al_vector_ref(&joysticks, i);
      joy = *slot;
      if (joy->id == id)
         return joy;
   }

   for (int i = 0; i < (int)_al_vector_size(&joysticks); i++) {
      slot = _al_vector_ref(&joysticks, i);
      joy = *slot;
      if (!SDL_JoystickFromInstanceID(joy->id)) {
         joy->id = id;
         return joy;
      }
   }

   joy = al_calloc(1, sizeof *joy);
   joy->id = id;
   slot = _al_vector_alloc_back(&joysticks);
   *slot = joy;
   return joy;
}

void _al_sdl_joystick_event(SDL_Event *e)
{
   bool emit = false;
   ALLEGRO_EVENT event;
   memset(&event, 0, sizeof event);

   event.joystick.timestamp = al_get_time();

   if (e->type == SDL_CONTROLLERAXISMOTION) {
      ALLEGRO_JOYSTICK_SDL *joy = get_joystick_opt(e->caxis.which);
      if (!joy || joy->allegro.info.type != ALLEGRO_JOYSTICK_TYPE_GAMEPAD)
         return;
      int stick = -1;
      int axis = -1;
      switch (e->caxis.axis) {
         case SDL_CONTROLLER_AXIS_LEFTX:
            stick = ALLEGRO_GAMEPAD_STICK_LEFT_THUMB;
            axis = 0;
            break;
         case SDL_CONTROLLER_AXIS_LEFTY:
            stick = ALLEGRO_GAMEPAD_STICK_LEFT_THUMB;
            axis = 1;
            break;
         case SDL_CONTROLLER_AXIS_RIGHTX:
            stick = ALLEGRO_GAMEPAD_STICK_RIGHT_THUMB;
            axis = 0;
            break;
         case SDL_CONTROLLER_AXIS_RIGHTY:
            stick = ALLEGRO_GAMEPAD_STICK_RIGHT_THUMB;
            axis = 1;
            break;
         case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            stick = ALLEGRO_GAMEPAD_STICK_LEFT_TRIGGER;
            axis = 0;
            break;
         case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            stick = ALLEGRO_GAMEPAD_STICK_RIGHT_TRIGGER;
            axis = 0;
            break;
      }
      if (stick >= 0 && axis >= 0) {
         event.joystick.type = ALLEGRO_EVENT_JOYSTICK_AXIS;
         event.joystick.id = &joy->allegro;
         event.joystick.stick = stick;
         event.joystick.axis = axis;
         event.joystick.pos = e->caxis.value / 32768.0;
         event.joystick.button = 0;
         emit = true;
      }
   }
   else if (e->type == SDL_CONTROLLERBUTTONDOWN || e->type == SDL_CONTROLLERBUTTONUP) {
      int stick = 0;
      int axis = 0;
      int button = 0;
      float pos = 0.;
      bool down = e->type == SDL_CONTROLLERBUTTONDOWN;
      int type = down ? ALLEGRO_EVENT_JOYSTICK_BUTTON_DOWN : ALLEGRO_EVENT_JOYSTICK_BUTTON_UP;
      ALLEGRO_JOYSTICK_SDL *joy = get_joystick_opt(e->cbutton.which);
      if (!joy || joy->allegro.info.type != ALLEGRO_JOYSTICK_TYPE_GAMEPAD)
         return;

      switch (e->cbutton.button) {
         case SDL_CONTROLLER_BUTTON_A:
            button = ALLEGRO_GAMEPAD_BUTTON_A;
            break;
         case SDL_CONTROLLER_BUTTON_B:
            button = ALLEGRO_GAMEPAD_BUTTON_B;
            break;
         case SDL_CONTROLLER_BUTTON_X:
            button = ALLEGRO_GAMEPAD_BUTTON_X;
            break;
         case SDL_CONTROLLER_BUTTON_Y:
            button = ALLEGRO_GAMEPAD_BUTTON_Y;
            break;
         case SDL_CONTROLLER_BUTTON_BACK:
            button = ALLEGRO_GAMEPAD_BUTTON_BACK;
            break;
         case SDL_CONTROLLER_BUTTON_GUIDE:
            button = ALLEGRO_GAMEPAD_BUTTON_GUIDE;
            break;
         case SDL_CONTROLLER_BUTTON_START:
            button = ALLEGRO_GAMEPAD_BUTTON_START;
            break;
         case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            button = ALLEGRO_GAMEPAD_BUTTON_LEFT_SHOULDER;
            break;
         case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            button = ALLEGRO_GAMEPAD_BUTTON_RIGHT_SHOULDER;
            break;
         case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            button = ALLEGRO_GAMEPAD_BUTTON_LEFT_THUMB;
            break;
         case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            button = ALLEGRO_GAMEPAD_BUTTON_RIGHT_THUMB;
            break;
         case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            type = ALLEGRO_EVENT_JOYSTICK_AXIS;
            axis = 0;
            joy->dpad_state[2] = down;
            break;
         case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            type = ALLEGRO_EVENT_JOYSTICK_AXIS;
            axis = 0;
            joy->dpad_state[0] = down;
            break;
         case SDL_CONTROLLER_BUTTON_DPAD_UP:
            type = ALLEGRO_EVENT_JOYSTICK_AXIS;
            axis = 1;
            joy->dpad_state[3] = down;
            break;
         case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            type = ALLEGRO_EVENT_JOYSTICK_AXIS;
            axis = 1;
            joy->dpad_state[1] = down;
            break;
         default:
            type = 0;
      }
      if (type == ALLEGRO_EVENT_JOYSTICK_AXIS) {
         stick = ALLEGRO_GAMEPAD_STICK_DPAD;
         if (axis == 0)
            pos = (int)joy->dpad_state[0] - (int)joy->dpad_state[2];
         else
            pos = (int)joy->dpad_state[1] - (int)joy->dpad_state[3];
      }
      if (type != 0) {
         event.joystick.type = type;
         event.joystick.id = &joy->allegro;
         event.joystick.stick = stick;
         event.joystick.axis = axis;
         event.joystick.pos = pos;
         event.joystick.button = button;
         emit = true;
      }
   }
   else if (e->type == SDL_CONTROLLERDEVICEADDED || e->type == SDL_CONTROLLERDEVICEREMOVED
         || e->type == SDL_JOYDEVICEADDED || e->type == SDL_JOYDEVICEREMOVED) {
      event.joystick.type = ALLEGRO_EVENT_JOYSTICK_CONFIGURATION;
      emit = true;
   }
   else if (e->type == SDL_JOYAXISMOTION) {
      ALLEGRO_JOYSTICK_SDL *joy = get_joystick_opt(e->jaxis.which);
      if (!joy || joy->allegro.info.type != ALLEGRO_JOYSTICK_TYPE_UNKNOWN)
         return;
      event.joystick.type = ALLEGRO_EVENT_JOYSTICK_AXIS;
      event.joystick.id = &joy->allegro;
      event.joystick.stick = e->jaxis.axis / 2;
      event.joystick.axis = e->jaxis.axis % 2;
      event.joystick.pos = e->jaxis.value / 32768.0;
      event.joystick.button = 0;
      emit = true;
   }
   else if (e->type == SDL_JOYBUTTONDOWN) {
      ALLEGRO_JOYSTICK_SDL *joy = get_joystick_opt(e->jbutton.which);
      if (!joy || joy->allegro.info.type != ALLEGRO_JOYSTICK_TYPE_UNKNOWN)
         return;
      event.joystick.type = ALLEGRO_EVENT_JOYSTICK_BUTTON_DOWN;
      event.joystick.id = &joy->allegro;
      event.joystick.stick = 0;
      event.joystick.axis = 0;
      event.joystick.pos = 0;
      event.joystick.button = e->jbutton.button;
      emit = true;
   }
   else if (e->type == SDL_JOYBUTTONUP) {
      ALLEGRO_JOYSTICK_SDL *joy = get_joystick_opt(e->jbutton.which);
      if (!joy || joy->allegro.info.type != ALLEGRO_JOYSTICK_TYPE_UNKNOWN)
         return;
      event.joystick.type = ALLEGRO_EVENT_JOYSTICK_BUTTON_UP;
      event.joystick.id = &joy->allegro;
      event.joystick.stick = 0;
      event.joystick.axis = 0;
      event.joystick.pos = 0;
      event.joystick.button = e->jbutton.button;
      emit = true;
   }
   else {
      return;
   }

   if (emit) {
      ALLEGRO_EVENT_SOURCE *es = al_get_joystick_event_source();
      _al_event_source_lock(es);
      _al_event_source_emit_event(es, &event);
      _al_event_source_unlock(es);
   }
}

static void clear_joysticks(void)
{
   for (int i = 0; i < (int)_al_vector_size(&joysticks); i++) {
      ALLEGRO_JOYSTICK_SDL *joy = *(ALLEGRO_JOYSTICK_SDL **)_al_vector_ref(&joysticks, i);
      if (joy->sdl) {
         SDL_JoystickClose(joy->sdl);
         joy->sdl = NULL;
      }
      if (joy->sdl_gc) {
         SDL_GameControllerClose(joy->sdl_gc);
         joy->sdl_gc = NULL;
      }
      _al_destroy_joystick_info(&joy->allegro.info);
      /* Note that we deliberately keep the joystick id as is, so we can reuse it. */
   }
}

static bool sdl_init_joystick(void)
{
   num_joysticks = SDL_NumJoysticks();
   for (int i = 0; i < num_joysticks; i++) {
      ALLEGRO_JOYSTICK_SDL *joy = get_or_insert_joystick(SDL_JoystickGetDeviceInstanceID(i));
      SDL_JoystickGUID sdl_guid = SDL_JoystickGetDeviceGUID(i);
      memcpy(&joy->allegro.info.guid, &sdl_guid, sizeof(ALLEGRO_JOYSTICK_GUID));

      if (SDL_IsGameController(i) && !compat_5_2_10()) {
         joy->sdl_gc = SDL_GameControllerOpen(i);
         _al_fill_gamepad_info(&joy->allegro.info);
         SDL_GameControllerEventState(SDL_ENABLE);
         continue;
      }

      joy->sdl = SDL_JoystickOpen(i);
      _AL_JOYSTICK_INFO *info = &joy->allegro.info;
      int an = SDL_JoystickNumAxes(joy->sdl);
      int a;
      info->num_sticks = an / 2;
      for (a = 0; a < an; a++) {
         info->stick[a / 2].num_axes = 2;
         info->stick[a / 2].name = _al_strdup("stick");
         info->stick[a / 2].axis[0].name = _al_strdup("X");
         info->stick[a / 2].axis[1].name = _al_strdup("Y");
      }

      int bn = SDL_JoystickNumButtons(joy->sdl);
      info->num_buttons = bn;
      int b;
      for (b = 0; b < bn; b++) {
         info->button[b].name = _al_strdup(SDL_IsGameController(i) ?
            SDL_GameControllerGetStringForButton(b) : "button");
      }
      SDL_JoystickEventState(SDL_ENABLE);
   }
   return true;
}

static void sdl_exit_joystick(void)
{
   clear_joysticks();
   num_joysticks = 0;
   _al_vector_free(&joysticks);
}

static bool sdl_reconfigure_joysticks(void)
{
   joysticks_lock();
   clear_joysticks();
   bool ok = sdl_init_joystick();
   joysticks_unlock();
   return ok;
}

static int sdl_num_joysticks(void)
{
   return num_joysticks;
}

static ALLEGRO_JOYSTICK *sdl_get_joystick(int joyn)
{
   ALLEGRO_JOYSTICK *ret = NULL;
   joysticks_lock();
   for (int i = 0; i < (int)_al_vector_size(&joysticks); i++) {
      ALLEGRO_JOYSTICK_SDL *joy = *(ALLEGRO_JOYSTICK_SDL **)_al_vector_ref(&joysticks, i);
      if (SDL_JoystickFromInstanceID(joy->id)) {
         if (joyn == 0) {
            ret = &joy->allegro;
            break;
         }
         joyn--;
      }
   }
   joysticks_unlock();
   return ret;
}

static void sdl_release_joystick(ALLEGRO_JOYSTICK *joy)
{
   ASSERT(joy);
}

static void sdl_get_joystick_state(ALLEGRO_JOYSTICK *joy,
   ALLEGRO_JOYSTICK_STATE *ret_state)
{
#ifndef SDL2_JOYSTICK_STANDALONE
   ALLEGRO_SYSTEM_INTERFACE *s = _al_sdl_system_driver();
   s->heartbeat();
#endif
   joysticks_lock();

#define BUTTON(x) ((x) * 32767)
#define AXIS(x) ((x) / 32768.0)

   ALLEGRO_JOYSTICK_SDL *joy_sdl = get_joystick_from_allegro(joy);
   if (joy->info.type == ALLEGRO_JOYSTICK_TYPE_GAMEPAD) {
      SDL_GameController *sdl_gc = joy_sdl->sdl_gc;
      ret_state->stick[ALLEGRO_GAMEPAD_STICK_DPAD].axis[0] = SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
            - SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
      ret_state->stick[ALLEGRO_GAMEPAD_STICK_DPAD].axis[1] = SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            - SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_DPAD_UP);
      ret_state->stick[ALLEGRO_GAMEPAD_STICK_LEFT_THUMB].axis[0] = AXIS(SDL_GameControllerGetAxis(sdl_gc, SDL_CONTROLLER_AXIS_LEFTX));
      ret_state->stick[ALLEGRO_GAMEPAD_STICK_LEFT_THUMB].axis[1] = AXIS(SDL_GameControllerGetAxis(sdl_gc, SDL_CONTROLLER_AXIS_LEFTY));
      ret_state->stick[ALLEGRO_GAMEPAD_STICK_RIGHT_THUMB].axis[0] = AXIS(SDL_GameControllerGetAxis(sdl_gc, SDL_CONTROLLER_AXIS_RIGHTX));
      ret_state->stick[ALLEGRO_GAMEPAD_STICK_RIGHT_THUMB].axis[1] = AXIS(SDL_GameControllerGetAxis(sdl_gc, SDL_CONTROLLER_AXIS_RIGHTY));
      ret_state->stick[ALLEGRO_GAMEPAD_STICK_LEFT_TRIGGER].axis[0] = AXIS(SDL_GameControllerGetAxis(sdl_gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
      ret_state->stick[ALLEGRO_GAMEPAD_STICK_RIGHT_TRIGGER].axis[0] = AXIS(SDL_GameControllerGetAxis(sdl_gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_A] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_A));
      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_B] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_B));
      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_X] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_X));
      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_Y] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_Y));
      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_BACK] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_BACK));
      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_GUIDE] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_GUIDE));
      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_START] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_START));
      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_LEFT_SHOULDER] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_RIGHT_SHOULDER] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_LEFT_THUMB] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_LEFTSTICK));
      ret_state->button[ALLEGRO_GAMEPAD_BUTTON_RIGHT_THUMB] = BUTTON(SDL_GameControllerGetButton(sdl_gc, SDL_CONTROLLER_BUTTON_RIGHTSTICK));
   }
   else {
      SDL_Joystick *sdl = joy_sdl->sdl;
      int an = SDL_JoystickNumAxes(sdl);
      int i;
      for (i = 0; i < an; i++) {
         ret_state->stick[i / 2].axis[i % 2] = AXIS(SDL_JoystickGetAxis(sdl, i));
      }
      int bn = SDL_JoystickNumButtons(sdl);
      for (i = 0; i < bn; i++) {
         ret_state->button[i] = BUTTON(SDL_JoystickGetButton(sdl, i));
      }
   }

#undef AXIS
#undef BUTTON

   joysticks_unlock();
}

static const char *sdl_get_name(ALLEGRO_JOYSTICK *joy)
{
   const char *ret = NULL;
   joysticks_lock();
   ALLEGRO_JOYSTICK_SDL *joy_sdl = get_joystick_from_allegro(joy);
   if (joy_sdl->sdl)
      ret = SDL_JoystickName(joy_sdl->sdl);
   else if (joy_sdl->sdl_gc)
      ret = SDL_GameControllerName(joy_sdl->sdl_gc);
   joysticks_unlock();
   return ret;
}

static bool sdl_get_active(ALLEGRO_JOYSTICK *joy)
{
   bool ret = false;
   joysticks_lock();
   ALLEGRO_JOYSTICK_SDL *joy_sdl = get_joystick_from_allegro(joy);
   if (joy_sdl->sdl)
      ret = SDL_JoystickGetAttached(joy_sdl->sdl);
   else if (joy_sdl->sdl_gc)
      ret = SDL_GameControllerGetAttached(joy_sdl->sdl_gc);
   joysticks_unlock();
   return ret;
}

/* Returns the joystick's SDL_GameControllerType as an int, or -1 if the
 * joystick is not an SDL game controller (or not an SDL joystick at all,
 * e.g. a native driver is selected). Lets users distinguish controllers
 * whose face buttons carry letter labels (Xbox, Nintendo) from those that
 * do not (PlayStation).
 */
int _al_sdl_joystick_controller_type(ALLEGRO_JOYSTICK *joy)
{
   int ret = -1;
   joysticks_lock();
   /* Not get_joystick_from_allegro: when a native driver is selected this
    * driver's joystick list is empty, and that lookup asserts on a miss.
    */
   for (int i = 0; i < (int)_al_vector_size(&joysticks); i++) {
      ALLEGRO_JOYSTICK_SDL *joy_sdl = *(ALLEGRO_JOYSTICK_SDL **)_al_vector_ref(&joysticks, i);
      if (&joy_sdl->allegro == joy) {
         if (joy_sdl->sdl_gc)
            ret = (int)SDL_GameControllerGetType(joy_sdl->sdl_gc);
         break;
      }
   }
   joysticks_unlock();
   return ret;
}

#ifdef SDL2_JOYSTICK_STANDALONE

/* Feed mapping lines the user gave to al_set_joystick_mappings into SDL,
 * which understands the same format. SDL itself filters nothing here, so
 * skip lines meant for other platforms.
 */
static void standalone_add_mappings(void)
{
   const _AL_VECTOR *lines = _al_get_raw_joystick_mapping_lines();
   const char *platform = SDL_GetPlatform();
   for (int i = 0; i < (int)_al_vector_size(lines); i++) {
      const char *line = *(char **)_al_vector_ref((_AL_VECTOR *)lines, i);
      const char *p = strstr(line, "platform:");
      if (p) {
         p += strlen("platform:");
         size_t n = strcspn(p, ",\r\n");
         if (n != strlen(platform) || strncmp(p, platform, n) != 0)
            continue;
      }
      if (SDL_GameControllerAddMapping(line) < 0)
         ALLEGRO_WARN("SDL rejected mapping line: %s\n", line);
   }
}

/* Initializes SDL's joystick subsystem. Runs on the pump thread: some SDL
 * backends are bound to the thread that initialized them. SDL's IOKit
 * backend (macOS) schedules its HID manager on that thread's run loop and
 * only delivers hotplug callbacks when the same thread later pumps, so
 * initializing on the app thread would leave generic (non-HIDAPI) pads
 * unable to hotplug.
 */
static bool standalone_sdl_init(void)
{
   /* ZC and other Allegro apps handle these themselves. */
   SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
   /* On Windows, SDL's RawInput backend (which claims Xbox controllers)
    * receives its input as WM_INPUT messages on a message window owned by
    * the thread that initializes the joystick subsystem. Without the video
    * subsystem nothing runs a Win32 message loop on that thread, so no
    * input would ever arrive. This hint makes SDL create its own internal
    * thread that owns the message window and pumps it. No effect on other
    * platforms.
    */
   SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
   /* There is no SDL window, so never gate joystick input on focus. */
   SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
   /* Report Nintendo-style pads by their button labels (A/B and X/Y swapped
    * relative to the physical Xbox positions), so that label-based bindings
    * match what is printed on the controller. This is SDL's default; set it
    * explicitly because default control schemes may depend on it.
    */
   SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "1");
   if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
      ALLEGRO_ERROR("SDL_InitSubSystem(JOYSTICK|GAMECONTROLLER) failed: %s\n",
         SDL_GetError());
      return false;
   }
   standalone_add_mappings();

   joysticks_lock();
   bool ok = sdl_init_joystick();
   joysticks_unlock();
   if (!ok)
      SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
   return ok;
}

static void standalone_pump(_AL_THREAD *self, void *unused)
{
   (void)unused;

   bool ok = standalone_sdl_init();
   _al_mutex_lock(&standalone_mutex);
   standalone_init_state = ok ? 1 : -1;
   _al_cond_broadcast(&standalone_cond);
   _al_mutex_unlock(&standalone_mutex);
   if (!ok)
      return;

   SDL_Event events[16];
   while (!_al_get_thread_should_stop(self)) {
      SDL_PumpEvents();
      int n;
      while ((n = SDL_PeepEvents(events, 16, SDL_GETEVENT,
            SDL_JOYAXISMOTION, SDL_CONTROLLERDEVICEREMAPPED)) > 0) {
         joysticks_lock();
         for (int i = 0; i < n; i++)
            _al_sdl_joystick_event(&events[i]);
         joysticks_unlock();
      }
      /* Drop anything we don't consume (e.g. sensor events) so the queue
       * cannot grow unbounded; nothing else reads it in this mode. Leave the
       * joystick/controller range alone: an event another thread pushes
       * between the drain above and this flush (e.g. SDL's GameController
       * framework backend on macOS posts device-added events from the main
       * thread) must survive until the next drain.
       */
      SDL_FlushEvents(SDL_FIRSTEVENT, SDL_JOYAXISMOTION - 1);
      SDL_FlushEvents(SDL_CONTROLLERDEVICEREMAPPED + 1, SDL_LASTEVENT);
      al_rest(0.004);
   }

   /* Shut down on this thread too: e.g. the IOKit backend unschedules its
    * HID manager from the current thread's run loop.
    */
   joysticks_lock();
   sdl_exit_joystick();
   joysticks_unlock();
   SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
}

/* Starts the pump thread and blocks until it reports whether SDL's joystick
 * subsystem came up.
 */
static bool sdl_init_joystick_standalone(void)
{
   _al_mutex_init(&standalone_mutex);
   _al_cond_init(&standalone_cond);
   standalone_init_state = 0;
   _al_thread_create(&standalone_thread, standalone_pump, NULL);

   _al_mutex_lock(&standalone_mutex);
   while (standalone_init_state == 0)
      _al_cond_wait(&standalone_cond, &standalone_mutex);
   bool ok = standalone_init_state > 0;
   _al_mutex_unlock(&standalone_mutex);

   if (!ok) {
      _al_thread_join(&standalone_thread);
      _al_cond_destroy(&standalone_cond);
      _al_mutex_destroy(&standalone_mutex);
   }
   return ok;
}

static void sdl_exit_joystick_standalone(void)
{
   /* The pump thread closes the joysticks and quits SDL before exiting. */
   _al_thread_join(&standalone_thread);
   _al_cond_destroy(&standalone_cond);
   _al_mutex_destroy(&standalone_mutex);
}

#endif /* SDL2_JOYSTICK_STANDALONE */

ALLEGRO_JOYSTICK_DRIVER *_al_sdl_joystick_driver(void)
{
   if (vt)
      return vt;

   vt = al_calloc(1, sizeof *vt);
   vt->joydrv_id = AL_ID('S','D','L','2');
   vt->joydrv_name = "SDL2 Joystick";
   vt->joydrv_desc = "SDL2 Joystick";
   vt->joydrv_ascii_name = "SDL2 Joystick";
#ifdef SDL2_JOYSTICK_STANDALONE
   vt->init_joystick = sdl_init_joystick_standalone;
   vt->exit_joystick = sdl_exit_joystick_standalone;
#else
   vt->init_joystick = sdl_init_joystick;
   vt->exit_joystick = sdl_exit_joystick;
#endif
   vt->reconfigure_joysticks = sdl_reconfigure_joysticks;
   vt->num_joysticks = sdl_num_joysticks;
   vt->get_joystick = sdl_get_joystick;
   vt->release_joystick = sdl_release_joystick;
   vt->get_joystick_state = sdl_get_joystick_state;;
   vt->get_name = sdl_get_name;
   vt->get_active = sdl_get_active;

   return vt;
}
