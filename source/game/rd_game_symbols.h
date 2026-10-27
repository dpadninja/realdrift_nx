/* rd_game_symbols.h -- names in the game's C# code (Assembly-CSharp) and in Unity's managed
 * modules that the port hooks or calls. Everything is resolved BY NAME at run time
 * (il2cpp_class_from_name / _get_method_from_name / _get_field_from_name), so a new game build
 * only breaks if one of these names changes. Check them against Il2CppDumper's dump.cs
 * (analysis/research/10-patch-sites.md). Input icall names: unity_input_hook.c.
 * MIT (see LICENSE). */
#ifndef RD_GAME_SYMBOLS_H
#define RD_GAME_SYMBOLS_H

/* ---- assemblies / namespaces --------------------------------------------------------- */
#define RD_ASM_GAME            "Assembly-CSharp"
#define RD_ASM_UNITY_CORE      "UnityEngine.CoreModule"
#define RD_NS_GAME             ""                 /* the game's classes have no namespace */
#define RD_NS_UNITY            "UnityEngine"

/* ---- MobileCarController: the car's input source (rd_drive_hook.c) ------------------- */
#define RD_CLS_CAR_CONTROLLER  "MobileCarController"
/* void GetInput(out float throttle, brake, steer, handbrake, clutch,
 *               out bool startEngine, changeCamera, out int targetGear) -- virtual */
#define RD_MTH_GET_INPUT       "GetInput"
#define RD_MTH_GET_INPUT_ARGC  8
#define RD_FLD_STEER_INPUT     "mobileSteerInput" /* float, what the touch slider writes */

/* The game's on-screen buttons call these (no arguments), indexed by rd_action (rd_drive.h).
 * NULL release = the button has no release handler. */
#define RD_CAR_PRESS_METHODS   { "OnThrottlePress", "OnBrakePress", "OnHandbrakePress", \
                                 "OnGearUpPress", "OnGearDownPress", "OnChangeCamera" }
#define RD_CAR_RELEASE_METHODS { "OnThrottleRelease", "OnBrakeRelease", "OnHandbrakeRelease", \
                                 "OnGearUpRelease", "OnGearDownRelease", NULL }

/* ---- UIManager / RaceManager: race HUD and pause (rd_race_ui.c) ---------------------- */
#define RD_CLS_UI_MANAGER      "UIManager"
#define RD_CLS_RACE_MANAGER    "RaceManager"
#define RD_MTH_UPDATE          "Update"
#define RD_MTH_PAUSE_ON        "EnablePauseMode"
#define RD_MTH_PAUSE_OFF       "DisablePauseMode"

/* UIManager fields; the order is the rd_uim_field enum (rd_race_ui.c). */
#define RD_UIM_FIELD_NAMES     { "raceManager", \
                                 "shiftButtons", "leftRightButtons", "handbrakeButton", \
                                 "handbrakeRightButton", "sliderControls", "buttonControls" }
/* RaceManager fields; the order is the rd_rm_field enum. */
#define RD_RM_FIELD_NAMES      { "pausePanel", "isRaceEnded", "isPhotoMode" }

/* ---- UnityEngine.GameObject -------------------------------------------------------- */
#define RD_CLS_GAME_OBJECT     "GameObject"
#define RD_MTH_GET_ACTIVE_SELF "get_activeSelf"
#define RD_MTH_SET_ACTIVE      "SetActive"

/* ---- tyre smoke: Unity particle systems (rd_smoke_hook.c) ----------------------------- */
#define RD_ASM_PARTICLES          "UnityEngine.ParticleSystemModule"
#define RD_CLS_PARTICLE_SYSTEM    "ParticleSystem"
#define RD_CLS_PARTICLE           "Particle"       /* nested: ParticleSystem/Particle (struct) */
#define RD_FLD_PARTICLE_START_SIZE "m_StartSize"   /* Vector3 */
#define RD_CLS_PARTICLE_RENDERER  "ParticleSystemRenderer"
#define RD_SMOKE_MATERIAL         "SkidSmoke"      /* material of ParticleTrack / ParticleGrass */
/* Engine icalls. Only the ones the game's own code uses are registered (the rest of the
 * particle API, e.g. maxParticleSize / maxParticles, is not: il2cpp_resolve_icall fails). */
#define RD_ICALL_EMIT_OLD         "UnityEngine.ParticleSystem::Internal_EmitOld"   /* wrapped */
#define RD_ICALL_FIND_OBJECTS     "UnityEngine.Object::FindObjectsOfType"
#define RD_ICALL_OBJECT_NAME      "UnityEngine.Object::GetName"
#define RD_ICALL_GAME_OBJECT      "UnityEngine.Component::get_gameObject"
#define RD_ICALL_SHARED_MATERIAL  "UnityEngine.Renderer::GetSharedMaterial"
#define RD_ICALL_TIME_SCALE       "UnityEngine.Time::get_timeScale"   /* 0 on the pause menu */

/* ---- menu navigation (rd_menu_hook.c; the uGUI names are in engine/rd_ugui.c) -------- */
#define RD_ICALL_GET_KEY_DOWN     "UnityEngine.Input::GetKeyDownInt"     /* wrapped: Escape */
#define RD_ICALL_ACTIVE_SELF      "UnityEngine.GameObject::get_activeSelf"
/* Every SceneManager.LoadScene* ends here; the game's are synchronous (PersistentData.StartRace,
 * RaceManager.PreLoadScene): the next frame loads for seconds and the last one stays shown. */
#define RD_ICALL_LOAD_SCENE \
  "UnityEngine.SceneManagement.SceneManager::LoadSceneAsyncNameIndexInternal_Injected"   /* wrapped */
#define RD_ICALL_ACTIVE_SCENE     "UnityEngine.SceneManagement.SceneManager::GetActiveScene_Injected"
/* The loading screen between menu and track: drawn with the legacy GUILayer, invisible to uGUI
 * raycasts, while the menu's buttons still exist behind it. */
#define RD_CLS_LOADING_SCREEN     "LoadingScreenRandomizer"
/* The track preview before a race: UIManager.Update starts the race on any touch while
 * introSequence (a uSequencer) plays and introSequenceStopped is false. */
#define RD_UIM_INTRO_FIELD_NAMES  { "introSequence", "introSequenceStopped" }
#define RD_ASM_USEQ               "uSequencerRuntime"
#define RD_NS_USEQ                "WellFired"
#define RD_CLS_USEQ               "USSequencer"
#define RD_MTH_IS_PLAYING         "get_IsPlaying"

/* ---- offline menus: online features hidden (rd_offline.c, rd_offline_hook.c) ---------- */
#define RD_CLS_MENU_MANAGER       "MenuManager"      /* the menu scene's root GameObject */
#define RD_CLS_PERSISTENT_DATA    "PersistentData"
/* PersistentData.leaderboardType (enum LeaderboardType: onlineCountry 0, onlineWorld 1,
 * local 2); LeaderboardScrollerController.OnEnable loads the board it names. */
#define RD_FLD_LEADERBOARD_TYPE   "leaderboardType"
#define RD_LEADERBOARD_LOCAL      2
/* PersistentData.leaderboardRaceMode (RaceMode: driftRace 0, timeAttackRace 1): the board's
 * sort. The local scores do not record the mode, so "time attack" only re-sorts the same
 * runs by their time as a string; the port keeps driftRace (by points). */
#define RD_FLD_LEADERBOARD_MODE   "leaderboardRaceMode"
#define RD_RACE_MODE_DRIFT        0
#define RD_ASM_TMP                "Unity.TextMeshPro"
#define RD_NS_TMP                 "TMPro"
#define RD_CLS_TMP_TEXT           "TMP_Text"
#define RD_CLS_TMP_INPUT          "TMP_InputField"
#define RD_MTH_GET_TEXT           "get_text"
#define RD_MTH_SET_TEXT           "set_text"
#define RD_ICALL_TRANSFORM        "UnityEngine.Component::get_transform"
#define RD_ICALL_FIND_PATH        "UnityEngine.Transform::FindRelativeTransformWithPath"
#define RD_ICALL_SET_ACTIVE       "UnityEngine.GameObject::SetActive"
#define RD_ICALL_GET_COMPONENT    "UnityEngine.GameObject::GetComponent"
#define RD_ICALL_GET_ENABLED      "UnityEngine.Behaviour::get_enabled"
#define RD_ICALL_SET_ENABLED      "UnityEngine.Behaviour::set_enabled"
#define RD_ICALL_GET_ANCHORED     "UnityEngine.RectTransform::get_anchoredPosition_Injected"
#define RD_ICALL_SET_ANCHORED     "UnityEngine.RectTransform::set_anchoredPosition_Injected"
/* PlayerPrefs keys: PersistentData.Awake reads playerName (writes "Drifter_<n>" if empty);
 * CountryDetection reads CountryCode (ISO 3166 alpha-2, one of the 189 flags) and, without
 * it, MenuManager.Init asks a web service by IP -- offline that fails and the game stores
 * the first country of its list, Andorra. */
#define RD_PREF_PLAYER_NAME       "playerName"
#define RD_PREF_COUNTRY           "CountryCode"
#define RD_COUNTRY_FAILED_LOOKUP  "AD"

/* ---- Graphics Enhancements: SleekRender post-process (rd_sleek_hook.c) ---------------- */
#define RD_ASM_FIRSTPASS          "Assembly-CSharp-firstpass"   /* SleekRender is a plugin */
#define RD_NS_SLEEK               "SleekRender"
#define RD_CLS_SLEEK_POST         "SleekRenderPostProcess"
#define RD_MTH_SLEEK_SET_CLEAR    "SetActiveRenderTextureAndClear"   /* static, 1 argument */

#endif
