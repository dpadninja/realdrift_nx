/* sources: source/mods/rd_mod_ini.c */
/* Host unit test of rd_mod_ini: mod.ini and patch.ini reading. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_mod_ini.h"

static rd_ini g_ini;   /* large: keep it off the stack */

/* Keys, sections, comments, blank lines and surrounding spaces parse as documented, and the last
 * occurrence of a key wins (a user's later line overrides). */
static void test_parse(void) {
  const char *text = "# comment\n; also\n\nid = hello\n name=Hello mod \n[Assembly-CSharp:Drivetrain]\n"
                     "limiterRPM = 9500\nid = other\n[x]\nk=1\nk=2\n";
  CHECK(rd_ini_parse(text, &g_ini) == 0);
  CHECK(!strcmp(rd_ini_get(&g_ini, "", "id"), "hello"));
  CHECK(!strcmp(rd_ini_get(&g_ini, "", "name"), "Hello mod"));
  CHECK(!strcmp(rd_ini_get(&g_ini, "Assembly-CSharp:Drivetrain", "limiterRPM"), "9500"));
  CHECK(!strcmp(rd_ini_get(&g_ini, "Assembly-CSharp:Drivetrain", "id"), "other"));
  CHECK(!strcmp(rd_ini_get(&g_ini, "x", "k"), "2"));
  CHECK(rd_ini_get(&g_ini, "", "missing") == NULL);
  CHECK(g_ini.e[0].line == 4);   /* line numbers for error messages */
}

/* A bad line is reported by number so the log can say where: no '=', empty key, unclosed
 * section, a value too long; CRLF line ends parse like LF. */
static void test_errors(void) {
  CHECK(rd_ini_parse("id = a\nnonsense\n", &g_ini) == 2);
  CHECK(rd_ini_parse(" = x\n", &g_ini) == 1);
  CHECK(rd_ini_parse("[open\n", &g_ini) == 1);
  char longv[400] = "k = ";
  memset(longv + 4, 'v', 300);
  CHECK(rd_ini_parse(longv, &g_ini) == 1);
  CHECK(rd_ini_parse("id = a\r\napi = 1\r\n", &g_ini) == 0 && !strcmp(rd_ini_get(&g_ini, "", "api"), "1"));
  CHECK(rd_ini_parse("", &g_ini) == 0 && g_ini.n == 0);
}

/* mod.ini: required keys, defaults, id rules (it must equal the folder, so two mods cannot
 * claim one id), numeric keys checked. */
static void test_mod_info(void) {
  rd_mod_info info;
  char err[160];
  CHECK(rd_ini_parse("id = hello\napi = 1\n", &g_ini) == 0);
  CHECK(rd_mod_info_read(&g_ini, "hello", &info, err, sizeof err) == 0);
  CHECK(!strcmp(info.name, "hello") && info.enabled == 1 && info.priority == 0 && info.api == 1);
  CHECK(rd_mod_info_read(&g_ini, "other", &info, err, sizeof err) == -1 && strstr(err, "folder"));
  CHECK(rd_ini_parse("id = hello\n", &g_ini) == 0);
  CHECK(rd_mod_info_read(&g_ini, "hello", &info, err, sizeof err) == -1 && strstr(err, "\"api\""));
  CHECK(rd_ini_parse("id = Hello\napi = 1\n", &g_ini) == 0);
  CHECK(rd_mod_info_read(&g_ini, "Hello", &info, err, sizeof err) == -1);   /* upper case */
  CHECK(rd_ini_parse("id = a\napi = 1\npriority = high\n", &g_ini) == 0);
  CHECK(rd_mod_info_read(&g_ini, "a", &info, err, sizeof err) == -1 && strstr(err, "priority"));
  CHECK(rd_ini_parse("id = a\napi = 1\nenabled = 2\n", &g_ini) == 0);
  CHECK(rd_mod_info_read(&g_ini, "a", &info, err, sizeof err) == -1);
  CHECK(rd_ini_parse("id = a\napi = 1\nenabled = 0\npriority = -5\nversion = 1.2\n", &g_ini) == 0);
  CHECK(rd_mod_info_read(&g_ini, "a", &info, err, sizeof err) == 0);
  CHECK(info.enabled == 0 && info.priority == -5 && !strcmp(info.version, "1.2"));
}

/* The documented key table lists the required keys the reader enforces. */
static void test_key_table(void) {
  int required = 0;
  for (int i = 0; i < RD_MOD_INI_NKEYS; i++) required += RD_MOD_INI_KEYS[i].required;
  CHECK(required == 2 && RD_MOD_INI_NKEYS == 6);
}

/* patch.ini sections split into assembly, namespace and class, with nested classes kept whole
 * and the game's namespace-less classes allowed; malformed sections are refused. */
static void test_patch_section(void) {
  char a[64], ns[64], c[64];
  CHECK(rd_patch_section("Assembly-CSharp:Drivetrain", a, 64, ns, 64, c, 64) == 0);
  CHECK(!strcmp(a, "Assembly-CSharp") && !strcmp(ns, "") && !strcmp(c, "Drivetrain"));
  CHECK(rd_patch_section("UnityEngine.CoreModule:UnityEngine.Camera", a, 64, ns, 64, c, 64) == 0);
  CHECK(!strcmp(a, "UnityEngine.CoreModule") && !strcmp(ns, "UnityEngine") && !strcmp(c, "Camera"));
  CHECK(rd_patch_section("Asm:Ns.Outer/Inner.X", a, 64, ns, 64, c, 64) == 0);
  CHECK(!strcmp(ns, "Ns") && !strcmp(c, "Outer/Inner.X"));
  CHECK(rd_patch_section("NoColon", a, 64, ns, 64, c, 64) == -1);
  CHECK(rd_patch_section(":Cls", a, 64, ns, 64, c, 64) == -1);
  CHECK(rd_patch_section("Asm:", a, 64, ns, 64, c, 64) == -1);
}

int main(void) {
  RUN(test_parse);
  RUN(test_errors);
  RUN(test_mod_info);
  RUN(test_key_table);
  RUN(test_patch_section);
  return g_failures != 0;
}
