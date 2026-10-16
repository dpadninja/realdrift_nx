/* android_native_unity.c -- the libandroid NDK functions libunity.so imports.
 *
 * Unity is not a NativeActivity: the engine is driven by the JNI-registered natives (main.c),
 * so there is no ANativeActivity glue here, only the raw NDK functions libunity calls:
 *
 *   ANativeWindow_acquire/_release/_fromSurface/_setBuffersGeometry/
 *                _getWidth/_getHeight/_getFormat      -> libnx NWindow
 *   ALooper_prepare/_acquire/_release/_pollOnce/_wake/_forThread
 *                                                     -> condvar wait/wake
 *   ASensorManager_ , ASensorEventQueue_ , ASensor_   -> "no sensors"
 *
 * The engine creates its OWN EGL context from the ANativeWindow; the host must not create an
 * SDL_GL / EGL context (SDL is used for audio + HID only). Also here: the controller / touch
 * feed into nx_pointer and the input hook (android_native_feed_hid).
 *
 * Needs devkitA64 + libnx (switch.h) + switch-mesa. Not host-compilable.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <switch.h>
#include "nx_pointer.h"
#include "libc_shim.h"   /* RD_GAME_HOME, fopen_fake/fclose_fake (locked file IO) */
#include "util.h"   /* debugPrintf */
#include "config.h" /* screen_width / screen_height */
#include "rd_platform.h"  /* RD_TOUCH_PANEL_* */
#include "rd_perf.h"      /* RD_HANDHELD_* */
#include "unity_input_hook.h"

#ifndef AWINDOW_FORMAT_RGBA_8888
#define AWINDOW_FORMAT_RGBA_8888 1
#endif

/* opaque NDK types -> concrete libnx instances */
typedef struct ANativeWindow ANativeWindow;     /* == NWindow* at runtime */
typedef struct ALooper       ALooper;

/* ==========================================================================
 * dock-aware screen state (also read by unity_jni.c's Display getters)
 * ========================================================================== */
static u32 g_w = RD_HANDHELD_W, g_h = RD_HANDHELD_H;

void android_native_update_mode(void){
  /* The NWindow buffer MUST match the resolution Unity renders/reports (screen_width/height),
   * or the game renders at one size into a buffer of another -> zoom/crop. main.c picks it at
   * boot (rd_pick_resolution); mirror it here. */
  if (screen_width > 0 && screen_height > 0) { g_w = (u32)screen_width; g_h = (u32)screen_height; }
}

/* ==========================================================================
 * ANativeWindow  ->  libnx NWindow
 * ========================================================================== */
/* Pin the displayed region to exactly the dimensions Unity renders
 * into. nwindowSetDimensions may allocate a width-aligned (e.g. 720 -> 768)
 * swapchain buffer; without a matching crop the compositor can scan the extra
 * uninitialized columns, which shows up as the image being "cut off" / garbage
 * on the right edge. Cropping to (0,0,bw,bh) guarantees only the rendered
 * content is presented. */
/* LANDSCAPE, NO ROTATION. Real Drift renders landscape, so the buffer (bw x bh, 16:9) maps 1:1
 * to the landscape panel with the identity transform -- no compositor rotation. */
/* Remember the last geometry so we can re-assert it (see nx_window_reassert). */
static u32 g_geom_bw = RD_DOCKED_W, g_geom_bh = RD_DOCKED_H;

static void nx_window_set_geom(NWindow *w, u32 bw, u32 bh) {
  g_geom_bw = bw; g_geom_bh = bh;
  nwindowSetDimensions(w, bw, bh);
  nwindowSetCrop(w, 0, 0, bw, bh);
  nwindowSetTransform(w, 0u);   /* identity: landscape buffer -> landscape panel */
  static int nlog = 0;
  if (nlog < 6) {
    nlog++;
    u32 aw = 0, ah = 0;
    nwindowGetDimensions(w, &aw, &ah);
    debugPrintf("[gfx] set_geom: render %ux%u, nwindow reports %ux%u, transform=0 (landscape)\n",
                bw, bh, aw, ah);
  }
}

/* switch-mesa creates its swapchain inside eglCreateWindowSurface and may reset the NWindow's
 * crop/dimensions to its own defaults; re-assert our crop (and identity transform) afterwards
 * so only the rendered content is presented. Cheap: two property writes. */
void nx_window_reassert(void) {
  NWindow *w = nwindowGetDefault();
  nwindowSetCrop(w, 0, 0, g_geom_bw, g_geom_bh);
  nwindowSetTransform(w, 0u);
}

ANativeWindow *android_native_window(void){
  NWindow *w = nwindowGetDefault();
  nx_window_set_geom(w, g_w, g_h);
  return (ANativeWindow *)w;
}
void     ANativeWindow_acquire(ANativeWindow *w){ (void)w; }                 /* singleton: refcount no-op */
void     ANativeWindow_release(ANativeWindow *w){ (void)w; }
ANativeWindow *ANativeWindow_fromSurface(void *env, void *surface){
  (void)env; (void)surface; return android_native_window();               /* one surface == our window */
}
int32_t  ANativeWindow_getWidth (ANativeWindow *w){ (void)w; return (int32_t)g_w; }
int32_t  ANativeWindow_getHeight(ANativeWindow *w){ (void)w; return (int32_t)g_h; }
int32_t  ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height, int32_t format){
  (void)format;
  if (width > 0 && height > 0) nx_window_set_geom((NWindow *)w, (u32)width, (u32)height);
  return 0;
}

/* ==========================================================================
 * ALooper -- Unity uses it as a per-thread wait/wake primitive (not real fd
 * polling), so a condvar-backed looper is sufficient. If the engine turns out
 * to register real fds, a fake-fd poll layer (as in older so-loader ports) is needed here.
 * ========================================================================== */
#define ALOOPER_POLL_WAKE     (-1)
#define ALOOPER_POLL_TIMEOUT  (-3)
#define MAX_LOOPERS 16

struct ALooper { Mutex m; CondVar cv; int signaled; int refs; u32 owner; int used; };
static struct ALooper g_loopers[MAX_LOOPERS];
static Mutex g_loopers_lock;
static int   g_loopers_init = 0;

static void loopers_once(void){ if(!g_loopers_init){ mutexInit(&g_loopers_lock); g_loopers_init=1; } }

static struct ALooper *looper_for(u32 tid, int create){
  loopers_once();
  mutexLock(&g_loopers_lock);
  for (int i=0;i<MAX_LOOPERS;i++) if (g_loopers[i].used && g_loopers[i].owner==tid){
    struct ALooper *l=&g_loopers[i]; mutexUnlock(&g_loopers_lock); return l; }
  if (create) for (int i=0;i<MAX_LOOPERS;i++) if (!g_loopers[i].used){
    struct ALooper *l=&g_loopers[i];
    l->used=1; l->owner=tid; l->signaled=0; l->refs=1;
    mutexInit(&l->m); condvarInit(&l->cv);
    mutexUnlock(&g_loopers_lock); return l; }
  mutexUnlock(&g_loopers_lock);
  return NULL;
}
static u32 cur_tid(void){ return (u32)(uintptr_t)threadGetCurHandle(); }

ALooper *ALooper_prepare(int opts){ (void)opts; return (ALooper *)looper_for(cur_tid(), 1); }
/* Unity's InitializeUILooper() does `if (ALooper_forThread()==NULL) { log
 * "Couldn't retrieve native ALooper for UI thread"; return; }` -- and leaves the
 * global NdkLooper singleton NULL. A later NdkLooper::CreateHandler() then calls
 * WaitForCreation() on that null pointer -> Data Abort (ldrb [null+0xe8]). On real
 * Android the UI thread already owns a Java-created looper; we have none, so return
 * a create-on-demand looper (like ALooper_pollOnce already does) so the NdkLooper
 * is constructed (its ctor sets the [+0xe8] "created" flag WaitForCreation reads). */
ALooper *ALooper_forThread(void){  return (ALooper *)looper_for(cur_tid(), 1); }
void     ALooper_acquire(ALooper *l){ struct ALooper *L=(void*)l; if(L){ mutexLock(&L->m); L->refs++; mutexUnlock(&L->m);} }
void     ALooper_release(ALooper *l){ struct ALooper *L=(void*)l; if(L){ mutexLock(&L->m); if(--L->refs<=0) L->used=0; mutexUnlock(&L->m);} }

void ALooper_wake(ALooper *l){
  struct ALooper *L=(void*)l; if(!L) return;
  mutexLock(&L->m); L->signaled=1; condvarWakeAll(&L->cv); mutexUnlock(&L->m);
}
int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData){
  struct ALooper *L = (void*)looper_for(cur_tid(), 1);
  if (outFd) *outFd=0;
  if (outEvents) *outEvents=0;
  if (outData) *outData=NULL;
  mutexLock(&L->m);
  if (!L->signaled){
    if (timeoutMillis==0){ mutexUnlock(&L->m); return ALOOPER_POLL_TIMEOUT; }
    if (timeoutMillis<0)  condvarWait(&L->cv,&L->m);
    else condvarWaitTimeout(&L->cv,&L->m,(u64)timeoutMillis*1000000ull);
  }
  int was = L->signaled; L->signaled=0;
  mutexUnlock(&L->m);
  return was ? ALOOPER_POLL_WAKE : ALOOPER_POLL_TIMEOUT;
}
/* Unity rarely uses these two, but provide them for completeness. */
int ALooper_addFd(ALooper *l,int fd,int ident,int events,void *cb,void *data){
  (void)l;(void)fd;(void)ident;(void)events;(void)cb;(void)data; return 1; }
int ALooper_removeFd(ALooper *l,int fd){ (void)l;(void)fd; return 1; }

/* ==========================================================================
 * Sensors -- report none. Unity imports ASensorManager, so these must exist and return a
 * clean empty state rather than be missing symbols.
 * ========================================================================== */
void *ASensorManager_getInstance(void){ static int x; return &x; }
int   ASensorManager_getSensorList(void *m, void **list){ (void)m; if(list)*list=NULL; return 0; }
void *ASensorManager_getDefaultSensor(void *m, int type){ (void)m;(void)type; return NULL; }
void *ASensorManager_createEventQueue(void *m, void *looper, int ident, void *cb, void *data){
  (void)m;(void)looper;(void)ident;(void)cb;(void)data; static int q; return &q; }
int   ASensorManager_destroyEventQueue(void *m, void *q){ (void)m;(void)q; return 0; }

int   ASensorEventQueue_enableSensor (void *q, const void *s){ (void)q;(void)s; return -1; }
int   ASensorEventQueue_disableSensor(void *q, const void *s){ (void)q;(void)s; return 0; }
int   ASensorEventQueue_setEventRate (void *q, const void *s, int32_t us){ (void)q;(void)s;(void)us; return 0; }
int   ASensorEventQueue_getEvents    (void *q, void *ev, size_t n){ (void)q;(void)ev;(void)n; return 0; }
int   ASensorEventQueue_hasEvents    (void *q){ (void)q; return 0; }

const char *ASensor_getName      (const void *s){ (void)s; return ""; }
const char *ASensor_getVendor    (const void *s){ (void)s; return ""; }
int         ASensor_getType      (const void *s){ (void)s; return 0; }
float       ASensor_getResolution(const void *s){ (void)s; return 0.0f; }
int         ASensor_getMinDelay  (const void *s){ (void)s; return 0; }

/* No orientation sensor -> report level (jni_fake.c orientation getters). */
void android_get_orientation(float *x, float *y, float *z){
  if (x) *x = 0.0f;
  if (y) *y = 0.0f;
  if (z) *z = 0.0f;
}

/* ==========================================================================
 * HID polling -> Unity input.
 * Unity ingests input through the Java UnityPlayer (touch -> nativeInjectEvent /
 * key path). The exact native event struct is engine-internal: recover the
 * registered "injectEvent"/"nativePointer*" method from libunity's JNI_OnLoad
 * (PORTING_PLAN.md S4) and fill in feed_one_touch(). Until then this reads HID
 * but doesn't yet hand it to the engine.
 * ========================================================================== */
/* HID -> Unity input. The Switch touchscreen passes straight through (all fingers) into
 * il2cpp UnityEngine.Input via the hooks in unity_input_hook.c.
 *
 * On top of that, the reusable nx_pointer module (nx_pointer.{c,h}) provides the on-screen
 * cursor, USB mouse, gyro pointing and the +/-/stick/A controls, plus its own GL overlay
 * (drawn from the eglSwapBuffers wrapper in imports.c). We feed it a NxpConfig once, pump
 * it each frame, and merge its pointer events into the same multi-touch stream the panel
 * uses -- so a cursor tap is just another finger and rides the same slot allocator. */

static PadState g_pad;                              /* still used for nothing but init parity */
static float g_last_tx = 360, g_last_ty = 640;      /* last touch (game space), reused on release */

void android_native_input_init(void){
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&g_pad);
  hidInitializeTouchScreen();

  /* nx_pointer owns the pad/touch/mouse/gyro from here on. It reads cursor.png off the SD
   * card during init, so this must run before the engine spawns its worker threads. The
   * data_dir is the game folder (RD_GAME_HOME); cursor.png and pointer.cfg live alongside the
   * save. We hand it the port's LOCKED file wrappers -- newlib's handle table is not
   * thread-safe and the engine hammers it, so settings I/O must go through fopen_fake/
   * fclose_fake, not raw fopen. cursor_id sits above any real HID finger_id (0..15). */
  /* The render size is chosen at boot (render_resolution, rd_perf.c) and is
   * already set when this runs; nx_pointer maps panel/cursor coordinates into it. It used to
   * be hardcoded 1920x1080, which put every touch in the wrong place once handheld rendered
   * at 1280x720. nxp_init copies the struct. */
  const NxpConfig cfg = {
    .screen_w = screen_width, .screen_h = screen_height,
    .panel_w  = RD_TOUCH_PANEL_W, .panel_h = RD_TOUCH_PANEL_H,
    .data_dir = RD_GAME_HOME,
    .cursor_id = 1000,                              /* reserved id: cannot clash with a finger */
    .max_touch_slots = NX_MAX_TOUCH,
    .stick_speed = 0.0f,                            /* 0 => module default (14 px/frame)    */
    .mouse_sens  = 0.0f,
    .log = NULL,
    .fopen_fn  = fopen_fake,
    .fclose_fn = fclose_fake,
  };
  nxp_init(&cfg);
}

void android_native_feed_hid(void){
  padUpdate(&g_pad);
  nxp_update();                                     /* reads pad/touch/mouse/gyro, builds events */

  const float PANEL_W = RD_TOUCH_PANEL_W, PANEL_H = RD_TOUCH_PANEL_H;
  const float SW = (screen_width  > 0) ? (float)screen_width  : PANEL_W;
  const float SH = (screen_height > 0) ? (float)screen_height : PANEL_H;

  /* nx_pointer already reports pointer events in render space with a TOP-LEFT origin and the
   * pointer-phase values NXP_DOWN/MOVE/UP -- which are numerically the same as the tracker's
   * (see unity_input_hook.h). Unity wants a BOTTOM-LEFT origin, so we flip Y here and hand the
   * lot to the same multi-touch tracker the panel feeds. Touchscreen fingers, the stick/gyro/
   * mouse cursor and USB-mouse taps therefore all arrive through ONE path and share the slot
   * allocator; nothing has to special-case the cursor. */
  NxpEvent ev[NX_MAX_TOUCH];
  int n = nxp_poll(ev, NX_MAX_TOUCH);

  NxTouchIn tin[NX_MAX_TOUCH];
  int tn = 0;
  for (int i = 0; i < n && tn < NX_MAX_TOUCH; i++) {
    float gx = ev[i].x;
    float gy = SH - ev[i].y;                         /* flip Y: top-left -> bottom-left */
    if (gx < 0.0f) gx = 0.0f;
    if (gx > SW - 1.0f) gx = SW - 1.0f;
    if (gy < 0.0f) gy = 0.0f;
    if (gy > SH - 1.0f) gy = SH - 1.0f;
    tin[tn].id = ev[i].id;                           /* raw id; densified in the hook   */
    tin[tn].x  = gx;
    tin[tn].y  = gy;
    tn++;
  }
  if (tn > 0) { g_last_tx = tin[0].x; g_last_ty = tin[0].y; }

  /* tn == 0 -> no fingers down: update_multi emits the Ended frame for any that just lifted. */
  nx_input_hook_update_multi(tin, tn);
}
