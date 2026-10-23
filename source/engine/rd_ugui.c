/* rd_ugui.c -- see rd_ugui.h. MIT (see LICENSE).
 *
 * icalls take the managed arguments only; managed methods take `this` (if any), the arguments
 * and their MethodInfo last. Value-type fields count the boxed object header
 * (RD_IL2CPP_OBJECT_HEADER). The scratch objects (Vector3[4], PointerEventData) are created
 * once and kept alive with a pinned GC handle. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rd_ugui.h"
#include "rd_il2cpp.h"
#include "util.h"

/* ---- Unity names (UnityEngine.UI, UnityEngine.CoreModule, engine icalls) --------------- */
#define ASM_UI       "UnityEngine.UI"
#define ASM_CORE     "UnityEngine.CoreModule"
#define NS_UI        "UnityEngine.UI"
#define NS_ES        "UnityEngine.EventSystems"
#define NS_UNITY     "UnityEngine"
#define FLD_RESULTS  "m_RaycastResultCache"    /* BaseInputModule: List<RaycastResult> */
#define FLD_GO       "m_GameObject"            /* RaycastResult */
#define FLD_ITEMS    "_items"                  /* List<T> */
#define FLD_SIZE     "_size"
#define FLD_CACHED_PTR "m_CachedPtr"           /* UnityEngine.Object: the native object */
#define EYE_MONO     2                         /* Camera.MonoOrStereoscopicEye.Mono */
#define RENDER_OVERLAY 0                       /* RenderMode.ScreenSpaceOverlay */
#define MAX_DEPTH    12                        /* parents walked from a raycast hit */

typedef struct { float x, y, z; } v3;
typedef struct { float x, y; } v2;

static struct {
  void *(*find_objects)(void *type);
  void *(*get_name)(void *obj);
  void *(*game_object)(void *component);
  void *(*transform)(void *component);
  uint8_t (*active_enabled)(void *behaviour);
  void *(*parent)(void *transform);
  void *(*go_transform)(void *go);
  void *(*root_canvas)(void *canvas);
  int32_t (*render_mode)(void *canvas);
  void *(*world_camera)(void *canvas);
  void (*world_to_screen)(void *camera, const v3 *pos, int32_t eye, v3 *out);
  void *(*in_parent)(void *go, void *type);      /* GameObject.GetComponentInParent(Type) */
} ic;

typedef struct { void *fn; const void *mi; } method;
static struct {
  method interactable, target_graphic, canvas, corners, current, input_module, raycast_all,
         ped_ctor, set_position, s_value, s_set_value, s_min, s_max, s_whole, s_direction;
} m;

static void *g_selectable_type;              /* System.Type of Selectable */
static void *g_canvas_type;                  /* System.Type of Canvas */
static void *g_ped_class;
static uint32_t g_results_off, g_item_go_off;
static uint32_t g_cached_ptr_off;            /* UnityEngine.Object.m_CachedPtr */
static void *g_corners;                      /* Vector3[4] */
static void *g_ped, *g_ped_es;               /* PointerEventData and the EventSystem it is for */
static int g_state;                          /* 0 = not tried, 1 = ready, -1 = off */

#define FIELD(obj, off, type) (*(type *)((char *)(obj) + (off)))
#define CALL(meth, type, ...) ((type)(meth).fn)(__VA_ARGS__, (meth).mi)

/* An engine icall by name; counts it in *missing if absent. */
static void *icall(const char *name, int *missing) {
  void *p = g_il2cpp.resolve_icall(name);
  if (!p) { debugPrintf("[uinav] icall %s not found\n", name); (*missing)++; }
  return p;
}

/* A managed method by class and name; counts it in *missing if absent. */
static method find(void *klass, const char *name, int argc, int *missing) {
  void **mi = klass ? g_il2cpp.class_get_method_from_name(klass, name, argc) : NULL;
  if (!mi || !mi[0]) { debugPrintf("[uinav] method %s not found\n", name); (*missing)++;
                       return (method){ NULL, NULL }; }
  return (method){ mi[0], mi };
}

int rd_ugui_init(void) {
  if (g_state) return g_state;
  const rd_il2cpp_api *api = &g_il2cpp;
  if (!api->class_get_method_from_name || !api->resolve_icall || !api->object_new ||
      !api->object_get_class || !api->array_new || !api->gchandle_new || !api->type_get_object)
    return 0;
  void *selectable = rd_il2cpp_class(api, ASM_UI, NS_UI, "Selectable");
  if (!selectable) return 0;                              /* UnityEngine.UI not loaded yet */
  int missing = 0;
  ic.find_objects    = icall("UnityEngine.Object::FindObjectsOfType", &missing);
  ic.get_name        = icall("UnityEngine.Object::GetName", &missing);
  ic.game_object     = icall("UnityEngine.Component::get_gameObject", &missing);
  ic.transform       = icall("UnityEngine.Component::get_transform", &missing);
  ic.active_enabled  = icall("UnityEngine.Behaviour::get_isActiveAndEnabled", &missing);
  ic.parent          = icall("UnityEngine.Transform::GetParent", &missing);
  ic.go_transform    = icall("UnityEngine.GameObject::get_transform", &missing);
  ic.root_canvas     = icall("UnityEngine.Canvas::get_rootCanvas", &missing);
  ic.render_mode     = icall("UnityEngine.Canvas::get_renderMode", &missing);
  ic.world_camera    = icall("UnityEngine.Canvas::get_worldCamera", &missing);
  ic.world_to_screen = icall("UnityEngine.Camera::WorldToScreenPoint_Injected", &missing);
  ic.in_parent       = icall("UnityEngine.GameObject::GetComponentInParent", &missing);

  void *graphic = rd_il2cpp_class(api, ASM_UI, NS_UI, "Graphic");
  void *rect_tr = rd_il2cpp_class(api, ASM_CORE, NS_UNITY, "RectTransform");
  void *vector3 = rd_il2cpp_class(api, ASM_CORE, NS_UNITY, "Vector3");
  void *es      = rd_il2cpp_class(api, ASM_UI, NS_ES, "EventSystem");
  void *module  = rd_il2cpp_class(api, ASM_UI, NS_ES, "BaseInputModule");
  void *result  = rd_il2cpp_class(api, ASM_UI, NS_ES, "RaycastResult");
  g_ped_class   = rd_il2cpp_class(api, ASM_UI, NS_ES, "PointerEventData");
  m.interactable   = find(selectable, "IsInteractable", 0, &missing);
  m.target_graphic = find(selectable, "get_targetGraphic", 0, &missing);
  m.canvas         = find(graphic, "get_canvas", 0, &missing);
  m.corners        = find(rect_tr, "GetWorldCorners", 1, &missing);
  m.current        = find(es, "get_current", 0, &missing);
  m.input_module   = find(es, "get_currentInputModule", 0, &missing);
  m.raycast_all    = find(es, "RaycastAll", 2, &missing);
  m.ped_ctor       = find(g_ped_class, ".ctor", 1, &missing);
  m.set_position   = find(g_ped_class, "set_position", 1, &missing);
  void *slider  = rd_il2cpp_class(api, ASM_UI, NS_UI, "Slider");
  m.s_value        = find(slider, "get_value", 0, &missing);
  m.s_set_value    = find(slider, "set_value", 1, &missing);
  m.s_min          = find(slider, "get_minValue", 0, &missing);
  m.s_max          = find(slider, "get_maxValue", 0, &missing);
  m.s_whole        = find(slider, "get_wholeNumbers", 0, &missing);
  m.s_direction    = find(slider, "get_direction", 0, &missing);
  uint32_t go_off = 0;
  if (!module || !rd_il2cpp_field_offset(api, module, FLD_RESULTS, &g_results_off) ||
      !result || !rd_il2cpp_field_offset(api, result, FLD_GO, &go_off) ||
      go_off < RD_IL2CPP_OBJECT_HEADER) missing++;
  g_item_go_off = go_off - RD_IL2CPP_OBJECT_HEADER;
  void *canvas_cls = rd_il2cpp_class(api, "UnityEngine.UIModule", NS_UNITY, "Canvas");
  if (api->class_get_type) {
    g_selectable_type = api->type_get_object(api->class_get_type(selectable));
    if (canvas_cls) g_canvas_type = api->type_get_object(api->class_get_type(canvas_cls));
  }
  if (vector3) g_corners = api->array_new(vector3, 4);
  if (g_corners) api->gchandle_new(g_corners, 1);
  void *object = rd_il2cpp_class(api, ASM_CORE, NS_UNITY, "Object");
  if (!object || !rd_il2cpp_field_offset(api, object, FLD_CACHED_PTR, &g_cached_ptr_off)) missing++;
  if (!g_selectable_type || !g_canvas_type || !g_corners || !api->array_length || !api->class_get_name) missing++;
  g_state = missing ? -1 : 1;
  debugPrintf("[uinav] uGUI %s\n", missing ? "lookups missing -- menu navigation OFF" : "ready");
  return g_state;
}

/* The GameObject's name as ASCII into buf ("" if there is none). */
static void copy_name(void *obj, char *buf) {
  buf[0] = '\0';
  void *s = obj ? ic.get_name(obj) : NULL;
  if (!s || !g_il2cpp.string_length || !g_il2cpp.string_chars) return;
  int32_t n = g_il2cpp.string_length(s);
  const uint16_t *c = g_il2cpp.string_chars(s);
  if (n > RD_UGUI_NAME - 1) n = RD_UGUI_NAME - 1;
  for (int32_t i = 0; i < n; i++) buf[i] = c[i] < 0x80 ? (char)c[i] : '?';
  buf[n] = '\0';
}

/* Screen rectangle (top-left origin) of a Selectable; 0 if its canvas is unknown. The canvas
 * comes from the target graphic, else from the nearest Canvas up the hierarchy. */
static int screen_rect(void *sel, int screen_h, rd_uinav_rect *out) {
  void *graphic = CALL(m.target_graphic, void *(*)(void *, const void *), sel);
  void *canvas = graphic ? CALL(m.canvas, void *(*)(void *, const void *), graphic) : NULL;
  if (!canvas) canvas = ic.in_parent(ic.game_object(sel), g_canvas_type);
  void *root = canvas ? ic.root_canvas(canvas) : NULL;
  if (!root) return 0;
  void *camera = ic.render_mode(root) == RENDER_OVERLAY ? NULL : ic.world_camera(root);
  if (ic.render_mode(root) != RENDER_OVERLAY && !camera) return 0;
  CALL(m.corners, void (*)(void *, void *, const void *), ic.transform(sel), g_corners);
  const v3 *w = (const v3 *)((char *)g_corners + RD_IL2CPP_ARRAY_DATA);
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (int i = 0; i < 4; i++) {
    v3 p = w[i];
    if (camera) ic.world_to_screen(camera, &w[i], EYE_MONO, &p);
    if (p.x < x0) x0 = p.x;
    if (p.x > x1) x1 = p.x;
    if (p.y < y0) y0 = p.y;
    if (p.y > y1) y1 = p.y;
  }
  *out = (rd_uinav_rect){ x0, (float)screen_h - y1, x1 - x0, y1 - y0 };
  return 1;
}

/* The scratch PointerEventData for the current EventSystem, or NULL. */
static void *pointer_data(void *es) {
  if (g_ped && g_ped_es == es) return g_ped;
  void *ped = g_il2cpp.object_new(g_ped_class);
  if (!ped) return NULL;
  CALL(m.ped_ctor, void (*)(void *, void *, const void *), ped, es);
  g_il2cpp.gchandle_new(ped, 1);     /* the previous one is left to its handle: one per scene */
  g_ped = ped;
  g_ped_es = es;
  return ped;
}

/* 1 if `go` is `target` or one of its descendants. */
static int is_under(void *go, void *target) {
  void *tr = go ? ic.go_transform(go) : NULL;
  for (int i = 0; tr && i < MAX_DEPTH; i++) {
    if (ic.game_object(tr) == target) return 1;
    tr = ic.parent(tr);
  }
  return 0;
}

/* The topmost GameObject a tap at the screen point (bottom-left origin) would hit, or NULL. */
static void *hit_at(void *es, void *results, float x, float y) {
  void *ped = pointer_data(es);
  if (!ped) return NULL;
  CALL(m.set_position, void (*)(void *, v2, const void *), ped, ((v2){ x, y }));
  CALL(m.raycast_all, void (*)(void *, void *, void *, const void *), es, ped, results);
  static uint32_t items_off, size_off;              /* List<RaycastResult>, looked up once */
  if (!items_off) {
    void *klass = g_il2cpp.object_get_class(results);
    if (!rd_il2cpp_field_offset(&g_il2cpp, klass, FLD_ITEMS, &items_off) ||
        !rd_il2cpp_field_offset(&g_il2cpp, klass, FLD_SIZE, &size_off)) { items_off = 0; return NULL; }
  }
  if (FIELD(results, size_off, int32_t) <= 0) return NULL;
  void *items = FIELD(results, items_off, void *);
  return items ? FIELD(items, RD_IL2CPP_ARRAY_DATA + g_item_go_off, void *) : NULL;
}

/* Tells the reject callback (if any) why Selectable `sel` is left out. */
static void reject_one(rd_ugui_reject_fn reject, void *sel, const char *why, void *hit) {
  if (!reject) return;
  char name[RD_UGUI_NAME], hit_name[RD_UGUI_NAME];
  copy_name(ic.game_object(sel), name);
  copy_name(hit, hit_name);
  reject(name, why, hit_name);
}

/* Finds where a tap reaches the target's GameObject: the first probe point whose topmost
 * raycast hit is it or a child. 1 and t->tap_* if found; else 0 and *hit = what the centre hit. */
static int find_tap(void *es, void *results, void *go, int screen_h, rd_ugui_target *t,
                    void **hit) {
  *hit = NULL;
  for (int p = 0; p < RD_UINAV_PROBES; p++) {
    float x, y;
    rd_uinav_probe(&t->t.r, p, &x, &y);
    void *h = hit_at(es, results, x, (float)screen_h - y);
    if (is_under(h, go)) { t->tap_x = x; t->tap_y = y; return 1; }
    if (!p) *hit = h;
  }
  return 0;
}

int rd_ugui_scan(rd_ugui_target *out, int max, int screen_w, int screen_h,
                 rd_ugui_reject_fn reject) {
  if (g_state <= 0) return 0;
  void *arr = ic.find_objects(g_selectable_type);
  const uint32_t n = arr ? g_il2cpp.array_length(arr) : 0;
  void *es = ((void *(*)(const void *))m.current.fn)(m.current.mi);   /* static */
  void *module = es ? CALL(m.input_module, void *(*)(void *, const void *), es) : NULL;
  void *results = module ? FIELD(module, g_results_off, void *) : NULL;
  const float sw = (float)screen_w, sh = (float)screen_h;
  int k = 0;
  for (uint32_t i = 0; i < n && k < max; i++) {
    void *sel = rd_il2cpp_array_at(arr, i);
    if (!sel) continue;
    if (!ic.active_enabled(sel)) { reject_one(reject, sel, "inactive", NULL); continue; }
    if (!CALL(m.interactable, uint8_t (*)(void *, const void *), sel)) {
      reject_one(reject, sel, "not interactable", NULL);
      continue;
    }
    rd_ugui_target *t = &out[k];
    if (!screen_rect(sel, screen_h, &t->t.r)) { reject_one(reject, sel, "no canvas", NULL); continue; }
    if (!rd_uinav_on_screen(&t->t.r, sw, sh)) { reject_one(reject, sel, "off screen", NULL); continue; }
    if (rd_uinav_screen_share(&t->t.r, sw, sh) > RD_UGUI_MAX_SHARE) {
      reject_one(reject, sel, "too big", NULL);
      continue;
    }
    void *go = ic.game_object(sel);
    rd_uinav_center(&t->t.r, &t->tap_x, &t->tap_y);
    void *hit = NULL;
    if (results && !find_tap(es, results, go, screen_h, t, &hit)) {   /* a dialog on top? */
      reject_one(reject, sel, "covered by", hit);
      continue;
    }
    t->t.id = (uintptr_t)sel;
    const char *cls = g_il2cpp.class_get_name(g_il2cpp.object_get_class(sel));
    snprintf(t->cls, sizeof t->cls, "%s", cls ? cls : "");
    t->kind = !strcmp(t->cls, "Slider") ? RD_UGUI_SLIDER : RD_UGUI_OTHER;
    copy_name(go, t->name);
    void *ptr = ic.parent(ic.transform(sel));
    copy_name(ptr ? ic.game_object(ptr) : NULL, t->parent);
    k++;
  }
  return k;
}

void rd_ugui_slider_get(uintptr_t id, rd_ugui_slider *out) {
  void *s = (void *)id;
  out->value = CALL(m.s_value, float (*)(void *, const void *), s);
  out->min = CALL(m.s_min, float (*)(void *, const void *), s);
  out->max = CALL(m.s_max, float (*)(void *, const void *), s);
  out->whole = CALL(m.s_whole, uint8_t (*)(void *, const void *), s);
  out->direction = CALL(m.s_direction, int32_t (*)(void *, const void *), s);
}

void rd_ugui_slider_set(uintptr_t id, float v) {
  CALL(m.s_set_value, void (*)(void *, float, const void *), (void *)id, v);
}

int rd_ugui_alive(const void *obj) {
  return obj && g_cached_ptr_off && FIELD(obj, g_cached_ptr_off, void *) != NULL;
}
