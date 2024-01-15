/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef INCLUDE_ALSA_CONFORMANCE_TIMER_H_
#define INCLUDE_ALSA_CONFORMANCE_TIMER_H_

#include <time.h>

#define MAX_ALSA_API_LENGTH 25

enum ALSA_API {
	SND_PCM_OPEN = 0,
	SND_PCM_HW_PARAMS,
	SND_PCM_HW_PARAMS_ANY,
	SND_PCM_SW_PARAMS,
	SND_PCM_PREPARE,
	SND_PCM_START,
	SND_PCM_AVAIL,
        SND_PCM_AVAIL_DELAY,
	ALSA_API_COUNT /* Keep it in the last line to count total amounts. */
};

#define ENUM_STR(x)                                                            \
	case x:                                                                \
		return #x;

static inline const char *alsa_api_str(enum ALSA_API api)
{
	switch (api) {
		ENUM_STR(SND_PCM_OPEN)
		ENUM_STR(SND_PCM_HW_PARAMS)
		ENUM_STR(SND_PCM_HW_PARAMS_ANY)
		ENUM_STR(SND_PCM_SW_PARAMS)
		ENUM_STR(SND_PCM_PREPARE)
		ENUM_STR(SND_PCM_START)
		ENUM_STR(SND_PCM_AVAIL)
		ENUM_STR(SND_PCM_AVAIL_DELAY)
	default:
		return "INVALID_API";
	}
}

struct alsa_conformance_timer;

/* Returns true if timespec a later than timespec b. */
int timespec_after(const struct timespec *a, const struct timespec *b);

/* Timespec a += b. */
void add_timespec(struct timespec *a, const struct timespec *b);

/* Timespec a -= b. */
void subtract_timespec(struct timespec *a, const struct timespec *b);

/* Returns string of timespec, need to be freed after usage.*/
char *timespec_to_str(const struct timespec *time);

/* Returns nanosecond of timespec. */
long long timespec_to_ns(const struct timespec *time);

/* Returns second of timespec. */
double timespec_to_s(const struct timespec *time);

/* Creates and initializes a new timer object. */
struct alsa_conformance_timer *conformance_timer_create();

/* Destroys timer object */
void conformance_timer_destroy(struct alsa_conformance_timer *timer);

/* Starts api_timer of this alsa api. */
void conformance_timer_start(struct alsa_conformance_timer *timer,
			     enum ALSA_API id);

/* Stops api_timer and records time of this alsa api. */
void conformance_timer_stop(struct alsa_conformance_timer *timer,
			    enum ALSA_API id);

/* Enables timer. It will record data. */
void conformance_timer_enable(struct alsa_conformance_timer *timer);

/* Disables timer. It will not record any data until it is enabled. */
void conformance_timer_disable(struct alsa_conformance_timer *timer);

/* Prints timer result. */
void conformance_timer_print_result(const struct alsa_conformance_timer *timer);

#endif /* INCLUDE_ALSA_CONFORMANCE_TIMER_H_ */
