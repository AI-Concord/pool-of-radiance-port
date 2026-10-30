// Engine interface shared by the Swift front end and the game core.
#ifndef POOLRAD_ENGINE_H
#define POOLRAD_ENGINE_H
#define ENGINE_WIDTH 320
#define ENGINE_HEIGHT 200
void engine_start(const char *game_dir, const char *save_dir);
void engine_post_key(unsigned short bios_key);   // high byte scancode, low byte ASCII
int engine_copy_frame(unsigned char *bgra);      // 320x200 BGRA; returns 1 if a frame is available
int engine_has_exited(void);
void engine_request_quit(void);
int engine_record(const char *dir);              // before engine_start: record the session into dir (keys.txt, clock.txt); 1 if ok
void engine_mark(const char *label);             // the player marks a problem: noted in the recording at this point
int engine_note_count(void);                     // notes so far (battle, each round, battle over)
const char *engine_note_last(void);
void engine_note_frame(unsigned char *bgra);     // the screen as it was at the latest note
void engine_audio_render(float *out, int n, double sample_rate);   // PC speaker, mono
#endif
