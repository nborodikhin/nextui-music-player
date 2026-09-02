#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <SDL2/SDL_image.h>

#include "defines.h"
#include "api.h"
#include "display_helper.h"
#include "module_common.h"
#include "test_control.h"

// Buttons enter the pad state directly, after PAD_poll() reads the hardware.
// Do not send synthetic SDL events instead: the desktop platform reads key
// events and the device platforms read joystick events, thus one event shape
// cannot drive all targets.

#define MAX_ACTIONS   128
#define MAX_STEPS     64
#define MAX_LINE      1024
#define MAX_PATH_LEN  192

// A press stays down for more than one frame, because logic that reads a held
// button does not see a button that goes down and up in one poll. The value is
// less than PAD_REPEAT_DELAY, thus a single press does not repeat.
#define PRESS_HOLD_MS 80

// Space between two steps, so the pad sees a release before the next press.
#define STEP_GAP_MS   50

#define REPLY_PREFIX  "@"

typedef enum {
    ACT_DOWN,
    ACT_UP,
    ACT_SHOT,
    ACT_KEEP,
    ACT_QUIT,
} ActionType;

typedef struct {
    ActionType type;
    uint32_t   at;
    int        btn;
    int        btn_id;
    int        line;                // the line that made the action
    char       path[MAX_PATH_LEN];
} Action;

typedef struct {
    int      line;
    uint32_t end;
} Step;

typedef struct {
    const char* name;
    int         btn;
    int         btn_id;
} ButtonName;

static const ButtonName buttons[] = {
    { "UP",     BTN_DPAD_UP,    BTN_ID_DPAD_UP    },
    { "DOWN",   BTN_DPAD_DOWN,  BTN_ID_DPAD_DOWN  },
    { "LEFT",   BTN_DPAD_LEFT,  BTN_ID_DPAD_LEFT  },
    { "RIGHT",  BTN_DPAD_RIGHT, BTN_ID_DPAD_RIGHT },
    { "A",      BTN_A,          BTN_ID_A          },
    { "B",      BTN_B,          BTN_ID_B          },
    { "X",      BTN_X,          BTN_ID_X          },
    { "Y",      BTN_Y,          BTN_ID_Y          },
    { "START",  BTN_START,      BTN_ID_START      },
    { "SELECT", BTN_SELECT,     BTN_ID_SELECT     },
    { "L1",     BTN_L1,         BTN_ID_L1         },
    { "R1",     BTN_R1,         BTN_ID_R1         },
    { "L2",     BTN_L2,         BTN_ID_L2         },
    { "R2",     BTN_R2,         BTN_ID_R2         },
    { "MENU",   BTN_MENU,       BTN_ID_MENU       },
    { "PLUS",   BTN_PLUS,       BTN_ID_PLUS       },
    { "MINUS",  BTN_MINUS,      BTN_ID_MINUS      },
    { "POWER",  BTN_POWER,      BTN_ID_POWER      },
};

static bool active = false;
static int  in_fd = -1;
static int  out_fd = -1;
static bool close_in = false;       // false for a descriptor the caller owns
static bool close_out = false;
static bool eof_ends_run = true;    // false for a FIFO that a path gives
static bool input_eof = false;
static bool keep_open = false;   // the end of the source does not stop the app
static bool quit_asked = false;

static Action   queue[MAX_ACTIONS];
static int      queue_head = 0;
static int      queue_count = 0;

static Step     steps[MAX_STEPS];
static int      steps_head = 0;
static int      steps_count = 0;

static char     line_buf[MAX_LINE];
static int      line_len = 0;
static int      line_no = 0;
static uint32_t cursor = 0;         // when the next step starts

static void reply(const char* fmt, ...) {
    char buf[512];
    int n = snprintf(buf, sizeof(buf), REPLY_PREFIX);
    va_list args;
    va_start(args, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - (size_t)n - 2, fmt, args);
    va_end(args);
    if (n > (int)sizeof(buf) - 2) n = (int)sizeof(buf) - 2;
    buf[n++] = '\n';

    // A short write leaves a partial line. Repeat until the line is complete.
    int done = 0;
    while (done < n) {
        ssize_t w = write(out_fd, buf + done, (size_t)(n - done));
        if (w > 0) { done += (int)w; continue; }
        if (w < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        break;
    }
}

// Split "<in>[,<out>]" and give the two sides. Returns false if a side is empty.
static bool split_value(const char* value, char* in, size_t in_size, char* out, size_t out_size) {
    const char* comma = strchr(value, ',');
    size_t in_len = comma ? (size_t)(comma - value) : strlen(value);
    if (in_len == 0 || in_len >= in_size) return false;
    memcpy(in, value, in_len);
    in[in_len] = '\0';

    out[0] = '\0';
    if (comma) {
        if (strlen(comma + 1) == 0 || strlen(comma + 1) >= out_size) return false;
        strcpy(out, comma + 1);
    }
    return true;
}

// Give the descriptor of "fd:<n>", or -1 if the text has another form.
static int parse_fd(const char* spec) {
    if (strncmp(spec, "fd:", 3) != 0) return -1;
    char* end = NULL;
    long n = strtol(spec + 3, &end, 10);
    if (!end || *end != '\0' || n < 0 || n > 1024) return -1;
    return (int)n;
}

static bool open_in(const char* spec) {
    if (strcmp(spec, "std") == 0) {
        in_fd = STDIN_FILENO;
        eof_ends_run = true;
    }
    else {
        int fd = parse_fd(spec);
        if (fd >= 0) {
            struct stat st;
            if (fstat(fd, &st) != 0) {
                fprintf(stderr, "test-control: descriptor %d is not open\n", fd);
                return false;
            }
            in_fd = fd;
            eof_ends_run = true;
        }
        else {
            struct stat st;
            if (stat(spec, &st) != 0) {
                fprintf(stderr, "test-control: cannot open %s: %s\n", spec, strerror(errno));
                return false;
            }
            // A FIFO gives end of file each time that its last writer closes.
            // Keep a writer of our own, so only a quit command ends the run.
            int flags = S_ISFIFO(st.st_mode) ? O_RDWR : O_RDONLY;
            in_fd = open(spec, flags | O_NONBLOCK);
            if (in_fd < 0) {
                fprintf(stderr, "test-control: cannot open %s: %s\n", spec, strerror(errno));
                return false;
            }
            close_in = true;
            eof_ends_run = !S_ISFIFO(st.st_mode);
        }
    }

    int flags = fcntl(in_fd, F_GETFL, 0);
    if (flags >= 0) fcntl(in_fd, F_SETFL, flags | O_NONBLOCK);
    return true;
}

static bool open_out(const char* spec) {
    if (spec[0] == '\0') {
        // The prefix separates a reply from a log line on standard output.
        out_fd = STDOUT_FILENO;
        return true;
    }
    if (strcmp(spec, "std") == 0) {
        out_fd = STDOUT_FILENO;
        return true;
    }

    int fd = parse_fd(spec);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) != 0) {
            fprintf(stderr, "test-control: descriptor %d is not open\n", fd);
            return false;
        }
        out_fd = fd;
        return true;
    }

    struct stat st;
    bool is_fifo = (stat(spec, &st) == 0) && S_ISFIFO(st.st_mode);
    // A FIFO opens for write only after a reader attaches. Do not block the
    // app before it draws: report that nothing reads the replies.
    int flags = is_fifo ? (O_WRONLY | O_NONBLOCK) : (O_WRONLY | O_CREAT | O_TRUNC);
    out_fd = open(spec, flags, 0644);
    if (out_fd < 0) {
        if (is_fifo && errno == ENXIO)
            fprintf(stderr, "test-control: no program reads %s\n", spec);
        else
            fprintf(stderr, "test-control: cannot write %s: %s\n", spec, strerror(errno));
        return false;
    }
    close_out = true;
    return true;
}

bool TestControl_init(const char* value) {
    if (!value || value[0] == '\0') return false;

    char in_spec[MAX_PATH_LEN];
    char out_spec[MAX_PATH_LEN];
    if (!split_value(value, in_spec, sizeof(in_spec), out_spec, sizeof(out_spec))) {
        fprintf(stderr, "test-control: bad value '%s', expected <in>[,<out>]\n", value);
        return false;
    }
    if (!open_in(in_spec)) return false;
    if (!open_out(out_spec)) {
        if (close_in) close(in_fd);
        return false;
    }

    active = true;
    cursor = 0;
    return true;
}

void TestControl_quit(void) {
    if (!active) return;
    if (close_in) close(in_fd);
    if (close_out) close(out_fd);
    active = false;
}

static const ButtonName* find_button(const char* name) {
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (strcmp(buttons[i].name, name) == 0) return &buttons[i];
    }
    return NULL;
}

static int current_line = 0;

static Action* push_action(ActionType type, uint32_t at) {
    if (queue_count >= MAX_ACTIONS) return NULL;
    Action* a = &queue[(queue_head + queue_count) % MAX_ACTIONS];
    queue_count++;
    memset(a, 0, sizeof(*a));
    a->type = type;
    a->at = at;
    a->line = current_line;
    return a;
}

static void push_step(int line, uint32_t end) {
    if (steps_count >= MAX_STEPS) return;
    steps[(steps_head + steps_count) % MAX_STEPS] = (Step){ .line = line, .end = end };
    steps_count++;
}

// Remove the spaces at each end of the text, in place.
static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
    *end = '\0';
    return s;
}

// Execute one command of a line. base gives the start time of the step.
// Returns the end time of the command, or base if the command has no duration.
static uint32_t schedule_command(const char* name, char* args, uint32_t base, int line) {
    current_line = line;
    char* arg1 = args;
    char* arg2 = NULL;
    if (args) {
        char* comma = strchr(args, ',');
        if (comma) {
            *comma = '\0';
            arg2 = trim(comma + 1);
        }
        arg1 = trim(args);
    }

    if (strcmp(name, "press") == 0 || strcmp(name, "hold") == 0 || strcmp(name, "release") == 0) {
        const ButtonName* b = arg1 ? find_button(arg1) : NULL;
        if (!b) {
            reply("err %d unknown button '%s'", line, arg1 ? arg1 : "");
            return base;
        }
        if (strcmp(name, "release") == 0) {
            Action* a = push_action(ACT_UP, base);
            if (!a) { reply("err %d queue is full", line); return base; }
            a->btn = b->btn;
            a->btn_id = b->btn_id;
            return base;
        }
        // press(BTN, keep) puts the button down and does not release it. Its
        // counterpart is release(BTN).
        if (strcmp(name, "press") == 0 && arg2 && strcmp(arg2, "keep") == 0) {
            Action* a = push_action(ACT_DOWN, base);
            if (!a) { reply("err %d queue is full", line); return base; }
            a->btn = b->btn;
            a->btn_id = b->btn_id;
            return base;
        }

        long n = 1;
        if (arg2) {
            char* end = NULL;
            n = strtol(arg2, &end, 10);
            if (!end || *end != '\0' || n <= 0) {
                reply("err %d bad count '%s', expected a number or 'keep'", line, arg2);
                return base;
            }
        }
        if (strcmp(name, "hold") == 0) {
            uint32_t ms = arg2 ? (uint32_t)n : PRESS_HOLD_MS;
            Action* down = push_action(ACT_DOWN, base);
            Action* up = push_action(ACT_UP, base + ms);
            if (!down || !up) { reply("err %d queue is full", line); return base; }
            down->btn = up->btn = b->btn;
            down->btn_id = up->btn_id = b->btn_id;
            return base + ms;
        }
        uint32_t at = base;
        for (long i = 0; i < n; i++) {
            Action* down = push_action(ACT_DOWN, at);
            Action* up = push_action(ACT_UP, at + PRESS_HOLD_MS);
            if (!down || !up) { reply("err %d queue is full", line); return at; }
            down->btn = up->btn = b->btn;
            down->btn_id = up->btn_id = b->btn_id;
            at += PRESS_HOLD_MS;
            if (i + 1 < n) at += STEP_GAP_MS;
        }
        return at;
    }

    if (strcmp(name, "wait") == 0) {
        char* end = NULL;
        long ms = arg1 ? strtol(arg1, &end, 10) : -1;
        if (!arg1 || !end || *end != '\0' || ms < 0) {
            reply("err %d bad delay '%s'", line, arg1 ? arg1 : "");
            return base;
        }
        return base + (uint32_t)ms;
    }

    if (strcmp(name, "screenshot") == 0) {
        if (!arg1 || arg1[0] == '\0' || strlen(arg1) >= MAX_PATH_LEN) {
            reply("err %d bad path", line);
            return base;
        }
        Action* a = push_action(ACT_SHOT, base);
        if (!a) { reply("err %d queue is full", line); return base; }
        strcpy(a->path, arg1);
        return base;
    }

    if (strcmp(name, "keep") == 0) {
        if (!push_action(ACT_KEEP, base)) reply("err %d queue is full", line);
        return base;
    }

    if (strcmp(name, "quit") == 0) {
        if (!push_action(ACT_QUIT, base)) reply("err %d queue is full", line);
        return base;
    }

    reply("err %d unknown command '%s'", line, name);
    return base;
}

// Schedule each command of one line. All commands of a line start together.
static void schedule_line(char* text) {
    line_no++;

    char* hash = strchr(text, '#');
    if (hash) *hash = '\0';
    char* p = trim(text);
    if (*p == '\0') return;

    uint32_t now = SDL_GetTicks();
    uint32_t base = (cursor > now) ? cursor : now;
    uint32_t end = base;

    while (*p) {
        if (*p == ',' || *p == ' ' || *p == '\t') { p++; continue; }

        char* name = p;
        while (*p && *p != '(' && *p != ' ' && *p != '\t' && *p != ',') p++;
        // A space between the name and its arguments is permitted.
        char* after_name = p;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '(') p = after_name;

        char* args = NULL;
        if (*p == '(') {
            *after_name = '\0';
            *p++ = '\0';
            args = p;
            char* close = strchr(p, ')');
            if (!close) {
                reply("err %d no ')' in the line", line_no);
                break;
            }
            *close = '\0';
            p = close + 1;
        }
        else if (*p) {
            *p++ = '\0';
        }

        uint32_t cmd_end = schedule_command(name, args, base, line_no);
        if (cmd_end > end) end = cmd_end;
    }

    push_step(line_no, end);
    cursor = end + STEP_GAP_MS;
}

// Read what arrived and cut it into lines. A line that arrives in two writes
// stays in the buffer until its end.
static void read_input(void) {
    if (input_eof) return;

    char buf[512];
    for (;;) {
        ssize_t n = read(in_fd, buf, sizeof(buf));
        if (n == 0) {
            if (eof_ends_run) input_eof = true;
            return;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            return;  // EAGAIN: nothing more at this time
        }
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                line_buf[line_len] = '\0';
                schedule_line(line_buf);
                line_len = 0;
            }
            else if (line_len < MAX_LINE - 1) {
                line_buf[line_len++] = buf[i];
            }
        }
    }
}

// The fields agree with the fields that a real press gives. A list reads
// just_repeated for its up and down movement, thus a press that sets only
// just_pressed moves no cursor.
static void button_down(int btn, int btn_id) {
    pad.just_pressed |= btn;
    pad.just_repeated |= btn;
    pad.is_pressed |= btn;
    // PAD_poll() makes the automatic repeat from this time in the next frames.
    pad.repeat_at[btn_id] = SDL_GetTicks() + PAD_REPEAT_DELAY;
}

static void button_up(int btn) {
    pad.just_released |= btn;
    pad.just_repeated &= ~btn;
    pad.is_pressed &= ~btn;
}

static void take_screenshot(const Action* a) {
    SDL_Surface* screen = DisplayHelper_getSurface(DisplayHelper_current());
    if (!screen) {
        reply("err %d no screen to save", a->line);
        return;
    }
    if (IMG_SavePNG(screen, a->path) != 0) {
        reply("err %d cannot write %s: %s", a->line, a->path, IMG_GetError());
    }
}

static void run_due_actions(uint32_t now) {
    while (queue_count > 0) {
        Action* a = &queue[queue_head];
        if (a->at > now) break;
        switch (a->type) {
            case ACT_DOWN: button_down(a->btn, a->btn_id); break;
            case ACT_UP:   button_up(a->btn); break;
            case ACT_SHOT: take_screenshot(a); break;
            case ACT_KEEP: keep_open = true; break;
            case ACT_QUIT:
                quit_asked = true;
                reply("bye");
                ModuleCommon_requestQuit();
                break;
        }
        queue_head = (queue_head + 1) % MAX_ACTIONS;
        queue_count--;
    }
}

static void reply_finished_steps(uint32_t now) {
    while (steps_count > 0) {
        const Step* s = &steps[steps_head];
        if (s->end > now) break;
        reply("ok %d", s->line);
        steps_head = (steps_head + 1) % MAX_STEPS;
        steps_count--;
    }
}

void TestControl_tick(void) {
    if (!active || quit_asked) return;

    read_input();

    uint32_t now = SDL_GetTicks();
    run_due_actions(now);
    if (quit_asked) return;
    reply_finished_steps(now);

    if (input_eof && !keep_open && queue_count == 0 && steps_count == 0) {
        quit_asked = true;
        reply("bye");
        ModuleCommon_requestQuit();
    }
}
