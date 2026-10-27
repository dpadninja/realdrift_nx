/* unity_jni.c -- real handlers for the Android classes listed in unity_jni.h.
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "unity_jni.h"
#include "rd_log.h"
#include "util.h"
#include "config.h"   /* RD_GAME_PACKAGE etc. */
#include "rd_apkpath.h"
#include "rd_prefs.h"
#include "rd_zip.h"
#include "rd_perf.h"   /* RD_HANDHELD_* */
#include "rd_platform.h"

/* FakeID layout we read (must match jni_fake.c). Only the char[] fields. */
struct FakeID { uint32_t tag; char cls[96]; char name[64]; char sig[160]; };


static char g_root[256];            /* the game folder, RD_GAME_HOME                  */

static int  has(const char *s, const char *sub){ return strstr(s,sub)!=NULL; }

/* --------------------------------------------------------------------------
 * stateful handle objects (streams / fds / prefs) -- jni_fake's objects are
 * label-only, so we carry our own. Opaque to the engine; recovered via recv.
 * -------------------------------------------------------------------------- */
enum { UJ_TAG = 0x554a4831 /*'UJH1'*/ };
enum { UJ_INPUTSTREAM, UJ_AFD, UJ_FD, UJ_PREFS, UJ_EDITOR, UJ_GENERIC,
       UJ_MAP, UJ_SET, UJ_ITER, UJ_ENTRY, UJ_BOXED };

typedef struct {
  uint32_t tag; int kind;
  FILE *fp;                 /* InputStream                                  */
  void *mem;                /* InputStream backing buffer (asset from base.apk) */
  int   fd;                 /* AFD / FD                                     */
  long  off, len;           /* AFD region                                  */
  int   idx;                /* UJ_ITER cursor / UJ_ENTRY kv index           */
  char  btype;              /* UJ_BOXED: 'I' int 'L' long 'B' bool 'F' float*/
  long long bival;          /* UJ_BOXED int/long/bool payload               */
  double bfval;             /* UJ_BOXED float payload                       */
} UHandle;

/* A new handle of the given kind (asset stream, file, ...), tagged so unity_jni can recognise
 * its own objects among JNI handles. */
static UHandle *uh_new(int kind){
  UHandle *h = calloc(1,sizeof *h);
  h->tag = UJ_TAG; h->kind = kind; h->fd = -1; return h;
}
static int is_uh(void *p,int kind){ UHandle*h=p; return h && h->tag==UJ_TAG && h->kind==kind; }

/* "assets/<name>" inside base.apk for AssetManager.open("bin/Data/x"). */
static void apk_asset_entry(char *out,size_t n,const char *name){
  while (name && name[0]=='/') name++;
  snprintf(out,n,"assets/%s",name?name:"");
}

/* Path handed to *managed* code (persistentDataPath / dataPath via the Context
 * getters below). It must be Unix-rooted ("/switch/realdrift_nx"): Mono/IL2CPP on
 * Android uses Unix path rules where ":" is NOT a root marker, so a "sdmc:/..."
 * path is treated as *relative* and Path.Combine() concatenates it after a
 * relative asset path -> "assets/bin/Data/sdmc:/switch/realdrift_nx". newlib then
 * sees the embedded "sdmc:" mid-path, mis-parses the device, and null-derefs
 * (Data Abort at the devoptab mkdir_r slot, +0x68). Stripping the device prefix
 * fixes this; newlib still resolves the device-less absolute path via the
 * default device (sdmc), set by our boot chdir, so file I/O is unaffected. Our
 * internal g_root keeps the explicit "sdmc:" prefix. */
static int rd_file_exists(const char *p){ struct stat st; return stat(p,&st)==0; }
/* The game folder without its "sdmc:" device, as managed code must see paths (see managed_path
 * in libc_shim.h). */
static const char *managed_root(void){
  const char *c = strchr(g_root, ':');
  return (c && c[1] == '/') ? c + 1 : g_root;
}

/* ==========================================================================
 * SharedPreferences  (Unity PlayerPrefs == save data)  -> g_root/RD_PREFS_FILE
 * flat "type\tkey\tvalue" lines; value url-ish escaped on tab/newline.
 * ========================================================================== */
typedef struct { char type; char *key; char *val; } KV;
static KV   *g_kv = NULL; static int g_kv_n=0, g_kv_cap=0; static int g_kv_dirty=0;

static char *prefs_file(char *buf,size_t n){ snprintf(buf,n,"%s/" RD_PREFS_FILE,g_root); return buf; }

/* Sets a SharedPreferences entry (type letter, key, value), replacing an existing key; marks
 * the store dirty. */
static void kv_set(char type,const char*key,const char*val){
  for (int i=0;i<g_kv_n;i++) if(!strcmp(g_kv[i].key,key)){
    g_kv[i].type=type; free(g_kv[i].val); g_kv[i].val=strdup(val); g_kv_dirty=1; return; }
  if (g_kv_n==g_kv_cap){ g_kv_cap=g_kv_cap?g_kv_cap*2:32; g_kv=realloc(g_kv,g_kv_cap*sizeof(KV)); }
  g_kv[g_kv_n].type=type; g_kv[g_kv_n].key=strdup(key); g_kv[g_kv_n].val=strdup(val);
  g_kv_n++; g_kv_dirty=1;
}
static KV *kv_get(const char*key){ for(int i=0;i<g_kv_n;i++) if(!strcmp(g_kv[i].key,key)) return &g_kv[i]; return NULL; }
/* Removes a SharedPreferences entry if present; marks the store dirty. */
static void kv_remove(const char*key){
  for(int i=0;i<g_kv_n;i++) if(!strcmp(g_kv[i].key,key)){
    free(g_kv[i].key);free(g_kv[i].val); g_kv[i]=g_kv[--g_kv_n]; g_kv_dirty=1; return; }
}
static void kv_clear(void){ for(int i=0;i<g_kv_n;i++){free(g_kv[i].key);free(g_kv[i].val);} g_kv_n=0; g_kv_dirty=1; }

/* File format and atomic save: rd_prefs.c. The game keeps its whole progress in ONE long
 * base64 PlayerPrefs string (TrollpantsDevString), so lines must not be length-limited. */
static void prefs_load(void){
  char p[RD_PATH_MAX], tmp[RD_PATH_MAX + 8];
  prefs_file(p,sizeof p); snprintf(tmp,sizeof tmp,"%s.tmp",p);
  const char *src = rd_prefs_pick(p, tmp);
  FILE *f = src ? fopen(src,"rb") : NULL;
  if(!f){ debugPrintf("[prefs] load: no save file at %s (errno=%d)\n", p, errno); return; }
  char *line=NULL, *k, *v, type; size_t cap=0;
  while (rd_prefs_read_entry(f,&line,&cap,&type,&k,&v)) kv_set(type,k,v);
  free(line); fclose(f); g_kv_dirty=0;
  debugPrintf("[prefs] load: %d entries from %s\n", g_kv_n, src);
}
/* Writes the SharedPreferences store to prefs.kv through the .tmp file and an atomic replace
 * (rd_prefs.h), if anything changed; logs the outcome. */
static void prefs_flush(void){
  if(!g_kv_dirty){ debugPrintf("[prefs] flush: nothing dirty (%d entries)\n", g_kv_n); return; }
  char p[RD_PATH_MAX], tmp[RD_PATH_MAX + 8];
  prefs_file(p,sizeof p); snprintf(tmp,sizeof tmp,"%s.tmp",p);
  FILE*f=fopen(tmp,"wb");
  if(!f){ debugPrintf("[prefs] flush FAILED: fopen(%s,wb) errno=%d\n", tmp, errno); return; }
  for(int i=0;i<g_kv_n;i++) rd_prefs_write_entry(f,g_kv[i].type,g_kv[i].key,g_kv[i].val);
  int ok = fclose(f)==0 && rd_prefs_commit(tmp,p)==0;
  if(!ok){ debugPrintf("[prefs] flush FAILED: commit %s -> %s errno=%d\n", tmp, p, errno); return; }
  g_kv_dirty=0;
  debugPrintf("[prefs] flush: wrote %d entries to %s\n", g_kv_n, p);
}

/* PlayerPrefs: Unity reaches them through the Java SharedPreferences API (getString/putInt/...
 * on the fake objects in this file); this store keeps them in RD_PREFS_FILE on the SD card. */

/* ==========================================================================
 * getAll() boxed values: turn a stored KV into the Java object Unity expects.
 * Strings come back as a native FakeString (Unity reads them via
 * GetStringUTFChars); primitives come back as our UJ_BOXED handle, which
 * jni_fake.c recognises by receiver for IsInstanceOf + intValue/longValue/
 * booleanValue/floatValue. Type char matches kv_set(): S/I/L/B/F.
 * ========================================================================== */
static void *uh_box_from_kv(const KV *kv){
  if (!kv) return jni_make_string("");
  switch (kv->type){
    case 'I': case 'L': case 'B': {
      UHandle *h = uh_new(UJ_BOXED);
      h->btype = kv->type;
      h->bival = strtoll(kv->val, NULL, 10);
      if (kv->type=='B') h->bival = (kv->val[0]=='1'||kv->val[0]=='t'||kv->val[0]=='T') ? 1 : 0;
      return h;
    }
    case 'F': {
      UHandle *h = uh_new(UJ_BOXED);
      h->btype = 'F'; h->bfval = strtod(kv->val, NULL);
      return h;
    }
    default: /* 'S' and anything else -> string */
      return jni_make_string(kv->val);
  }
}

/* Receiver-keyed accessors used by jni_fake.c (see unity_jni.h). */
int unity_is_boxed(void *p){
  UHandle *h = p; return (h && h->tag==UJ_TAG && h->kind==UJ_BOXED) ? 1 : 0;
}
uint64_t unity_boxed_int(void *p){
  UHandle *h = p; if (!unity_is_boxed(h)) return 0;
  if (h->btype=='F') return (uint64_t)(long long)h->bfval;
  return (uint64_t)h->bival;
}
float unity_boxed_float(void *p){
  UHandle *h = p; if (!unity_is_boxed(h)) return 0.0f;
  return (h->btype=='F') ? (float)h->bfval : (float)h->bival;
}
/* 1/0 if obj is one of our boxed primitives and matches/!matches clazz; -1 if
 * obj is not ours (jni_fake.c then applies its own rules). */
int unity_isinstance(void *p, const char *clazz){
  UHandle *h = p; if (!h || h->tag!=UJ_TAG || h->kind!=UJ_BOXED) return -1;
  if (!clazz) return 0;
  switch (h->btype){
    case 'I': return strstr(clazz,"Integer") ? 1 : 0;
    case 'L': return strstr(clazz,"Long")    ? 1 : 0;
    case 'F': return strstr(clazz,"Float")   ? 1 : 0;
    case 'B': return strstr(clazz,"Boolean") ? 1 : 0;
  }
  return 0;
}

/* ==========================================================================
 * class ownership + dispatch
 * ========================================================================== */
int unity_owns_class(const char *cls){
  return has(cls,"AssetManager") || has(cls,"java/io/InputStream") ||
         has(cls,"AssetFileDescriptor") || has(cls,"java/io/FileDescriptor") ||
         has(cls,"SharedPreferences") || has(cls,"SharedPreferences$Editor") ||
         has(cls,"java/util/Map") || has(cls,"java/util/Set") ||
         has(cls,"java/util/Iterator") || has(cls,"java/util/HashMap") ||
         has(cls,"view/Display") || has(cls,"DisplayManager") ||
         has(cls,"res/Configuration") || has(cls,"res/Resources") ||
         has(cls,"DisplayMetrics") || has(cls,"content/Context") ||
         has(cls,"unity3d/player/UnityPlayer");
}

/* ---- object-returning calls --------------------------------------------- */
void *unity_dispatch_object(void *recv, const void *id_, va_list va){ const struct FakeID *id = id_;
  const char *cls=id->cls, *m=id->name;

  /* AssetManager.open(name) -> InputStream ; openFd(name) -> AssetFileDescriptor */
  if (has(cls,"AssetManager")){
    /* Assets come straight from base.apk (rd_zip.c), like on Android --
     * no extracted assets/ folder on the SD card. */
    if (has(m,"openFd") || has(m,"openNonAssetFd")){
      const char *name = jni_string_utf(va_arg(va,void*));
      char apk[RD_PATH_MAX], entry[RD_PATH_MAX]; rd_zip_entry e;
      snprintf(apk,sizeof apk,"%s/%s",g_root,RD_BASE_APK_NAME);
      apk_asset_entry(entry,sizeof entry,name);
      FILE *zf = fopen(apk,"rb");
      int found = zf && rd_zip_find(zf,entry,&e)==0;
      if (zf) fclose(zf);
      /* Android can only hand out an fd for STORED entries (compressed -> exception). */
      if (!found || e.method != 0) {
        debugPrintf("[io] JNI AssetManager.openFd(%s) -> %s\n", name, found ? "compressed (no fd)" : "MISSING");
        return NULL;
      }
      int fd = open(apk,O_RDONLY);
      debugPrintf("[io] JNI AssetManager.openFd(%s) -> base.apk+0x%llx len %llu\n", name,
                  (unsigned long long)e.data_offset, (unsigned long long)e.size);
      if(fd<0) return NULL;
      UHandle*h=uh_new(UJ_AFD); h->fd=fd; h->off=(long)e.data_offset; h->len=(long)e.size; return h;
    }
    if (has(m,"open")){
      const char *name = jni_string_utf(va_arg(va,void*));
      char apk[RD_PATH_MAX], entry[RD_PATH_MAX]; rd_zip_entry e;
      snprintf(apk,sizeof apk,"%s/%s",g_root,RD_BASE_APK_NAME);
      apk_asset_entry(entry,sizeof entry,name);
      FILE *zf = fopen(apk,"rb");
      void *buf = (zf && rd_zip_find(zf,entry,&e)==0) ? rd_zip_read(zf,&e) : NULL;
      if (zf) fclose(zf);
      FILE *fp = buf ? fmemopen(buf, e.size ? e.size : 1, "rb") : NULL;
      debugPrintf("[io] JNI AssetManager.open(%s) -> %s\n", name,
                  fp ? "ok (base.apk)" : "MISSING");
      if(!fp){ free(buf); return NULL; }
      UHandle*h=uh_new(UJ_INPUTSTREAM); h->fp=fp; h->mem=buf; return h;
    }
    if (has(m,"list")) return jni_make_object("String[]"); /* CHECK: empty array */
    return jni_make_object("AssetManager");
  }

  /* AssetFileDescriptor.getFileDescriptor() -> FileDescriptor (carries the fd) */
  if (has(cls,"AssetFileDescriptor")){
    if (has(m,"getFileDescriptor") || has(m,"getParcelFileDescriptor")){
      UHandle*a=recv; UHandle*fd=uh_new(UJ_FD); fd->fd = is_uh(a,UJ_AFD)?a->fd:-1; return fd;
    }
    return jni_make_object("AssetFileDescriptor");
  }

  /* SharedPreferences.edit() -> Editor ; getString -> String ; getAll -> Map */
  if (has(cls,"SharedPreferences") && !has(cls,"Editor")){
    if (has(m,"edit")) return uh_new(UJ_EDITOR);
    if (has(m,"getString")){
      /* Honour the default argument (rd_prefs.h). */
      const char *key = jni_string_utf(va_arg(va,void*));
      void *defobj = va_arg(va,void*);
      KV*kv=kv_get(key);
      const char *v = rd_pref_string(kv ? kv->val : NULL, defobj ? jni_string_utf(defobj) : NULL);
      debugPrintf("[prefs] getString '%s' -> %s%s\n", key, v ? v : "null", kv ? "" : " (default)");
      return v ? jni_make_string(v) : NULL;
    }
    if (has(m,"getAll")){                 /* Unity PlayerPrefs LOAD entry point */
      debugPrintf("[prefs] getAll() -> Map of %d entries\n", g_kv_n);
      return uh_new(UJ_MAP);
    }
    if (has(m,"getStringSet")) return jni_make_object("Set");
    return jni_make_object("SharedPreferences");
  }

  /* ---- getAll() Map iteration: Map.entrySet/keySet -> Set -> Iterator -> ----
   * Entry{getKey,getValue}. The whole chain just walks the live g_kv list; the
   * Iterator carries a cursor, each Entry captures one index. Map.get(key) is
   * also handled in case Unity takes the keySet()+get() path on some build. */
  if (has(cls,"java/util/Map") && !has(cls,"Entry")){
    if (has(m,"entrySet") || has(m,"keySet")) return uh_new(UJ_SET);
    if (has(m,"get")){ const char*k=jni_string_utf(va_arg(va,void*)); return uh_box_from_kv(kv_get(k)); }
    return uh_new(UJ_MAP);
  }
  if (has(cls,"java/util/Set")){
    if (has(m,"iterator")){ UHandle*it=uh_new(UJ_ITER); it->idx=0; return it; }
    return uh_new(UJ_SET);
  }
  if (has(cls,"java/util/Iterator")){
    if (has(m,"next")){                   /* return current entry, advance cursor */
      UHandle*it=recv;
      int i = is_uh(it,UJ_ITER) ? it->idx : 0;
      if (is_uh(it,UJ_ITER)) it->idx++;
      UHandle*e=uh_new(UJ_ENTRY); e->idx=i; return e;
    }
    return jni_make_object("java/util/Iterator");
  }
  if (has(cls,"java/util/Map") && has(cls,"Entry")){   /* java/util/Map$Entry */
    UHandle*e=recv; int i = is_uh(e,UJ_ENTRY) ? e->idx : -1;
    if (i<0 || i>=g_kv_n) return jni_make_string("");
    if (has(m,"getKey"))   return jni_make_string(g_kv[i].key);
    if (has(m,"getValue")) return uh_box_from_kv(&g_kv[i]);
    return jni_make_string("");
  }
  if (has(cls,"SharedPreferences$Editor")){
    /* putX all return the Editor (chained: editor.putInt(k,v).apply()), so the
     * engine reaches them via CallObjectMethod -> HERE, not the int path. All
     * four primitive puts MUST kv_set here or the write is silently dropped
     * (this was the save bug: int/bool prefs, incl. Unity's storage-version
     * marker, never persisted -> "Upgrading PlayerPrefs storage" every launch).
     * An EMPTY key means the key-encoding path (String([B)/Uri.encode) produced
     * nothing; storing under "" makes every such pref collide into one slot and
     * corrupts Screenmanager resolution -> bad res -> crash. Skip those. */
    if (has(m,"putString")){ const char*k=jni_string_utf(va_arg(va,void*));
      const char*v=jni_string_utf(va_arg(va,void*));
      if(!k[0]){ debugPrintf("[prefs] putString SKIP empty key\n"); return recv; }
      kv_set('S',k,v); debugPrintf("[prefs] putString '%s'\n", k); return recv; }
    if (has(m,"putInt")){ const char*k=jni_string_utf(va_arg(va,void*));
      int v=va_arg(va,int); char b[32]; snprintf(b,sizeof b,"%d",v);
      if(!k[0]){ debugPrintf("[prefs] putInt SKIP empty key (=%d)\n",v); return recv; }
      kv_set('I',k,b); debugPrintf("[prefs] putInt '%s'=%d\n",k,v); return recv; }
    if (has(m,"putLong")){ const char*k=jni_string_utf(va_arg(va,void*));
      long long v=va_arg(va,long long); char b[32]; snprintf(b,sizeof b,"%lld",v);
      if(!k[0]){ debugPrintf("[prefs] putLong SKIP empty key\n"); return recv; }
      kv_set('L',k,b); debugPrintf("[prefs] putLong '%s'=%lld\n",k,v); return recv; }
    if (has(m,"putFloat")){ const char*k=jni_string_utf(va_arg(va,void*));
      double v=va_arg(va,double); char b[32]; snprintf(b,sizeof b,"%.9g",v);
      if(!k[0]){ debugPrintf("[prefs] putFloat SKIP empty key\n"); return recv; }
      kv_set('F',k,b); debugPrintf("[prefs] putFloat '%s'=%g\n",k,v); return recv; }
    if (has(m,"putBoolean")){ const char*k=jni_string_utf(va_arg(va,void*));
      int v=va_arg(va,int);
      if(!k[0]){ debugPrintf("[prefs] putBoolean SKIP empty key (=%d)\n",v); return recv; }
      kv_set('B',k,v?"1":"0"); debugPrintf("[prefs] putBoolean '%s'=%d\n",k,v); return recv; }
    if (has(m,"remove")){ const char*k=jni_string_utf(va_arg(va,void*)); kv_remove(k); return recv; }
    if (has(m,"clear")){ kv_clear(); return recv; }
    return recv;
  }

  /* Display / DisplayManager / Resources / Context: object getters */
  if (has(cls,"DisplayManager") && has(m,"getDisplay")) return jni_make_object("Display");
  if (has(cls,"res/Resources")){
    if (has(m,"getConfiguration")) return jni_make_object("Configuration");
    if (has(m,"getDisplayMetrics")) return jni_make_object("DisplayMetrics");
    return jni_make_object("Resources");
  }
  if (has(cls,"content/Context")){
    /* path getters -> our staged dir, so engine fopen()s land on the SD card */
    if (has(m,"getFilesDir")||has(m,"getCacheDir")||has(m,"getDataDir")||has(m,"getExternalFilesDir"))
      return jni_make_object("File");                 /* File.getAbsolutePath -> g_root below */
    if (has(m,"getPackageName")) return jni_make_string(RD_GAME_PACKAGE);
    if (has(m,"getPackageCodePath")||has(m,"getPackageResourcePath")) {
      /* Report <root>/base.apk when staged (rd_apkpath.h). */
      static char pcp[RD_PATH_MAX];
      const char *r = rd_package_code_path(managed_root(), rd_file_exists, pcp, sizeof pcp);
      debugPrintf("[android] %s -> %s\n", m, r ? r : managed_root());
      return jni_make_string(r ? r : managed_root());
    }
    if (has(m,"getAssets")) return jni_make_object("AssetManager");
    if (has(m,"getResources")) return jni_make_object("Resources");
    if (has(m,"getSystemService")) return jni_make_object("Service");
    return jni_make_object("Context");
  }
  if (has(cls,"java/io/File") && (has(m,"getAbsolutePath")||has(m,"getPath")||has(m,"toString")))
    return jni_make_string(managed_root());

  /* UnityPlayer host queries that return objects -> benign */
  if (has(cls,"UnityPlayer")) return jni_make_object("UnityPlayer");

  return jni_make_object(cls); /* default: opaque handle, never NULL */
}

/* SharedPreferences.getFloat(key, def), called from jni_fake.c dispatch_float. */
int unity_prefs_get_float(const void *id_, va_list va, float *out){ const struct FakeID *id = id_;
  if (!has(id->cls,"SharedPreferences") || has(id->cls,"Editor") || !has(id->name,"getFloat")) return 0;
  const char *k = jni_string_utf(va_arg(va,void*));
  float def = (float)va_arg(va,double);
  KV *kv = kv_get(k);
  *out = rd_pref_float(kv ? kv->val : NULL, def);
  debugPrintf("[prefs] getFloat '%s' -> %g%s\n", k, *out, kv ? "" : " (default)");
  return 1;
}

/* ---- int / boolean / long calls ----------------------------------------- */
uint64_t unity_dispatch_int(void *recv, const void *id_, va_list va){ const struct FakeID *id = id_;
  const char *cls=id->cls, *m=id->name;

  /* getAll() iteration: Iterator.hasNext + a few Map predicates. (Integer/Long/
   * Boolean unboxing is routed by RECEIVER in jni_fake.c, not here.) */
  if (has(cls,"java/util/Iterator") && has(m,"hasNext")){
    UHandle*it=recv; return (uint64_t)((is_uh(it,UJ_ITER) && it->idx < g_kv_n) ? 1 : 0);
  }
  if (has(cls,"java/util/Map") && !has(cls,"Entry")){
    if (has(m,"size"))    return (uint64_t)g_kv_n;
    if (has(m,"isEmpty")) return (uint64_t)(g_kv_n==0);
    if (has(m,"containsKey")){ const char*k=jni_string_utf(va_arg(va,void*)); return (uint64_t)(kv_get(k)?1:0); }
  }

  /* InputStream.read() / read([B) / read([B,off,len) / available / skip */
  if (has(cls,"java/io/InputStream")){
    UHandle*h=recv; if(!is_uh(h,UJ_INPUTSTREAM)||!h->fp) return (uint64_t)-1;
    if (has(m,"available")){ long cur=ftell(h->fp); fseek(h->fp,0,SEEK_END);
      long end=ftell(h->fp); fseek(h->fp,cur,SEEK_SET); return (uint64_t)(end-cur); }
    if (has(m,"skip")){ long nskip=(long)va_arg(va,long long); fseek(h->fp,nskip,SEEK_CUR); return (uint64_t)nskip; }
    if (has(m,"close")){ fclose(h->fp); h->fp=NULL; free(h->mem); h->mem=NULL; return 0; }
    if (has(m,"read")){
      if (strstr(id->sig,"([B")){                     /* read(byte[][,off,len]) */
        void *arr = va_arg(va,void*);
        int alen=0; char *buf = jni_bytearray_data(arr,&alen);
        int off=0, len=alen;
        if (strstr(id->sig,"([BII)")){ off=va_arg(va,int); len=va_arg(va,int); }
        size_t got=fread(buf+off,1,(size_t)len,h->fp);
        return got? (uint64_t)got : (uint64_t)-1;     /* -1 == EOF, per InputStream */
      }
      int c=fgetc(h->fp); return (uint64_t)(c==EOF? -1 : c); /* read() one byte */
    }
    return 0;
  }

  /* SharedPreferences getters (Int/Long/Boolean + contains) */
  if (has(cls,"SharedPreferences") && !has(cls,"Editor")){
    if (has(m,"contains")){ const char*k=jni_string_utf(va_arg(va,void*)); return kv_get(k)?1:0; }
    if (has(m,"getInt")||has(m,"getLong")){ const char*k=jni_string_utf(va_arg(va,void*));
      long long def=(long long)va_arg(va,long long); KV*kv=kv_get(k);
      return (uint64_t)(kv? strtoll(kv->val,NULL,10) : def); }
    if (has(m,"getBoolean")){ const char*k=jni_string_utf(va_arg(va,void*));
      int def=va_arg(va,int); KV*kv=kv_get(k); return (uint64_t)(kv? (kv->val[0]=='1'||kv->val[0]=='t') : def); }
    return 0;
  }
  /* Editor.putInt/Long/Boolean(...)Z?  most return the Editor (object), but
   * commit() returns Z. Route the primitive puts here since the key+value are
   * primitive-shaped, then have the engine ignore the int return. CHECK. */
  if (has(cls,"SharedPreferences$Editor")){
    if (has(m,"commit")){ debugPrintf("[prefs] commit\n"); prefs_flush(); return 1; }
    if (has(m,"putInt")||has(m,"putLong")){ const char*k=jni_string_utf(va_arg(va,void*));
      long long v=(long long)va_arg(va,long long); char b[32]; snprintf(b,sizeof b,"%lld",v);
      kv_set(has(m,"putLong")?'L':'I',k,b); return (uint64_t)(uintptr_t)recv; }
    if (has(m,"putBoolean")){ const char*k=jni_string_utf(va_arg(va,void*));
      int v=va_arg(va,int); kv_set('B',k,v?"1":"0"); return (uint64_t)(uintptr_t)recv; }
    return (uint64_t)(uintptr_t)recv;
  }

  /* AssetFileDescriptor.getStartOffset()/getLength()/getDeclaredLength() (long) */
  if (has(cls,"AssetFileDescriptor")){
    UHandle*a=recv;
    if (has(m,"getStartOffset")) return (uint64_t)(is_uh(a,UJ_AFD)?a->off:0);
    if (has(m,"getLength")||has(m,"getDeclaredLength")) return (uint64_t)(is_uh(a,UJ_AFD)?a->len:0);
    return 0;
  }
  /* FileDescriptor: some engines read the raw int via a field, not a call.
   * If Unity calls FileDescriptor.getInt$()/getFd(), hand back the fd. CHECK. */
  if (has(cls,"java/io/FileDescriptor")){ UHandle*f=recv; return (uint64_t)(is_uh(f,UJ_FD)?(unsigned)f->fd:0); }

  /* Display / DisplayMetrics / Configuration ints. Report the resolved render resolution
   * (main.c screen_width/height: config.txt override or 1440x2560) so every path the game
   * queries the screen agrees with the buffer we render into. */
  if (has(cls,"view/Display")||has(cls,"DisplayMetrics")||has(cls,"DisplayManager")){
    extern int screen_width, screen_height;
    int sw = screen_width  > 0 ? screen_width  : RD_HANDHELD_W;
    int sh = screen_height > 0 ? screen_height : RD_HANDHELD_H;
    if (has(m,"getWidth")||has(m,"WidthPixels")||has(m,"getRawWidth"))  return sw;
    if (has(m,"getHeight")||has(m,"HeightPixels")||has(m,"getRawHeight"))return sh;
    if (has(m,"getRotation")) return 0;
    if (has(m,"getDisplayId")) return 0;
    return 0;
  }
  return 0;
}

/* ---- void calls --------------------------------------------------------- */
void unity_dispatch_void(void *recv, const void *id_, va_list va){ const struct FakeID *id = id_;
  const char *cls=id->cls, *m=id->name;
  if (has(cls,"java/io/InputStream") && has(m,"close")){ UHandle*h=recv;
    if(is_uh(h,UJ_INPUTSTREAM)&&h->fp){fclose(h->fp);h->fp=NULL;free(h->mem);h->mem=NULL;} return; }
  if (has(cls,"AssetFileDescriptor") && has(m,"close")){ UHandle*a=recv;
    if(is_uh(a,UJ_AFD)&&a->fd>=0){close(a->fd);a->fd=-1;} return; }
  if (has(cls,"SharedPreferences$Editor") && has(m,"apply")){ debugPrintf("[prefs] apply\n"); prefs_flush(); return; }
  if (has(cls,"SharedPreferences$Editor") && has(m,"putString")){ /* if routed here as void */
    const char*k=jni_string_utf(va_arg(va,void*)); const char*v=jni_string_utf(va_arg(va,void*));
    kv_set('S',k,v); return; }
  if (has(cls,"UnityPlayer")){
    /* setOrientation/lowMemory/configurationChanged/etc. -> no-op */
    return;
  }
  (void)recv;(void)va;
}

const char *unity_prefs_get_string(const char *key){
  const KV *kv = key ? kv_get(key) : NULL;
  return kv && kv->type=='S' ? kv->val : NULL;
}
void unity_prefs_put_string(const char *key, const char *val){
  if (key && key[0] && val) kv_set('S',key,val);
}

/* ========================================================================== */
void unity_jni_init(const char *data_root){
  snprintf(g_root,sizeof g_root,"%s",data_root && *data_root ? data_root : RD_GAME_HOME);
  prefs_load();
  /* caller (jni_fake.c jni_init) should also intern every UNITY_JNI_CLASSES[]
   * name so FindClass returns non-NULL classes. */
}
