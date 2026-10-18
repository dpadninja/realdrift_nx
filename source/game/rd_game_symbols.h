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
/* Scene change detection for mods (mods/rd_mods.c): void (Scene *out), string (int handle) */
/* cars.ini (mods/rd_mods.c): a component's car is found by walking up its object tree */
#define RD_ICALL_TRANSFORM        "UnityEngine.Component::get_transform"
#define RD_ICALL_PARENT           "UnityEngine.Transform::GetParent"
/* The components on a car whose fields a cars.ini key may name without "Class." (searched in
 * this order; a field name found in two of them must be written Class.field). */
#define RD_CAR_COMPONENTS         { "CarController", "Drivetrain", "CarDynamics", "CarSpecs" }
/* cars.ini's own keys (mods/rd_mod_cars.c); findings in analysis/research/14-modding.md.
 * Car components, read by the game while driving: */
#define RD_CLS_DRIVETRAIN         "Drivetrain"
#define RD_CLS_CAR_BASE           "CarController"
#define RD_CLS_WHEEL              "Wheel"
#define RD_FLD_POWER_MULTIPLIER   "powerMultiplier"     /* x externalMultiplier = actualMultiplier,
                                                           the engine torque factor (FixedUpdate) */
#define RD_FLD_TUNING_MULTIPLIER  "externalMultiplier"  /* the game's engine tuning */
#define RD_FLD_MAX_POWER          "maxPower"            /* hp, the curve's peak (an output) */
#define RD_FLD_MAX_TORQUE         "maxTorque"           /* Nm, the curve's peak (an output) */
#define RD_FLD_TURBO              "turboCharged"
#define RD_FLD_TURBO_BAR          "maxTurboPressure"
#define RD_FLD_TURBO_EXTRA        "externalTurboPressure"
#define RD_FLD_TURBO_LIMIT        "actualMaxTurboPressure"  /* = the two above, set in Awake only */
#define RD_FLD_REV_LIMIT          "limiterRPM"
#define RD_FLD_MAX_RPM            "maxRPM"
#define RD_FLD_IDLE_RPM           "minRPM"
#define RD_FLD_GEAR_RATIOS        "gearRatios"          /* float[]: reverse, neutral, 1st (= `first`).. */
#define RD_FLD_FIRST_GEAR         "first"
#define RD_FLD_FINAL_RATIO        "finalDriveRatio"
#define RD_FLD_TRANSMISSION       "transmission"        /* enum Transmissions, see RD_DRIVES */
#define RD_MTH_SET_TRANSMISSION   "SetTransmission"     /* rebuilds poweredWheels */
#define RD_DRIVES                 { "RWD", "FWD", "AWD", "XWD" }  /* Transmissions 0..3 */
#define RD_FLD_BRAKE_TORQUE       "brakeFrictionTorque"
#define RD_FLD_HANDBRAKE_TORQUE   "handbrakeFrictionTorque"
#define RD_FLD_TCS                "TCS"
#define RD_FLD_TCS_SLIP           "TCSAllowedSlip"
#define RD_FLD_ESP                "ESP"
#define RD_FLD_ESP_STRENGTH       "ESPStrength"
#define RD_FLD_ABS                "ABS"
#define RD_FLD_ABS_SLIP           "ABSAllowedSlip"
/* The car's base data (PersistentData.vehicles[]: Vehicle), which the game's tuning turns into
 * the car's values when it builds the car (MenuManager.UpdateCar, RaceManager.UpdateVehicle).
 * Not saved: Vehicle.WriteToStream writes only customizations and track results. */
#define RD_CLS_PERSISTENT         "PersistentData"
#define RD_FLD_VEHICLES           "vehicles"
#define RD_CLS_VEHICLE            "Vehicle"
#define RD_FLD_VEHICLE_PREFAB     "prefabPath"          /* its last part names the car object */
#define RD_FLD_VEHICLE_ID         "id"
#define RD_FLD_VEHICLE_NAME       "name"
#define RD_FLD_BASE_WEIGHT                "originalWeight"
#define RD_FLD_BASE_SHIFT_TIME            "originalShiftTime"
#define RD_FLD_BASE_SUSP_HEIGHT           "originalSuspHeight"
#define RD_FLD_BASE_FRONT_CAMBER          "originalFrontCamber"
#define RD_FLD_BASE_REAR_CAMBER           "originalRearCamber"
#define RD_FLD_BASE_STEERING_ANGLE        "originalSteeringAngle"
#define RD_FLD_BASE_WEIGHT_DISTR          "originalWeightDistr"
#define RD_FLD_BASE_FRONT_FORWARD_GRIP    "originalFrontForwardGrip"
#define RD_FLD_BASE_FRONT_SIDEWAYS_GRIP   "originalFrontSidewaysGrip"
#define RD_FLD_BASE_REAR_FORWARD_GRIP     "originalRearForwardGrip"
#define RD_FLD_BASE_REAR_SIDEWAYS_GRIP    "originalRearSidewaysGrip"

#define RD_ICALL_ACTIVE_SCENE     "UnityEngine.SceneManagement.SceneManager::GetActiveScene_Injected"
#define RD_ICALL_SCENE_NAME       "UnityEngine.SceneManagement.Scene::GetNameInternal"
#define RD_ICALL_OBJECT_NAME      "UnityEngine.Object::GetName"
#define RD_ICALL_GAME_OBJECT      "UnityEngine.Component::get_gameObject"
#define RD_ICALL_SHARED_MATERIAL  "UnityEngine.Renderer::GetSharedMaterial"

#endif
