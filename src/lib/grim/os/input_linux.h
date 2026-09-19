#pragma once

// TODO make a grim unified header for the essentials
#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <lib/grim/logger.h>

// Also have a mem.h
#include <lib/grim/mem/arena.h>
#include <lib/grim/mem/buff.h>

#include <dirent.h>
#include <liburing.h>
#include <linux/io_uring.h>
#include <stdatomic.h>

#include <linux/input.h>
#include <linux/limits.h>
#include <sys/inotify.h>

#include <libevdev-1.0/libevdev/libevdev.h>
#include <libinput.h>
#include <libudev.h>

#include <errno.h>

/* This is x86 specific */
#define read_barrier()  __asm__ __volatile__("" ::: "memory")
#define write_barrier() __asm__ __volatile__("" ::: "memory")

#define BLOCK_SZ    1024
#define MAX_DEVICES 32

#define is_bit_set(bit, arr) (arr[(bit) / 8] & (1 << ((bit) % 8)))

void handle_register_error(i32 register_result);

// typedef struct KeyboardState {
//     struct libevdev* dev;
// } KeyboardState;

// typedef struct GamepadState {
//     struct libevdev* dev;
// } GamepadState;

// typedef struct MouseState {
//     struct libevdev* dev;
// } MouseState;

typedef struct InputMethod {
    struct libevdev* dev;
} InputMethod;
STATIC_ASSERT(sizeof(InputMethod) == 8);

typedef struct InputContext {
    // MouseState*    mouse_states;
    // GamepadState*  gamepad_states;
    // KeyboardState* keyboard_states;

    rop(rw InputMethod) states;

    struct {
        u32 key_e      : 1;
        u32 key_s      : 1;
        u32 key_d      : 1;
        u32 key_f      : 1;
        u32 key_space  : 1;
        u32 key_ctrl   : 1;
        u32 key_enter  : 1;
        u32 key_delete : 1;
    };

    f32 joystick_LX;
    f32 joystick_LY;
    f32 joystick_RX;
    f32 joystick_RY;

    // TODO does ZII matter if I have create funcs?
    f32 joystick_rx_sens; // all sens values are offset from 0 so -0.1 would be 0.9
    f32 joystick_ry_sens; // all sens values are offset from 0 so -0.1 would be 0.9
    f32 joystick_lx_sens; // all sens values are offset from 0 so -0.1 would be 0.9
    f32 joystick_ly_sens; // all sens values are offset from 0 so -0.1 would be 0.9

    f32 mouse_dx;
    f32 mouse_dy;

    f32 mouse_x_sens; // all sens values are offset from 0 so -0.1 would be 0.9
    f32 mouse_y_sens; // all sens values are offset from 0 so -0.1 would be 0.9

    u32 _padding;
} InputContext;
STATIC_ASSERT(sizeof(InputContext) == 64);

typedef struct InputState {
    // TODO tie these to specific inputMethods by idx or fd?
    // Or allow them to disable specific ones?
    // This is mostly for multiplayer, having multiple states you call update on
    // Maybe this is a future enhancement, not for this game
    rop(rw InputContext) ipt_ctx;

    // WARN It is important that forward is the first of the inputs
    // We use it's offset to memset this struct
    f32 forward;
    f32 up;
    f32 right;
    f32 cam_forward;
    f32 cam_right;

    struct {
        u32 lock_mouse : 1;
        u32 lock_cam   : 1;
        u32 _padding   : 30;
    };
} InputState;
STATIC_ASSERT(sizeof(InputState) == 32);

struct file_info {
    off_t        file_sz;
    struct iovec iovecs[];
};

void libevdev_log_func(enum libevdev_log_priority priority,
                       void*                      data,
                       const char*                file,
                       int                        line,
                       const char*                func,
                       const char*                format,
                       va_list                    args) {
    VERBOSE_LOG(SCOPE_INPUT_LINUX, "LIBEVDEV_LOG: func: %s & data: %p", func, data);
    switch (priority) {
        case LIBEVDEV_LOG_ERROR:
            _RAW_LOG(LEVEL_ERROR, SCOPE_INPUT_LINUX, file, line, format, args);
            break;
        case LIBEVDEV_LOG_INFO:
            _RAW_LOG(LEVEL_INFO, SCOPE_INPUT_LINUX, file, line, format, args);
            break;
        case LIBEVDEV_LOG_DEBUG:
            _RAW_LOG(LEVEL_DEBUG, SCOPE_INPUT_LINUX, file, line, format, args);
            break;
    };
}

void input_refresh(rop(rw Arena) arena, rop(rw InputContext) ipt_ctx);

InputContext input_ctx_create(rop(rw Arena) arena) {
    libevdev_set_log_function(libevdev_log_func, NULL);

    InputMethod* input_methods = arena_alloc_align(arena, sizeof(void*), sizeof(InputMethod) * MAX_DEVICES);
    InputContext ipt_ctx       = (InputContext){
        .states = input_methods,

        // .key_e     = 0,
        // .key_s     = 0,
        // .key_d     = 0,
        // .key_f     = 0,
        // .key_space = 0,
        //
        // .joystick_LX = 0.0f,
        // .joystick_LY = 0.0f,
        // .joystick_RX = 0.0f,
        // .joystick_RY = 0.0f,
        //
        // .joystick_rx_sens = 0.0f, // all sens values are offset from 0 so -0.1 would be 0.9
        // .joystick_ry_sens = 0.0f, // all sens values are offset from 0 so -0.1 would be 0.9
        // .joystick_lx_sens = 0.0f, // all sens values are offset from 0 so -0.1 would be 0.9
        // .joystick_ly_sens = 0.0f, // all sens values are offset from 0 so -0.1 would be 0.9
        //
        // .mouse_dx = 0.0f,
        // .mouse_dy = 0.0f,
        //
        // .mouse_x_sens = 0.0f, // all sens values are offset from 0 so -0.1 would be 0.9
        // .mouse_y_sens = 0.0f, // all sens values are offset from 0 so -0.1 would be 0.9
        //
        // ._padding = 0,
    };

    input_refresh(arena, &ipt_ctx);

    return ipt_ctx;
}

InputState input_create_state(rop(rw InputContext) ipt_ctx) {
    InputState state = {
        .ipt_ctx = ipt_ctx,
        0,
    };
    return state;
}

void input_cleanup(rop(rw InputContext) ipt_ctx) {
    for (u32 idx = 0; idx < MAX_DEVICES; idx++) {
        if (ipt_ctx->states[idx].dev == NULL) {
            break;
        }

        i32 fd = libevdev_get_fd(ipt_ctx->states[idx].dev);
        libevdev_free(ipt_ctx->states[idx].dev);
        close(fd);
    }
}

// check to see if any new inputs have been plugged in.
// TODO do this through an inotify?
void input_refresh(rop(rw Arena) arena, rop(rw InputContext) ipt_ctx) {
    const char* input_path = "/dev/input";

    u32 input_idx = 0;

    struct dirent* de;
    DIR*           dr = opendir(input_path);
    if (dr == NULL) {
        CRITICAL_LOG(SCOPE_INPUT_LINUX, "Could not open input directory.");
        exit(1);
    }
    while ((de = readdir(dr)) != NULL) {
        char filename[1024] = {0};

        const size_t length = strlen(input_path) + strlen(de->d_name) + 1;
        snprintf(filename, length + 1, "%s/%s", input_path, de->d_name);

        if (strncmp("event", de->d_name, sizeof("event") - 1) == 0) {
            i32 fd = open(filename, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

            if (fd == -1) {
                WARNING_LOG(
                    SCOPE_INPUT_LINUX, "Failed to open input method: %s\n    error: %s", filename, strerror(errno));
            } else {
                VERBOSE_LOG(SCOPE_INPUT_LINUX, "Got: %i", fd);
                VERBOSE_LOG(SCOPE_INPUT_LINUX, "Adding: %s", filename);

                InputMethod* input_method = &ipt_ctx->states[input_idx];

                if (ipt_ctx->states[input_idx].dev != NULL) {
                    i32 fd = libevdev_get_fd(ipt_ctx->states[input_idx].dev);
                    libevdev_free(ipt_ctx->states[input_idx].dev);
                    close(fd);
                }

                struct libevdev* dev    = libevdev_new();
                int              result = libevdev_set_fd(dev, fd);
                if (result != 0) {
                    WARNING_LOG(SCOPE_INPUT_LINUX, "Failed to connect fd to libevdev.");
                }

                // Figure out what type of device this is
                const char* name = libevdev_get_name(dev);

                if (libevdev_has_event_code(dev, EV_ABS, ABS_X) && !libevdev_has_event_code(dev, EV_KEY, BTN_SOUTH)) {
                    // ignore motion sensors for now, they spam output
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "Dropping as motion: %s", name);
                    libevdev_free(dev);
                    continue;
                }

                if (libevdev_has_event_type(dev, EV_KEY) || libevdev_has_event_type(dev, EV_ABS) ||
                    libevdev_has_event_type(dev, EV_REL)) {
                    INFO_LOG(SCOPE_INPUT_LINUX, "Watching device: %s", name);

                    input_method->dev = dev;
                    input_idx++;
                    continue;
                }

                VERBOSE_LOG(SCOPE_INPUT_LINUX, "Dropping as no support: %s", name);
                libevdev_free(dev);
            }
        }
    }
    closedir(dr);
}

void input_update(rop(rw InputState) ipt_state) {
    InputContext* ipt_ctx = ipt_state->ipt_ctx;
    // memset everything beyond ipt_ctx to 0
    memset((u8*)ipt_state + offsetof(InputState, forward), 0, sizeof(InputState) - offsetof(InputState, forward));

    // ipt_state->forward     = 0.0f;
    // ipt_state->up          = 0.0f;
    // ipt_state->right       = 0.0f;
    // ipt_state->cam_forward = 0.0f;
    // ipt_state->cam_right   = 0.0f;
    // ipt_state->lock_mouse  = false;

    // delta based input needs to be reset
    // WARN this is on input_ctx, not input_state rn
    ipt_ctx->mouse_dx = 0.0f;
    ipt_ctx->mouse_dy = 0.0f;

    for (u32 idx = 0; idx < MAX_DEVICES; idx++) {
        if (ipt_ctx->states[idx].dev == NULL) {
            break;
        }

        struct input_event ievent;
        // TODO maybe just manually read abs state?
        // Force sync fucks with the mouse rel values
        // u32                flags = LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_FORCE_SYNC;
        u32                flags = LIBEVDEV_READ_FLAG_NORMAL;
        while (true) {
            i32 result = libevdev_next_event(ipt_ctx->states[idx].dev, flags, &ievent);
            if (result == LIBEVDEV_READ_STATUS_SYNC) {
                flags = LIBEVDEV_READ_FLAG_SYNC;
            } else if (result != LIBEVDEV_READ_STATUS_SUCCESS) {
                // WARNING_LOG(SCOPE_INPUT_LINUX, "An error ocurred getting device events.");
                // DEBUG_LOG(SCOPE_INPUT_LINUX, "result was: %i", result);
                break;
            }

            const char* name = libevdev_get_name(ipt_ctx->states[idx].dev);
            VERBOSE_LOG(SCOPE_INPUT_LINUX, "From dev: %s %i", name, ievent.type);
            switch (ievent.type) {
                // Used as markers to separate events. Events may be separated in time or in space, such as with the
                // multitouch protocol.
                case EV_SYN: {
                    // DEBUG_LOG(SCOPE_INPUT_LINUX, "SYN");
                } break;
                // Used to describe state changes of keyboards, buttons, or other key-like devices.
                case EV_KEY: {
                    // WARN we get a 2 when we hold the key hence the > 0
                    // 0 for EV_KEY for release, 1 for keypress and 2 for autorepeat.
                    // [https://www.kernel.org/doc/Documentation/input/input.txt]
                    //
                    // TODO allow to differentiate between press & hold. I don't like
                    // state tracking (currently this is immediate mode) but press is
                    // valid. Is press event reliable?
                    switch (ievent.code) {
                        case KEY_E: {
                            // ipt_ctx->forward = (f32)ievent.value * 1.0f;
                            ipt_ctx->key_e = ievent.value > 0;
                        } break;
                        case KEY_S: {
                            // ipt_ctx->right = (f32)ievent.value * -1.0f;
                            ipt_ctx->key_s = ievent.value > 0;
                        } break;
                        case KEY_D: {
                            // ipt_ctx->forward = (f32)ievent.value * -1.0f;
                            ipt_ctx->key_d = ievent.value > 0;
                        } break;
                        case KEY_F: {
                            // ipt_ctx->right += (f32)ievent.value * 1.0f;
                            ipt_ctx->key_f = ievent.value > 0;
                        } break;
                        case KEY_SPACE: {
                            // ipt_ctx->up += (f32)ievent.value * 1.0f;
                            ipt_ctx->key_space = ievent.value > 0;
                        } break;
                        case KEY_LEFTCTRL: {
                            // ipt_ctx->up += (f32)ievent.value * 1.0f;
                            ipt_ctx->key_ctrl = ievent.value > 0;
                        } break;
                        case KEY_ENTER: {
                            // ipt_ctx->up += (f32)ievent.value * 1.0f;
                            ipt_ctx->key_enter = ievent.value > 0;
                        } break;
                        case KEY_DELETE: {
                            // ipt_ctx->up += (f32)ievent.value * 1.0f;
                            ipt_ctx->key_delete = ievent.value > 0;
                        } break;
                        default: {
                            INFO_LOG(SCOPE_INPUT_LINUX, "Unsupported key: %u", ievent.code);
                        } break;
                    }
                } break;
                // Used to describe relative axis value changes, e.g. moving the mouse 5 units to the left.
                case EV_REL: {
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "REL: %u, %i", ievent.code, ievent.value);
                    switch (ievent.code) {
                        case REL_X: {
                            // TODO let change sens
                            ipt_ctx->mouse_dx = (f32)ievent.value;
                        } break;
                        case REL_Y: {
                            // TODO let change sens
                            ipt_ctx->mouse_dy = (f32)ievent.value;
                        } break;
                        default: {
                            INFO_LOG(SCOPE_INPUT_LINUX, "Unsupported rel axis: %u.", ievent.code);
                        } break;
                    }
                } break;
                // Used to describe absolute axis value changes, e.g. describing the coordinates of a touch on a
                // touchscreen.
                case EV_ABS: {
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "ABS: %u, %u", ievent.code, ievent.value);
                    const struct input_absinfo* info = libevdev_get_abs_info(ipt_ctx->states[idx].dev, ievent.code);
                    VERBOSE_LOG(SCOPE_INPUT_LINUX,
                                "min is: %i max %i fuzz %i resol %i flat %i val %i",
                                info->minimum,
                                info->maximum,
                                info->fuzz,
                                info->resolution,
                                info->flat,
                                info->value);

                    // TODO this can be simplified a bunch, right?
                    // Also this is technically wrong with min and max
                    f32 range       = (f32)(info->maximum - info->minimum + 1);
                    f32 norm        = (f32)info->value - (range / 2.0f);
                    f32 final_value = (norm / range) * 2.0f;
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "range: %f, norm: %f, Final: %f", range, norm, final_value);
                    switch (ievent.code) {
                        case ABS_X: {
                            ipt_ctx->joystick_LX = final_value;
                        } break;
                        case ABS_Y: {
                            ipt_ctx->joystick_LY = final_value;
                        } break;
                        case ABS_RX: {
                            ipt_ctx->joystick_RX = final_value;
                        } break;
                        case ABS_RY: {
                            ipt_ctx->joystick_RY = final_value;
                        } break;
                        default: {
                            INFO_LOG(SCOPE_INPUT_LINUX, "Unsupported abs axis: %u.", ievent.code);
                        } break;
                    }

                    // libevdev_get_abs_info(ipt_ctx->states[idx].dev, ABS_X);
                    // libevdev_get_abs_info(ipt_ctx->states[idx].dev, ABS_Y);
                    // libevdev_get_abs_info(ipt_ctx->states[idx].dev, ABS_RX);
                    // libevdev_get_abs_info(ipt_ctx->states[idx].dev, ABS_RY);
                } break;
                // Used to describe miscellaneous input data that do not fit into other types.
                case EV_MSC: {
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "MSC");
                } break;
                // Used to describe binary state input switches.
                case EV_SW: {
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "SW");
                } break;
                // Used to turn LEDs on devices on and off.
                case EV_LED: {
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "LED");
                } break;
                // Used to output sound to devices.
                case EV_SND: {
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "SND");
                } break;
                // Used for autorepeating devices.
                case EV_REP: {
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "REP");
                } break;
                // Used to send force feedback commands to an input device.
                case EV_FF: {
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "FF");
                    // TODO to send controller rumble
                    // Event type 21 (EV_FF)
                    // Event code 80 (FF_RUMBLE)
                    // Event code 81 (FF_PERIODIC)
                    // Event code 88 (FF_SQUARE)
                    // Event code 89 (FF_TRIANGLE)
                    // Event code 90 (FF_SINE)
                    // Event code 96 (FF_GAIN)
                } break;
                // A special type for power button and switch input.
                case EV_PWR: {
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "PWR");
                } break;
                // Used to receive force feedback device status.
                case EV_FF_STATUS: {
                    VERBOSE_LOG(SCOPE_INPUT_LINUX, "FF_STATUS");
                } break;
            }
        }
    }

    if (ipt_ctx->key_e) {
        ipt_state->forward += 1.0f;
    }
    if (ipt_ctx->key_s) {
        ipt_state->right += -1.0f;
    }
    if (ipt_ctx->key_d) {
        ipt_state->forward += -1.0f;
    }
    if (ipt_ctx->key_f) {
        ipt_state->right += 1.0f;
    }
    if (ipt_ctx->key_space) {
        ipt_state->up += 1.0f;
    }
    if (ipt_ctx->key_ctrl) {
        ipt_state->up -= 1.0f;
    }

    if (ipt_ctx->key_enter) {
        ipt_state->lock_mouse = true;
    }
    if (ipt_ctx->key_delete) {
        ipt_state->lock_cam = true;
    }

    if (ipt_ctx->joystick_LX != 0.0f) {
        ipt_state->right += ipt_ctx->joystick_LX * (1.0f + ipt_ctx->joystick_lx_sens);
    }
    if (ipt_ctx->joystick_LY != 0.0f) {
        ipt_state->forward += ipt_ctx->joystick_LY * (1.0f + ipt_ctx->joystick_ly_sens);
    }
    if (ipt_ctx->joystick_RY != 0.0f) {
        ipt_state->cam_forward += ipt_ctx->joystick_RY * (1.0f + ipt_ctx->joystick_ry_sens);
    }
    if (ipt_ctx->joystick_RX != 0.0f) {
        ipt_state->cam_right += ipt_ctx->joystick_RX * (1.0f + ipt_ctx->joystick_rx_sens);
    }

    if (ipt_ctx->mouse_dx != 0.0f) {
        ipt_state->cam_right += ipt_ctx->mouse_dx * (1.0f + ipt_ctx->mouse_x_sens);
    }
    if (ipt_ctx->mouse_dy != 0.0f) {
        ipt_state->cam_forward += ipt_ctx->mouse_dy * (1.0f + ipt_ctx->mouse_y_sens);
    }
}

void handle_register_error(i32 register_result) {
    if (register_result < 0) {
        CRITICAL_LOG(SCOPE_INPUT_LINUX, "Failed to register evdev events with the ring.");

        // clang-format off
        switch (register_result) {
            case -EACCES     : { CRITICAL_LOG(SCOPE_INPUT_LINUX, "The opcode field is not allowed due to registered restrictions.");} break;
            case -EBADF      : { CRITICAL_LOG(SCOPE_INPUT_LINUX, "One or more fds in the fd array are invalid.");} break;
            case -EBADFD     : { CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_REGISTER_ENABLE_RINGS or IORING_REGISTER_RESTRICTIONS was specified, but the io_uring ring is not disabled.");} break;
            case -EBUSY      : { CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_REGISTER_BUFFERS or IORING_REGISTER_FILES or IORING_REGISTER_RESTRICTIONS was specified, but there were already buffers, files, or restrictions registered.");} break;
            case -EEXIST     : { CRITICAL_LOG(SCOPE_INPUT_LINUX, "The thread performing the registration is invalid.");} break;
            case -EFAULT     : { 
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "buffer is outside of the process' accessible address space, or iov_len is greater than 1GiB.");
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "User buffers point to file-backed memory (newer kernels).");
                               } break;
            case -EINVAL     : { 
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_REGISTER_BUFFERS or IORING_REGISTER_FILES was specified, but nr_args is 0.");
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_REGISTER_BUFFERS was specified, but nr_args exceeds UIO_MAXIOV");
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_UNREGISTER_BUFFERS or IORING_UNREGISTER_FILES was specified, and nr_args is non-zero or arg is non-NULL.");
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_REGISTER_RESTRICTIONS was specified, but nr_args exceeds the maximum allowed number of restrictions or restriction opcode is invalid.");
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_REGISTER_PBUF_STATUS was specified, but the valid buffer group specified by buf_group did not refer to a buffer group registered via IORING_REGISTER_PBUF_RING.");
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_REGISTER_NAPI was specified, but the ring associated with fd has not been created with IORING_SETUP_IOPOLL.");
                               } break;
            case -EMFILE     : { 
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_REGISTER_FILES was specified and nr_args exceeds the maximum allowed number of files in a fixed file set.");
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_REGISTER_FILES was specified and adding nr_args file references would exceed the maximum allowed number of files the user is allowed to have according to the RLIMIT_NOFILE resource limit and the caller does not have CAP_SYS_RESOURCE capability. Note that this is a per user limit, not per process.");
                               } break;
            case -ENOMEM     : { CRITICAL_LOG(SCOPE_INPUT_LINUX, "Insufficient kernel resources are available, or the caller had a non-zero RLIMIT_MEMLOCK soft resource limit, but tried to lock more memory than the limit permitted.  This limit is not enforced if the process is privileged (CAP_IPC_LOCK).");} break;
            case -ENXIO      : { 
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_UNREGISTER_BUFFERS or IORING_UNREGISTER_FILES was specified, but there were no buffers or files registered.");
                                   CRITICAL_LOG(SCOPE_INPUT_LINUX, "Attempt to register files or buffers on an io_uring instance that is already undergoing file or buffer registration, or is being torn down.");
                               } break;
            case -EOPNOTSUPP : { CRITICAL_LOG(SCOPE_INPUT_LINUX, "User buffers point to file-backed memory.");} break;
            case -ENOENT     : { CRITICAL_LOG(SCOPE_INPUT_LINUX, "IORING_REGISTER_PBUF_STATUS was specified, but buf_group did not refer to a currently valid buffer group.");} break;
            default : { CRITICAL_LOG(SCOPE_INPUT_LINUX, "Unknown error ocurred trying to register input ring."); } break;
        }
        // clang-format on

        exit(1);
    }
}
