//
// Copyright(C) 2026 Koen Kooi
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	doomgeneric for Wayland, drawing into wl_shm buffers.
//

#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "dg_present.h"
#include "doomdef.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_controls.h"

#if !defined(CMAP256) || DOOMGENERIC_RESX != SCREENWIDTH || DOOMGENERIC_RESY != SCREENHEIGHT
#error The Wayland platform needs -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
#endif

#define KEYQUEUE_SIZE 64
#define NUM_EVDEV_KEYS 768
#define NUM_BUFFERS 2
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define MAX_ASPECT 4096
#define MAX_SURFACE_SIZE 16384

struct buffer
{
	struct wl_buffer *wl_buffer;
	void *data;
	size_t size;
	int busy;
	int stale;
	int drawnFull;
	uint32_t gen;		// the tracker frame this buffer last showed
};

struct keybind
{
	int code;
	int key;
	int *var;
};

static struct wl_display *s_Display;
static struct wl_registry *s_Registry;
static struct wl_compositor *s_Compositor;
static struct wl_shm *s_Shm;
static struct xdg_wm_base *s_WmBase;
static struct zwp_idle_inhibit_manager_v1 *s_IdleInhibitManager;
static struct wl_seat *s_Seat;
static struct wl_keyboard *s_Keyboard;
static struct wl_surface *s_Surface;
static struct xdg_surface *s_XdgSurface;
static struct xdg_toplevel *s_Toplevel;
static struct wl_callback *s_FrameCallback;

static uint32_t s_Format = WL_SHM_FORMAT_XRGB8888;
static int s_HasRgb565;
static int s_Bpp = 4;

static struct buffer s_Buffers[NUM_BUFFERS];

// The size of the buffers, and of the picture inside them.
static int s_Width;
static int s_Height;
static int s_PicX, s_PicY, s_PicWidth, s_PicHeight;
static dgp_t s_Present;

static int s_NewWidth, s_NewHeight;
static int s_BoundsWidth, s_BoundsHeight;
static int s_Resize;

static int s_AspectWidth = 4;
static int s_AspectHeight = 3;
static int s_Stretch;
static int s_Fullscreen;
static const char *s_AppId = "doomgeneric";
static const char *s_Title;

static uint16_t s_Pal565[256];
static uint32_t s_Pal8888[256];
static int s_PaletteValid;

static int s_Configured;
static int s_NeedRedraw;
static volatile sig_atomic_t s_Quit;
static int s_DamageBuffer;
static int s_Direct;
static int s_PresentMode = DGP_MODE_OLD;
static int s_DamageMode = DGP_DAMAGE_FULL;
static int s_DamageRects = 4;
static int s_DamageNoSkip;

static struct timespec s_StartTime;

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static struct keybind s_KeyMap[NUM_EVDEV_KEYS];
static unsigned char s_HeldKeys[NUM_EVDEV_KEYS];

// Default bindings for a US layout, indexed by Linux evdev code.
#define LIT(code, key) { code, key, NULL }
#define VAR(code, var) { code, 0, &var }

static const struct keybind s_DefaultKeyMap[] =
{
	LIT(1, KEY_ESCAPE),
	LIT(2, '1'), LIT(3, '2'), LIT(4, '3'), LIT(5, '4'), LIT(6, '5'),
	LIT(7, '6'), LIT(8, '7'), LIT(9, '8'), LIT(10, '9'), LIT(11, '0'),
	LIT(12, KEY_MINUS), LIT(13, KEY_EQUALS), LIT(14, KEY_BACKSPACE),
	LIT(15, KEY_TAB),
	LIT(16, 'q'), LIT(17, 'w'), LIT(18, 'e'), LIT(19, 'r'), LIT(20, 't'),
	LIT(21, 'y'), LIT(22, 'u'), LIT(23, 'i'), LIT(24, 'o'), LIT(25, 'p'),
	VAR(26, key_prevweapon), VAR(27, key_nextweapon),
	LIT(28, KEY_ENTER),
	VAR(29, key_fire),
	LIT(30, 'a'), LIT(31, 's'), LIT(32, 'd'), LIT(33, 'f'), LIT(34, 'g'),
	LIT(35, 'h'), LIT(36, 'j'), LIT(37, 'k'), LIT(38, 'l'),
	LIT(39, ';'), LIT(40, '\''), LIT(41, '`'),
	VAR(42, key_speed),
	LIT(43, '\\'),
	LIT(44, 'z'), LIT(45, 'x'), LIT(46, 'c'), LIT(47, 'v'), LIT(48, 'b'),
	LIT(49, 'n'), LIT(50, 'm'),
	VAR(51, key_strafeleft), VAR(52, key_straferight),
	LIT(53, '/'),
	VAR(54, key_speed),
	VAR(56, key_strafe),
	VAR(57, key_use),
	LIT(59, KEY_F1), LIT(60, KEY_F2), LIT(61, KEY_F3), LIT(62, KEY_F4),
	LIT(63, KEY_F5), LIT(64, KEY_F6), LIT(65, KEY_F7), LIT(66, KEY_F8),
	LIT(67, KEY_F9), LIT(68, KEY_F10), LIT(87, KEY_F11), LIT(88, KEY_F12),
	LIT(96, KEY_ENTER),
	VAR(97, key_fire),
	VAR(100, key_strafe),
	LIT(102, KEY_HOME), LIT(103, KEY_UPARROW), LIT(104, KEY_PGUP),
	LIT(105, KEY_LEFTARROW), LIT(106, KEY_RIGHTARROW), LIT(107, KEY_END),
	LIT(108, KEY_DOWNARROW), LIT(109, KEY_PGDN),
	LIT(110, KEY_INS), LIT(111, KEY_DEL),
	VAR(119, key_pause),
};

struct keyname
{
	const char *name;
	int key;
};

struct actionname
{
	const char *name;
	int *var;
};

static const struct keyname s_KeyNames[] =
{
	{ "escape", KEY_ESCAPE }, { "enter", KEY_ENTER }, { "tab", KEY_TAB },
	{ "backspace", KEY_BACKSPACE }, { "space", ' ' },
	{ "comma", ',' }, { "period", '.' },
	{ "minus", KEY_MINUS }, { "equals", KEY_EQUALS },
	{ "uparrow", KEY_UPARROW }, { "downarrow", KEY_DOWNARROW },
	{ "leftarrow", KEY_LEFTARROW }, { "rightarrow", KEY_RIGHTARROW },
	{ "home", KEY_HOME }, { "end", KEY_END },
	{ "pgup", KEY_PGUP }, { "pgdn", KEY_PGDN },
	{ "ins", KEY_INS }, { "del", KEY_DEL },
	{ "rshift", KEY_RSHIFT }, { "rctrl", KEY_RCTRL }, { "ralt", KEY_RALT },
	{ "capslock", KEY_CAPSLOCK },
	{ "f1", KEY_F1 }, { "f2", KEY_F2 }, { "f3", KEY_F3 }, { "f4", KEY_F4 },
	{ "f5", KEY_F5 }, { "f6", KEY_F6 }, { "f7", KEY_F7 }, { "f8", KEY_F8 },
	{ "f9", KEY_F9 }, { "f10", KEY_F10 }, { "f11", KEY_F11 },
	{ "f12", KEY_F12 },
};

// These follow the engine's bindings when the key is pressed.
static const struct actionname s_ActionNames[] =
{
	{ "up", &key_up }, { "down", &key_down },
	{ "left", &key_left }, { "right", &key_right },
	{ "strafeleft", &key_strafeleft }, { "straferight", &key_straferight },
	{ "fire", &key_fire }, { "use", &key_use },
	{ "strafe", &key_strafe }, { "speed", &key_speed },
	{ "prevweapon", &key_prevweapon }, { "nextweapon", &key_nextweapon },
	{ "weapon1", &key_weapon1 }, { "weapon2", &key_weapon2 },
	{ "weapon3", &key_weapon3 }, { "weapon4", &key_weapon4 },
	{ "weapon5", &key_weapon5 }, { "weapon6", &key_weapon6 },
	{ "weapon7", &key_weapon7 }, { "weapon8", &key_weapon8 },
	{ "map", &key_map_toggle }, { "menu", &key_menu_activate },
	{ "pause", &key_pause },
};

static void connectionLost(void)
{
	int err = wl_display_get_error(s_Display);

	if (err == EPROTO)
	{
		const struct wl_interface *interface;
		uint32_t id;
		uint32_t code = wl_display_get_protocol_error(s_Display, &interface,
		                                              &id);

		I_Error("Wayland protocol error %u on %s@%u", code,
		        interface ? interface->name : "unknown", id);
	}

	I_Error("Wayland connection lost: %s", strerror(err ? err : errno));
}

static void handleSignal(int sig)
{
	(void)sig;
	s_Quit = 1;
}

// Only presses are dropped when the queue fills up, so that a key whose
// press was queued always gets its release.
static int addKeyToQueue(int pressed, unsigned char key)
{
	unsigned int next = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
	unsigned int used = (s_KeyQueueWriteIndex + KEYQUEUE_SIZE
	                     - s_KeyQueueReadIndex) % KEYQUEUE_SIZE;

	if (next == s_KeyQueueReadIndex || (pressed && used >= KEYQUEUE_SIZE / 2))
	{
		return 0;
	}

	s_KeyQueue[s_KeyQueueWriteIndex] = (pressed << 8) | key;
	s_KeyQueueWriteIndex = next;

	return 1;
}

static void keyEvent(unsigned int code, int pressed)
{
	if (code >= NUM_EVDEV_KEYS)
	{
		return;
	}

	if (pressed)
	{
		const struct keybind *bind = &s_KeyMap[code];
		int key = bind->var ? *bind->var : bind->key;

		if (s_HeldKeys[code] || key <= 0 || key > 255)
		{
			return;
		}

		// A release must send what the press sent, even if the engine
		// variable changed in between.
		if (addKeyToQueue(1, key))
		{
			s_HeldKeys[code] = key;
		}
	}
	else if (s_HeldKeys[code])
	{
		addKeyToQueue(0, s_HeldKeys[code]);
		s_HeldKeys[code] = 0;
	}
}

static void releaseAllKeys(void)
{
	unsigned int code;

	for (code = 0; code < NUM_EVDEV_KEYS; code++)
	{
		keyEvent(code, 0);
	}
}

static void keyboardKeymap(void *data, struct wl_keyboard *keyboard,
                           uint32_t format, int32_t fd, uint32_t size)
{
	(void)data; (void)keyboard; (void)format; (void)size;
	close(fd);
}

static void keyboardEnter(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, struct wl_surface *surface,
                          struct wl_array *keys)
{
	(void)data; (void)keyboard; (void)serial; (void)surface; (void)keys;
}

static void keyboardLeave(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, struct wl_surface *surface)
{
	(void)data; (void)keyboard; (void)serial; (void)surface;
	releaseAllKeys();
}

static void keyboardKey(void *data, struct wl_keyboard *keyboard,
                        uint32_t serial, uint32_t time, uint32_t key,
                        uint32_t state)
{
	(void)data; (void)keyboard; (void)serial; (void)time;
	keyEvent(key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
}

static void keyboardModifiers(void *data, struct wl_keyboard *keyboard,
                              uint32_t serial, uint32_t depressed,
                              uint32_t latched, uint32_t locked,
                              uint32_t group)
{
	(void)data; (void)keyboard; (void)serial; (void)depressed;
	(void)latched; (void)locked; (void)group;
}

static void keyboardRepeatInfo(void *data, struct wl_keyboard *keyboard,
                               int32_t rate, int32_t delay)
{
	(void)data; (void)keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener s_KeyboardListener =
{
	.keymap = keyboardKeymap,
	.enter = keyboardEnter,
	.leave = keyboardLeave,
	.key = keyboardKey,
	.modifiers = keyboardModifiers,
	.repeat_info = keyboardRepeatInfo,
};

static void seatCapabilities(void *data, struct wl_seat *seat,
                             uint32_t capabilities)
{
	(void)data;

	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !s_Keyboard)
	{
		s_Keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(s_Keyboard, &s_KeyboardListener, NULL);
	}
	else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && s_Keyboard)
	{
		releaseAllKeys();
		if (wl_keyboard_get_version(s_Keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
		{
			wl_keyboard_release(s_Keyboard);
		}
		else
		{
			wl_keyboard_destroy(s_Keyboard);
		}
		s_Keyboard = NULL;
	}
}

static void seatName(void *data, struct wl_seat *seat, const char *name)
{
	(void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener s_SeatListener =
{
	.capabilities = seatCapabilities,
	.name = seatName,
};

static void shmFormat(void *data, struct wl_shm *shm, uint32_t format)
{
	(void)data; (void)shm;

	if (format == WL_SHM_FORMAT_RGB565)
	{
		s_HasRgb565 = 1;
	}
}

static const struct wl_shm_listener s_ShmListener =
{
	.format = shmFormat,
};

static void wmBasePing(void *data, struct xdg_wm_base *wmBase,
                       uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wmBase, serial);
}

static const struct xdg_wm_base_listener s_WmBaseListener =
{
	.ping = wmBasePing,
};

static void xdgSurfaceConfigure(void *data, struct xdg_surface *surface,
                                uint32_t serial)
{
	int width = s_NewWidth;
	int height = s_NewHeight;

	(void)data;
	xdg_surface_ack_configure(surface, serial);

	// A buffer must stay below the 2 GiB the shm pool size can express.
	if (width > MAX_SURFACE_SIZE)
	{
		width = MAX_SURFACE_SIZE;
	}
	if (height > MAX_SURFACE_SIZE)
	{
		height = MAX_SURFACE_SIZE;
	}

	// A size of zero leaves the choice to the client.
	if (width <= 0)
	{
		width = s_Width ? s_Width : DEFAULT_WIDTH;
		if (!s_Width && s_BoundsWidth > 0 && width > s_BoundsWidth)
		{
			width = s_BoundsWidth;
		}
	}
	if (height <= 0)
	{
		height = s_Height ? s_Height : DEFAULT_HEIGHT;
		if (!s_Height && s_BoundsHeight > 0 && height > s_BoundsHeight)
		{
			height = s_BoundsHeight;
		}
	}

	if (width != s_Width || height != s_Height)
	{
		s_NewWidth = width;
		s_NewHeight = height;
		s_Resize = 1;
	}
	else
	{
		s_Resize = 0;
	}

	s_Configured = 1;
	s_NeedRedraw = 1;
}

static const struct xdg_surface_listener s_XdgSurfaceListener =
{
	.configure = xdgSurfaceConfigure,
};

static void toplevelConfigure(void *data, struct xdg_toplevel *toplevel,
                              int32_t width, int32_t height,
                              struct wl_array *states)
{
	(void)data; (void)toplevel; (void)states;
	s_NewWidth = width;
	s_NewHeight = height;
}

static void toplevelClose(void *data, struct xdg_toplevel *toplevel)
{
	(void)data; (void)toplevel;
	s_Quit = 1;
}

static void toplevelConfigureBounds(void *data, struct xdg_toplevel *toplevel,
                                    int32_t width, int32_t height)
{
	(void)data; (void)toplevel;
	s_BoundsWidth = width;
	s_BoundsHeight = height;
}

static const struct xdg_toplevel_listener s_ToplevelListener =
{
	.configure = toplevelConfigure,
	.close = toplevelClose,
	.configure_bounds = toplevelConfigureBounds,
};

static void registryGlobal(void *data, struct wl_registry *registry,
                           uint32_t name, const char *interface,
                           uint32_t version)
{
	(void)data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
	{
		s_Compositor = wl_registry_bind(registry, name,
		                                &wl_compositor_interface,
		                                version < 4 ? version : 4);
		s_DamageBuffer = version >= 4;
	}
	else if (strcmp(interface, wl_shm_interface.name) == 0)
	{
		s_Shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		wl_shm_add_listener(s_Shm, &s_ShmListener, NULL);
	}
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
	{
		s_WmBase = wl_registry_bind(registry, name,
		                            &xdg_wm_base_interface,
		                            version < 4 ? version : 4);
		xdg_wm_base_add_listener(s_WmBase, &s_WmBaseListener, NULL);
	}
	else if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0)
	{
		s_IdleInhibitManager = wl_registry_bind(registry, name,
		                                        &zwp_idle_inhibit_manager_v1_interface,
		                                        1);
	}
	else if (strcmp(interface, wl_seat_interface.name) == 0 && !s_Seat)
	{
		s_Seat = wl_registry_bind(registry, name, &wl_seat_interface,
		                          version < 5 ? version : 5);
		wl_seat_add_listener(s_Seat, &s_SeatListener, NULL);
	}
}

static void registryGlobalRemove(void *data, struct wl_registry *registry,
                                 uint32_t name)
{
	(void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener s_RegistryListener =
{
	.global = registryGlobal,
	.global_remove = registryGlobalRemove,
};

static void frameDone(void *data, struct wl_callback *callback, uint32_t time)
{
	(void)data; (void)time;
	wl_callback_destroy(callback);
	s_FrameCallback = NULL;
}

static const struct wl_callback_listener s_FrameListener =
{
	.done = frameDone,
};

static void destroyBuffer(struct buffer *buffer)
{
	if (buffer->wl_buffer)
	{
		wl_buffer_destroy(buffer->wl_buffer);
		munmap(buffer->data, buffer->size);
		buffer->wl_buffer = NULL;
	}
	buffer->stale = 0;
}

static void bufferRelease(void *data, struct wl_buffer *wlBuffer)
{
	struct buffer *buffer = data;

	(void)wlBuffer;
	buffer->busy = 0;
	if (buffer->stale)
	{
		destroyBuffer(buffer);
	}
}

static const struct wl_buffer_listener s_BufferListener =
{
	.release = bufferRelease,
};

static void pump(int timeoutMs)
{
	struct pollfd pfd;
	int flushBlocked = 0;
	int ret;

	while (wl_display_prepare_read(s_Display) != 0)
	{
		if (wl_display_dispatch_pending(s_Display) < 0)
		{
			connectionLost();
		}
	}

	if (wl_display_flush(s_Display) < 0)
	{
		if (errno != EAGAIN)
		{
			wl_display_cancel_read(s_Display);
			connectionLost();
		}
		flushBlocked = 1;
	}

	pfd.fd = wl_display_get_fd(s_Display);
	pfd.events = POLLIN | (flushBlocked ? POLLOUT : 0);
	pfd.revents = 0;

	ret = poll(&pfd, 1, timeoutMs);

	if (ret < 0 && errno != EINTR)
	{
		wl_display_cancel_read(s_Display);
		connectionLost();
	}

	if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)))
	{
		if (wl_display_read_events(s_Display) < 0)
		{
			connectionLost();
		}
	}
	else
	{
		wl_display_cancel_read(s_Display);
	}

	if (wl_display_dispatch_pending(s_Display) < 0)
	{
		connectionLost();
	}

	if (s_Quit)
	{
		I_Quit();
	}
}

static void createBuffer(struct buffer *buffer)
{
	struct wl_shm_pool *pool;
	int stride = s_Width * s_Bpp;
	int fd;

	buffer->size = (size_t)stride * s_Height;

	fd = memfd_create("doomgeneric", MFD_CLOEXEC);
	if (fd < 0)
	{
		I_Error("memfd_create failed: %s", strerror(errno));
	}

	if (ftruncate(fd, buffer->size) < 0)
	{
		I_Error("Cannot size the shared memory buffer: %s",
		        strerror(errno));
	}

	buffer->data = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE,
	                    MAP_SHARED, fd, 0);
	if (buffer->data == MAP_FAILED)
	{
		I_Error("Cannot map the shared memory buffer: %s",
		        strerror(errno));
	}

	pool = wl_shm_create_pool(s_Shm, fd, buffer->size);
	buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0, s_Width,
	                                              s_Height, stride,
	                                              s_Format);
	wl_shm_pool_destroy(pool);
	close(fd);

	buffer->busy = 0;
	buffer->stale = 0;
	buffer->drawnFull = 0;
	buffer->gen = 0;
	wl_buffer_add_listener(buffer->wl_buffer, &s_BufferListener, buffer);
}

static void updatePalette(void)
{
	uint16_t pal565[256];
	uint32_t pal8888[256];
	int i;

	for (i = 0; i < 256; i++)
	{
		const struct color *c = &colors[i];

		pal565[i] = ((c->r >> 3) << 11) | ((c->g >> 2) << 5) | (c->b >> 3);
		pal8888[i] = (c->r << 16) | (c->g << 8) | c->b;
	}

	// The engine sets the palette often with the same colours; only a real
	// change makes every row differ.
	if (s_PaletteValid && (memcmp(pal565, s_Pal565, sizeof(pal565)) != 0
	 || memcmp(pal8888, s_Pal8888, sizeof(pal8888)) != 0))
	{
		DGP_PaletteChanged(&s_Present);
	}

	memcpy(s_Pal565, pal565, sizeof(pal565));
	memcpy(s_Pal8888, pal8888, sizeof(pal8888));
	s_PaletteValid = 1;
	palette_changed = false;
}

static int parseKeyName(const char *name, struct keybind *bind)
{
	unsigned int i;

	bind->key = 0;
	bind->var = NULL;

	if (strcmp(name, "none") == 0)
	{
		return 0;
	}

	for (i = 0; i < sizeof(s_ActionNames) / sizeof(s_ActionNames[0]); i++)
	{
		if (strcmp(name, s_ActionNames[i].name) == 0)
		{
			bind->var = s_ActionNames[i].var;
			return 0;
		}
	}

	for (i = 0; i < sizeof(s_KeyNames) / sizeof(s_KeyNames[0]); i++)
	{
		if (strcmp(name, s_KeyNames[i].name) == 0)
		{
			bind->key = s_KeyNames[i].key;
			return 0;
		}
	}

	if (name[0] >= ' ' && name[0] <= '~' && name[1] == '\0')
	{
		bind->key = tolower((unsigned char)name[0]);
		return 0;
	}

	return -1;
}

static void parseKeyMap(const char *arg)
{
	char *copy = strdup(arg);
	char *save = NULL;
	char *entry;

	if (!copy)
	{
		I_Error("Out of memory");
	}

	for (entry = strtok_r(copy, ",", &save); entry;
	     entry = strtok_r(NULL, ",", &save))
	{
		struct keybind bind;
		char *end;
		char *name = strchr(entry, '=');
		long code;

		if (!name)
		{
			I_Error("-keymap entry '%s' is not CODE=NAME", entry);
		}
		*name++ = '\0';

		code = strtol(entry, &end, 10);
		if (*entry == '\0' || *end != '\0' || code < 1
		 || code >= NUM_EVDEV_KEYS)
		{
			I_Error("-keymap entry '%s=%s' has no valid key code",
			        entry, name);
		}

		if (parseKeyName(name, &bind) < 0)
		{
			I_Error("-keymap entry '%s=%s' has an unknown key name",
			        entry, name);
		}

		bind.code = code;
		s_KeyMap[code] = bind;
	}

	free(copy);
}

static void parseOptions(void)
{
	int p = M_CheckParmWithArgs("-aspect", 1);

	if (p)
	{
		char *end;
		long w = strtol(myargv[p + 1], &end, 10);
		long h = 0;

		if (*end == ':')
		{
			h = strtol(end + 1, &end, 10);
		}

		if (*end != '\0' || w < 1 || w > MAX_ASPECT || h < 1 || h > MAX_ASPECT)
		{
			I_Error("-aspect needs a ratio such as 4:3, got '%s'",
			        myargv[p + 1]);
		}

		s_AspectWidth = w;
		s_AspectHeight = h;
	}

	s_Direct = M_ParmExists("-present-direct");
	DG_DirectVideoBuffer = s_Direct;

	p = M_CheckParmWithArgs("-presentmode", 1);
	if (p)
	{
		if (strcmp(myargv[p + 1], "old") == 0)
		{
			s_PresentMode = DGP_MODE_OLD;
		}
		else if (strcmp(myargv[p + 1], "new") == 0)
		{
			s_PresentMode = DGP_MODE_NEW;
		}
		else
		{
			I_Error("-presentmode needs old or new, got '%s'", myargv[p + 1]);
		}
	}

	p = M_CheckParmWithArgs("-damage", 1);
	if (p)
	{
		if (strcmp(myargv[p + 1], "full") == 0)
		{
			s_DamageMode = DGP_DAMAGE_FULL;
		}
		else if (strcmp(myargv[p + 1], "sbar") == 0)
		{
			s_DamageMode = DGP_DAMAGE_SBAR;
		}
		else if (strcmp(myargv[p + 1], "rows") == 0)
		{
			s_DamageMode = DGP_DAMAGE_ROWS;
		}
		else
		{
			I_Error("-damage needs full, sbar or rows, got '%s'",
			        myargv[p + 1]);
		}
	}

	p = M_CheckParmWithArgs("-damage-rects", 1);
	if (p)
	{
		char *end;
		long n = strtol(myargv[p + 1], &end, 10);

		if (*end != '\0' || n < 1 || n > DGP_MAX_RECTS)
		{
			I_Error("-damage-rects needs a number from 1 to %d, got '%s'",
			        DGP_MAX_RECTS, myargv[p + 1]);
		}
		s_DamageRects = n;
	}

	s_DamageNoSkip = M_ParmExists("-damage-noskip");

	s_Stretch = M_ParmExists("-stretch");
	s_Fullscreen = M_ParmExists("-fullscreen");

	p = M_CheckParmWithArgs("-appid", 1);
	if (p)
	{
		s_AppId = myargv[p + 1];
	}

	p = M_CheckParmWithArgs("-title", 1);
	if (p)
	{
		s_Title = myargv[p + 1];
	}

	p = M_CheckParmWithArgs("-keymap", 1);
	if (p)
	{
		parseKeyMap(myargv[p + 1]);
	}
}

static const uint8_t *presentSource(void)
{
	return s_Direct ? I_VideoBuffer : DG_ScreenBuffer;
}

static void applyResize(void)
{
	int i;

	s_Width = s_NewWidth;
	s_Height = s_NewHeight;
	s_Resize = 0;

	if (s_Stretch)
	{
		s_PicWidth = s_Width;
		s_PicHeight = s_Height;
	}
	else
	{
		s_PicWidth = s_Width;
		s_PicHeight = (s_Width * s_AspectHeight + s_AspectWidth / 2) / s_AspectWidth;
		if (s_PicHeight > s_Height)
		{
			s_PicHeight = s_Height;
			s_PicWidth = (s_Height * s_AspectWidth + s_AspectHeight / 2) / s_AspectHeight;
		}
	}
	if (s_PicWidth < 1)
	{
		s_PicWidth = 1;
	}
	if (s_PicHeight < 1)
	{
		s_PicHeight = 1;
	}

	s_PicX = (s_Width - s_PicWidth) / 2;
	s_PicY = (s_Height - s_PicHeight) / 2;

	if (DGP_Resize(&s_Present, s_Width, s_Height, s_PicX, s_PicY, s_PicWidth,
	               s_PicHeight, s_Bpp, presentSource()) < 0)
	{
		I_Error("Out of memory for the scaling tables");
	}

	for (i = 0; i < NUM_BUFFERS; i++)
	{
		if (s_Buffers[i].wl_buffer && !s_Buffers[i].busy)
		{
			destroyBuffer(&s_Buffers[i]);
		}
		else
		{
			s_Buffers[i].stale = 1;
		}
	}
}

static void damage(int x, int y, int width, int height)
{
	if (s_DamageBuffer)
	{
		wl_surface_damage_buffer(s_Surface, x, y, width, height);
	}
	else
	{
		wl_surface_damage(s_Surface, x, y, width, height);
	}
}

static void printPresentStats(void)
{
	char line[512];

	DGP_StatsString(&s_Present, line, sizeof(line));
	puts(line);
}

void DG_Init()
{
	struct sigaction sa;
	unsigned int i;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handleSignal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	// The engine's defaults leave weapon cycling unbound.
	if (key_prevweapon == 0)
	{
		key_prevweapon = '[';
	}
	if (key_nextweapon == 0)
	{
		key_nextweapon = ']';
	}

	for (i = 0; i < sizeof(s_DefaultKeyMap) / sizeof(s_DefaultKeyMap[0]); i++)
	{
		s_KeyMap[s_DefaultKeyMap[i].code] = s_DefaultKeyMap[i];
	}

	parseOptions();

	s_Display = wl_display_connect(NULL);
	if (!s_Display)
	{
		I_Error("Cannot connect to the Wayland display: %s",
		        strerror(errno));
	}

	s_Registry = wl_display_get_registry(s_Display);
	wl_registry_add_listener(s_Registry, &s_RegistryListener, NULL);
	wl_display_roundtrip(s_Display);
	wl_display_roundtrip(s_Display);

	if (!s_Compositor)
	{
		I_Error("The compositor does not offer wl_compositor");
	}
	if (!s_Shm)
	{
		I_Error("The compositor does not offer wl_shm");
	}
	if (!s_WmBase)
	{
		I_Error("The compositor does not offer xdg_wm_base");
	}

	if (s_HasRgb565)
	{
		s_Format = WL_SHM_FORMAT_RGB565;
		s_Bpp = 2;
	}

	s_Surface = wl_compositor_create_surface(s_Compositor);
	if (s_IdleInhibitManager)
	{
		zwp_idle_inhibit_manager_v1_create_inhibitor(s_IdleInhibitManager,
		                                             s_Surface);
	}
	s_XdgSurface = xdg_wm_base_get_xdg_surface(s_WmBase, s_Surface);
	xdg_surface_add_listener(s_XdgSurface, &s_XdgSurfaceListener, NULL);
	s_Toplevel = xdg_surface_get_toplevel(s_XdgSurface);
	xdg_toplevel_add_listener(s_Toplevel, &s_ToplevelListener, NULL);
	xdg_toplevel_set_app_id(s_Toplevel, s_AppId);
	xdg_toplevel_set_title(s_Toplevel, s_Title ? s_Title : "Doom");
	if (s_Fullscreen)
	{
		xdg_toplevel_set_fullscreen(s_Toplevel, NULL);
	}
	wl_surface_commit(s_Surface);

	while (!s_Configured)
	{
		pump(100);
	}

	clock_gettime(CLOCK_MONOTONIC, &s_StartTime);

	if (DGP_Init(&s_Present, s_PresentMode, s_DamageMode, s_DamageRects,
	             s_DamageNoSkip) < 0)
	{
		I_Error("Out of memory for the present state");
	}
	// Not I_AtExit: D_Endoom calls exit(0) before the functions registered
	// earlier, so a normal quit would never reach this one.
	atexit(printPresentStats);

	printf("DG_Init: present direct=%d mode=%s damage=%s rects=%d skip=%d\n",
	       s_Direct, s_PresentMode == DGP_MODE_NEW ? "new" : "old",
	       s_DamageMode == DGP_DAMAGE_ROWS ? "rows" :
	       s_DamageMode == DGP_DAMAGE_SBAR ? "sbar" : "full",
	       s_DamageRects, !s_DamageNoSkip);
}

void DG_DrawFrame()
{
	struct buffer *buffer = NULL;
	dgp_plan_t plan;
	int i;

	pump(0);

	if (s_Resize)
	{
		applyResize();
	}

	if (s_FrameCallback && !s_NeedRedraw)
	{
		return;
	}

	for (i = 0; i < NUM_BUFFERS; i++)
	{
		if (!s_Buffers[i].busy)
		{
			buffer = &s_Buffers[i];
			break;
		}
	}

	if (!buffer)
	{
		return;
	}

	if (!buffer->wl_buffer)
	{
		createBuffer(buffer);
	}

	if (!s_PaletteValid || palette_changed)
	{
		updatePalette();
	}

	DGP_Plan(&s_Present, presentSource(), buffer->gen, buffer->drawnFull,
	         s_NeedRedraw, &plan);
	if (plan.skip)
	{
		// Nothing changed. The frame callback state stays as it is, so the
		// next call tries again.
		return;
	}

	DGP_Convert(&s_Present, buffer->data, presentSource(), s_Pal565,
	            s_Pal8888, &plan);

	wl_surface_attach(s_Surface, buffer->wl_buffer, 0, 0);
	if (plan.whole_buffer)
	{
		damage(0, 0, s_Width, s_Height);
	}
	else
	{
		for (i = 0; i < plan.nrects; i++)
		{
			damage(s_PicX, s_PicY + plan.rect[i].y0, s_PicWidth,
			       plan.rect[i].y1 - plan.rect[i].y0);
		}
	}
	buffer->drawnFull = 1;
	buffer->gen = plan.gen;

	if (!s_FrameCallback)
	{
		s_FrameCallback = wl_surface_frame(s_Surface);
		wl_callback_add_listener(s_FrameCallback, &s_FrameListener, NULL);
	}

	wl_surface_commit(s_Surface);
	buffer->busy = 1;
	s_NeedRedraw = 0;
}

void DG_SleepMs(uint32_t ms)
{
	pump(ms);
}

uint32_t DG_GetTicksMs()
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	return (uint32_t)(now.tv_sec - s_StartTime.tv_sec) * 1000u +
	       (uint32_t)((now.tv_nsec - s_StartTime.tv_nsec) / 1000000);
}

int DG_GetKey(int* pressed, unsigned char* key)
{
	unsigned short keyData;

	if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
	{
		pump(0);
	}

	if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
	{
		return 0;
	}

	keyData = s_KeyQueue[s_KeyQueueReadIndex];
	s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;

	*pressed = keyData >> 8;
	*key = keyData & 0xFF;

	return 1;
}

void DG_SetWindowTitle(const char * title)
{
	if (s_Toplevel && !s_Title)
	{
		xdg_toplevel_set_title(s_Toplevel, title);
	}
}

int main(int argc, char **argv)
{
	doomgeneric_Create(argc, argv);

	while (1)
	{
		doomgeneric_Tick();
	}

	return 0;
}
