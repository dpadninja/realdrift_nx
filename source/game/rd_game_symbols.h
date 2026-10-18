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
#define RD_FLD_POWER_MULTIPLIER   "powerMultiplier"     /* x externalMultiplier = actualMultiplier,
                                                           the engine torque factor (FixedUpdate) */
#define RD_FLD_TUNING_MULTIPLIER  "externalMultiplier"  /* the game's engine tuning */
#define RD_FLD_MAX_POWER          "maxPower"            /* hp, the curve's peak (an output) */
#define RD_FLD_TURBO              "turboCharged"
#define RD_FLD_TURBO_BAR          "maxTurboPressure"
#define RD_FLD_TURBO_EXTRA        "externalTurboPressure"
#define RD_FLD_TURBO_LIMIT        "actualMaxTurboPressure"  /* = the two above, set in Awake only */
#define RD_FLD_REV_LIMIT          "limiterRPM"
#define RD_FLD_MAX_RPM            "maxRPM"
#define RD_FLD_IDLE_RPM           "minRPM"
#define RD_FLD_GEAR_RATIOS        "gearRatios"          /* float[]: reverse, neutral, 1st (= `first`).. */
#define RD_FLD_FIRST_GEAR         "first"
#define RD_ASM_CORLIB             "mscorlib"
#define RD_NS_SYSTEM              "System"
#define RD_CLS_SINGLE             "Single"              /* float[] elements */
#define RD_CLS_PERSISTENT_DATA    "PersistentData"
#define RD_MTH_APPLY_GEAR_TUNING  "ApplyGearRatiosTuning"  /* (drivetrain, indexC, gear, ratio):
                                                              gearRatios[gear + 1] = ratio, bounds-checked */
#define RD_FLD_FINAL_RATIO        "finalDriveRatio"
#define RD_FLD_SHIFT_TIME         "shiftTime"
#define RD_FLD_TRANSMISSION       "transmission"        /* enum Transmissions, see RD_DRIVES */
#define RD_MTH_SET_TRANSMISSION   "SetTransmission"     /* rebuilds poweredWheels */
#define RD_DRIVES                 { "RWD", "FWD", "AWD", "XWD", NULL }  /* Transmissions 0..3 */
#define RD_FLD_BRAKE_TORQUE       "brakeFrictionTorque"      /* Axle; SetBrakes -> the wheels */
#define RD_FLD_HANDBRAKE_TORQUE   "handbrakeFrictionTorque"
#define RD_FLD_BRAKE_BALANCE      "frontRearBrakeBalance"    /* CarDynamics */
#define RD_FLD_HANDBRAKE_BALANCE  "frontRearHandBrakeBalance"
#define RD_MTH_SET_BRAKES         "SetBrakes"
#define RD_FLD_TCS                "TCS"
#define RD_FLD_TCS_SLIP           "TCSAllowedSlip"
#define RD_FLD_ESP                "ESP"
#define RD_FLD_ESP_STRENGTH       "ESPStrength"
#define RD_FLD_ABS                "ABS"
#define RD_FLD_ABS_SLIP           "ABSAllowedSlip"
#define RD_FLD_MAX_POWER_RPM      "maxPowerRPM"
#define RD_FLD_MAX_TORQUE_RPM     "maxTorqueRPM"
#define RD_FLD_TORQUE_CURVE       "torqueCurve"         /* AnimationCurve rpm -> Nm (useTorqueCurve) */
#define RD_MTH_CALC_MAX_POWER     "CalcMaxPowerMaxTorque"  /* (factor, turbo): scans the curve into
                                                              maxPower/maxPowerRPM/maxTorque/RPM */
#define RD_FLD_DRIVETRAIN_BODY    "body"                /* the car's Rigidbody */
#define RD_ICALL_SET_MASS         "UnityEngine.Rigidbody::set_mass"
#define RD_ICALL_GET_MASS         "UnityEngine.Rigidbody::get_mass"
#define RD_ASM_CORE_TYPES         RD_ASM_UNITY_CORE
#define RD_CLS_KEYFRAME           "Keyframe"            /* UnityEngine, 28 bytes */
#define RD_ICALL_CURVE_SET_KEYS   "UnityEngine.AnimationCurve::SetKeys"
#define RD_ICALL_CURVE_SMOOTH     "UnityEngine.AnimationCurve::SmoothTangents"
/* CarDynamics and its axles (CarDynamics.axles -> Axles.frontAxle / rearAxle: Axle); the game
 * copies Axle values into the wheels in SetWheelsParams (Start, FixedUpdate, its tuning). */
#define RD_CLS_CAR_DYNAMICS       "CarDynamics"
#define RD_FLD_AXLES              "axles"
#define RD_CLS_AXLES              "Axles"
#define RD_CLS_AXLE               "Axle"
#define RD_FLD_FRONT_AXLE         "frontAxle"
#define RD_FLD_REAR_AXLE          "rearAxle"
#define RD_MTH_SET_WHEELS_PARAMS  "SetWheelsParams"
#define RD_MTH_SET_TIRES_TYPE     "SetTiresType"        /* loads the tire data of Axle.tires */
#define RD_FLD_COM_Z              "zlocalPosition"      /* centre of mass, front-rear: the game's
                                                           "weight distribution" original */
#define RD_FLD_TIRE_PRESSURE_ON   "tirePressureEnabled"
#define RD_FLD_SUSP_TRAVEL        "suspensionTravel"
#define RD_FLD_SUSP_RATE          "suspensionRate"
#define RD_FLD_SUSP_BUMP          "bumpRate"
#define RD_FLD_SUSP_REBOUND       "reboundRate"
#define RD_FLD_ANTIROLL           "antiRollBarRate"
#define RD_FLD_CAMBER             "camber"
#define RD_FLD_STEERING_ANGLE     "maxSteeringAngle"
#define RD_FLD_FORWARD_GRIP       "forwardGripFactor"
#define RD_FLD_SIDEWAYS_GRIP      "sidewaysGripFactor"
#define RD_FLD_TIRES              "tires"               /* enum CarDynamics.Tires, see RD_TIRES */
#define RD_FLD_TIRES_PRESSURE     "tiresPressure"
#define RD_FLD_DIFFERENTIAL       "differential"        /* enum Axles.Differentials, see RD_DIFFS */
#define RD_FLD_DIFF_PRELOAD       "preLoad"
#define RD_FLD_DIFF_POWER         "powerRatio"
#define RD_FLD_DIFF_COAST         "coastRatio"
#define RD_DIFFS { "open", "viscous", "locked", "one_way_LSD", "two_way_LSD", "one_point_five_way_LSD", NULL }
#define RD_TIRES { "F1_front", "F1_rear", "competition_front", "competition_rear",               \
                   "competition_frontNew", "competition_rearNew", "supersport_front",            \
                   "supersport_rear", "sport_front", "sport_rear", "touring_front", "touring_rear", \
                   "offroad_front", "offroad_rear", "truck_front", "truck_rear", NULL }
/* The garage loads "<prefab>_Garage" or "<prefab>_cover" (MenuManager.LoadCar), the race
 * "<prefab>(Clone)"; MenuManager.LoadOriginalValues then reads the car's originals from that
 * model, and RaceManager.UpdateVehicle builds the race car from them. */
#define RD_CAR_NAME_SUFFIXES      { "(Clone)", "_Garage", "_cover", "_Cover", NULL }
#define RD_FLD_MANAGER_DRIVETRAIN "drivetrain"          /* MenuManager / RaceManager: the car */
#define RD_FLD_MANAGER_DYNAMICS   "carDynamics"
#define RD_FLD_MANAGER_BODY       "rigidbody"           /* MenuManager only */
#define RD_CLS_MENU_MANAGER       "MenuManager"
#define RD_MTH_LOAD_ORIGINALS     "LoadOriginalValues"
#define RD_CLS_RACE_MANAGER       "RaceManager"
#define RD_MTH_UPDATE_VEHICLE     "UpdateVehicle"

#define RD_ICALL_ACTIVE_SCENE     "UnityEngine.SceneManagement.SceneManager::GetActiveScene_Injected"
#define RD_ICALL_SCENE_NAME       "UnityEngine.SceneManagement.Scene::GetNameInternal"
#define RD_ICALL_OBJECT_NAME      "UnityEngine.Object::GetName"
#define RD_ICALL_GAME_OBJECT      "UnityEngine.Component::get_gameObject"
#define RD_ICALL_SHARED_MATERIAL  "UnityEngine.Renderer::GetSharedMaterial"

#endif
