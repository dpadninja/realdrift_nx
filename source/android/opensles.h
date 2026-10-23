/* opensles.h -- minimal OpenSL ES shim (FMOD's OpenSL output in libunity)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 *
 * libunity's FMOD links against the Android OpenSL ES system library, which has no
 * devkitPro equivalent. We implement just enough of the object model
 * (Engine -> OutputMix / AudioPlayer with an Android simple buffer queue, plus
 * the Play and Volume interfaces) and back the buffer-queue players with a
 * single SDL2 audio device that software-mixes them.
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE).
 */

#ifndef __OPENSLES_H__
#define __OPENSLES_H__

#include <stdint.h>

/* The audio output the engine is told about vs. what the Switch plays. FMOD asks Android's
 * AudioManager (jni_fake.c getProperty) for the native rate / buffer size and opens OpenSL ES
 * with them; we play through SDL at the device rate and resample each player in mix_player. */
#define RD_AUDIO_DEVICE_RATE       48000
#define RD_AUDIO_REPORTED_RATE     24000
#define RD_AUDIO_FRAMES_PER_BUFFER 256

/* OpenSL ES entry point: creates the engine object (SL_RESULT_SUCCESS = 0 or an SL error). */
uint32_t slCreateEngine(void **pEngine, uint32_t numOptions, const void *pEngineOptions,
                        uint32_t numInterfaces, const void *pInterfaceIds,
                        const void *pInterfaceRequired);

/* debug_audio, main loop about once a second: logs when the SDL audio callback stopped being
 * called since the previous check (and when it runs again), with the players' state. */
void opensles_audio_watch(void);

/* Closes the SDL audio device (at exit). */
void opensles_shutdown(void);


// interface-id tokens referenced by the engine's relocations. Each is a unique
// non-NULL sentinel (self-addressed); the engine passes the value to
// GetInterface and we compare pointers.
extern void *SL_IID_3DCOMMIT, *SL_IID_3DDOPPLER, *SL_IID_3DGROUPING, *SL_IID_3DLOCATION;
extern void *SL_IID_3DMACROSCOPIC, *SL_IID_3DSOURCE, *SL_IID_ANDROIDCONFIGURATION;
extern void *SL_IID_ANDROIDEFFECT, *SL_IID_ANDROIDEFFECTCAPABILITIES, *SL_IID_ANDROIDEFFECTSEND;
extern void *SL_IID_ANDROIDSIMPLEBUFFERQUEUE, *SL_IID_AUDIODECODERCAPABILITIES, *SL_IID_AUDIOENCODER;
extern void *SL_IID_AUDIOENCODERCAPABILITIES, *SL_IID_AUDIOIODEVICECAPABILITIES, *SL_IID_BASSBOOST;
extern void *SL_IID_BUFFERQUEUE, *SL_IID_DEVICEVOLUME, *SL_IID_DYNAMICINTERFACEMANAGEMENT;
extern void *SL_IID_DYNAMICSOURCE, *SL_IID_EFFECTSEND, *SL_IID_ENGINE, *SL_IID_ENGINECAPABILITIES;
extern void *SL_IID_ENVIRONMENTALREVERB, *SL_IID_EQUALIZER, *SL_IID_LED, *SL_IID_METADATAEXTRACTION;
extern void *SL_IID_METADATATRAVERSAL, *SL_IID_MIDIMESSAGE, *SL_IID_MIDIMUTESOLO, *SL_IID_MIDITEMPO;
extern void *SL_IID_MIDITIME, *SL_IID_MUTESOLO, *SL_IID_NULL, *SL_IID_OBJECT, *SL_IID_OUTPUTMIX;
extern void *SL_IID_PITCH, *SL_IID_PLAY, *SL_IID_PLAYBACKRATE, *SL_IID_PREFETCHSTATUS;
extern void *SL_IID_PRESETREVERB, *SL_IID_RATEPITCH, *SL_IID_RECORD, *SL_IID_SEEK, *SL_IID_THREADSYNC;
extern void *SL_IID_VIBRA, *SL_IID_VIRTUALIZER, *SL_IID_VISUALIZATION, *SL_IID_VOLUME;

#endif
