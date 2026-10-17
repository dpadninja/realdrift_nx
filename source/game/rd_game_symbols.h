/* rd_game_symbols.h -- names in the game's C# code (Assembly-CSharp) and in Unity's managed
 * modules that the port hooks or calls. Everything is resolved BY NAME at run time
 * (il2cpp_class_from_name / _get_method_from_name / _get_field_from_name), so a new game build
 * only breaks if one of these names changes. Check them against Il2CppDumper's dump.cs
 * (analysis/research/10-patch-sites.md). Input icall names: unity_input_hook.c. */
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

#endif
