/* Manual test for dropped key releases on Windows.
 *
 * Logs every keyboard event and, whenever it changes, the set of arrow keys
 * that al_get_keyboard_state() reports as held. Use together with a script
 * that posts WM_KEYDOWN / WM_KEYUP messages to this window (see the testing
 * notes); a key that is still listed as held after the posted key up, with no
 * KEY_UP logged, is the bug.
 *
 * Not part of the source tree: add `example(ex_stuck_key)` to
 * examples/CMakeLists.txt to build it.
 */
#include <stdio.h>
#include <string.h>
#include <allegro5/allegro.h>
#include "common.c"

static const int keys[] = { ALLEGRO_KEY_LEFT, ALLEGRO_KEY_RIGHT, ALLEGRO_KEY_UP,
   ALLEGRO_KEY_DOWN, ALLEGRO_KEY_Z, ALLEGRO_KEY_X };

static void describe_state(char *buf, size_t n)
{
   ALLEGRO_KEYBOARD_STATE st;
   size_t i;
   al_get_keyboard_state(&st);
   buf[0] = 0;
   for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
      if (al_key_down(&st, keys[i])) {
         strncat(buf, al_keycode_to_name(keys[i]), n - strlen(buf) - 1);
         strncat(buf, " ", n - strlen(buf) - 1);
      }
   }
   if (!buf[0])
      strncpy(buf, "(none)", n);
}

int main(int argc, char **argv)
{
   ALLEGRO_DISPLAY *display;
   ALLEGRO_EVENT_QUEUE *queue;
   ALLEGRO_TIMER *timer;
   char state[256], last_state[256] = "";
   bool done = false;

   (void)argc;
   (void)argv;

   if (!al_init())
      abort_example("Could not init Allegro.\n");
   al_install_keyboard();
   open_log();

   display = al_create_display(320, 200);
   if (!display)
      abort_example("Could not create display.\n");
   al_set_window_title(display, "ex_stuck_key");

   timer = al_create_timer(1.0 / 60);
   queue = al_create_event_queue();
   al_register_event_source(queue, al_get_keyboard_event_source());
   al_register_event_source(queue, al_get_display_event_source(display));
   al_register_event_source(queue, al_get_timer_event_source(timer));
   al_start_timer(timer);

   log_printf("Focus the window (title \"ex_stuck_key\"). ESC quits.\n");

   while (!done) {
      ALLEGRO_EVENT ev;
      al_wait_for_event(queue, &ev);
      switch (ev.type) {
         case ALLEGRO_EVENT_KEY_DOWN:
            log_printf("KEY_DOWN %s\n", al_keycode_to_name(ev.keyboard.keycode));
            if (ev.keyboard.keycode == ALLEGRO_KEY_ESCAPE)
               done = true;
            break;
         case ALLEGRO_EVENT_KEY_UP:
            log_printf("KEY_UP   %s\n", al_keycode_to_name(ev.keyboard.keycode));
            break;
         case ALLEGRO_EVENT_KEY_CHAR:
            if (ev.keyboard.repeat)
               log_printf("KEY_CHAR %s (repeat)\n", al_keycode_to_name(ev.keyboard.keycode));
            break;
         case ALLEGRO_EVENT_DISPLAY_CLOSE:
            done = true;
            break;
         case ALLEGRO_EVENT_TIMER:
            describe_state(state, sizeof(state));
            if (strcmp(state, last_state) != 0) {
               log_printf("state: held = %s\n", state);
               strcpy(last_state, state);
            }
            al_clear_to_color(last_state[0] == '(' ? al_map_rgb(0, 0, 0) : al_map_rgb(0, 96, 0));
            al_flip_display();
            break;
      }
   }

   close_log(false);
   return 0;
}
