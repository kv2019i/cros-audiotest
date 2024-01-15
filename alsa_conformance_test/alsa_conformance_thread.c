/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "alsa_conformance_debug.h"
#include "alsa_conformance_helper.h"
#include "alsa_conformance_recorder.h"
#include "alsa_conformance_thread.h"
#include "alsa_conformance_timer.h"

#define CHANNELS_MAX 16

extern int DEBUG_MODE;
extern int SINGLE_THREAD;
extern int STRICT_MODE;

struct dev_thread {
	snd_pcm_t *handle;
	snd_pcm_hw_params_t *params;
	snd_pcm_hw_params_t *params_record;
	snd_pcm_info_t *pcm_info;
	snd_ctl_card_info_t *card_info;

	char *dev_name;
	snd_pcm_stream_t stream;
	unsigned int channels;
	snd_pcm_format_t format;
	unsigned int rate;
	snd_pcm_uframes_t period_size;
	unsigned int block_size;
	double duration;
	int iterations;

	/* True if all samples captured in that channel are zeros. */
	bool zero_channels[CHANNELS_MAX];

	double merge_threshold_t;
	snd_pcm_sframes_t merge_threshold_sz;
	unsigned underrun_count; /* Record number of underruns during playback. */
	unsigned overrun_count; /* Record number of overrun during capture. */

	struct alsa_conformance_timer *timer;
	struct alsa_conformance_recorder_list *recorder_list;
};

struct dev_thread *dev_thread_create()
{
	struct dev_thread *thread;
	int i;

	thread = (struct dev_thread *)malloc(sizeof(struct dev_thread));
	if (!thread) {
		perror("malloc (dev_thread)");
		exit(EXIT_FAILURE);
	}

	thread->handle = NULL;
	thread->params = NULL;
	thread->params_record = NULL;
	thread->pcm_info = NULL;
	thread->card_info = NULL;
	thread->dev_name = NULL;
	thread->underrun_count = 0;
	thread->overrun_count = 0;
	thread->timer = conformance_timer_create();
	thread->recorder_list = recorder_list_create();
	thread->merge_threshold_t = 0;
	thread->merge_threshold_sz = 0;

	for (i = 0; i < CHANNELS_MAX; i++)
		thread->zero_channels[i] = true;

	return thread;
}

void dev_thread_destroy(struct dev_thread *thread)
{
	snd_pcm_hw_params_free(thread->params_record);
	snd_pcm_info_free(thread->pcm_info);
	snd_ctl_card_info_free(thread->card_info);
	conformance_timer_destroy(thread->timer);
	recorder_list_destroy(thread->recorder_list);
	free(thread->dev_name);
	free(thread);
}

void dev_thread_set_stream(struct dev_thread *thread, snd_pcm_stream_t stream)
{
	thread->stream = stream;
}

void dev_thread_set_dev_name(struct dev_thread *thread, const char *name)
{
	free(thread->dev_name);
	thread->dev_name = strdup(name);
}

void dev_thread_set_merge_threshold_t(struct dev_thread *thread,
				      double merge_threshold)
{
	thread->merge_threshold_t = merge_threshold;
}

void dev_thread_set_merge_threshold_size(struct dev_thread *thread,
					 snd_pcm_sframes_t merge_threshold_sz)
{
	thread->merge_threshold_sz = merge_threshold_sz;
}

void dev_thread_set_channels(struct dev_thread *thread, unsigned int channels)
{
	thread->channels = channels;
}

void dev_thread_set_format(struct dev_thread *thread, snd_pcm_format_t format)
{
	thread->format = format;
}

void dev_thread_set_format_from_str(struct dev_thread *thread,
				    const char *format_str)
{
	snd_pcm_format_t format;
	format = snd_pcm_format_value(format_str);
	if (format == SND_PCM_FORMAT_UNKNOWN) {
		fprintf(stderr, "unknown format: %s\n", format_str);
		exit(EXIT_FAILURE);
	}
	thread->format = format;
}

void dev_thread_set_rate(struct dev_thread *thread, unsigned int rate)
{
	thread->rate = rate;
}

void dev_thread_set_period_size(struct dev_thread *thread,
				snd_pcm_uframes_t period_size)
{
	thread->period_size = period_size;
}

void dev_thread_set_block_size(struct dev_thread *thread, unsigned int size)
{
	thread->block_size = size;
}

void dev_thread_set_duration(struct dev_thread *thread, double duration)
{
	thread->duration = duration;
}

void dev_thread_set_iterations(struct dev_thread *thread, int iterations)
{
	thread->iterations = iterations;
}

/* Open device and initialize params. */
void dev_thread_open_device(struct dev_thread *thread)
{
	int rc;
	assert(thread->dev_name);
	rc = alsa_helper_open(thread->timer, &thread->handle, &thread->params,
			      thread->dev_name, thread->stream);
	if (rc < 0)
		exit(EXIT_FAILURE);

	/* Records pcm_info and card_info to show it on the result. */
	if (thread->pcm_info == NULL)
		snd_pcm_info_malloc(&thread->pcm_info);
	if (thread->card_info == NULL)
		snd_ctl_card_info_malloc(&thread->card_info);
	alsa_helper_get_card_info(thread->handle, thread->pcm_info,
				  thread->card_info);
}

/* Close device. */
void dev_thread_close_device(struct dev_thread *thread)
{
	assert(thread->handle);
	snd_pcm_hw_params_free(thread->params);
	alsa_helper_close(thread->handle);
	thread->handle = NULL;
	thread->params = NULL;
}

void dev_thread_set_params(struct dev_thread *thread)
{
	unsigned int rate;
	snd_pcm_uframes_t period_size;
	int rc;

	assert(thread->handle);
	rate = thread->rate;
	period_size = thread->period_size;
	rc = alsa_helper_set_hw_params(thread->timer, thread->handle,
				       thread->params, thread->format,
				       thread->channels, &thread->rate,
				       &thread->period_size);
	if (rc < 0)
		exit(EXIT_FAILURE);

	if (STRICT_MODE) {
		if (rate != thread->rate) {
			fprintf(stderr, "%s want to set rate %u but get %u.\n",
				thread->dev_name, rate, thread->rate);
			exit(EXIT_FAILURE);
		}
		if (period_size != 0 && period_size != thread->period_size) {
			fprintf(stderr,
				"%s want to set period_size %lu but get %lu.\n",
				thread->dev_name, period_size,
				thread->period_size);
			exit(EXIT_FAILURE);
		}
	}

	rc = alsa_helper_set_sw_param(thread->timer, thread->handle);
	if (rc < 0)
		exit(EXIT_FAILURE);

	/* Records hw_params to show it on the result. */
	if (thread->params_record == NULL)
		snd_pcm_hw_params_malloc(&thread->params_record);
	snd_pcm_hw_params_copy(thread->params_record, thread->params);
}

void dev_thread_start_playback(struct dev_thread *thread,
			       struct alsa_conformance_recorder *recorder)
{
	snd_pcm_uframes_t buffer_size;
	snd_pcm_uframes_t period_size;
	snd_pcm_uframes_t block_size;
	snd_pcm_uframes_t frames_to_write;
	snd_pcm_sframes_t frames_avail;
        snd_pcm_sframes_t frames_delay;
	snd_pcm_sframes_t frames_written;
	snd_pcm_sframes_t frames_left;
	snd_pcm_sframes_t frames_played;
	snd_pcm_sframes_t frames_diff;
	snd_pcm_t *handle;
	struct timespec now;
	struct timespec ori;
	struct timespec relative_ts;
	struct alsa_conformance_timer *timer;
	uint8_t *buf;

	/* These variables are for debug usage. */
	char *time_str;
	struct timespec prev;
	struct timespec time_diff;
	double rate;
	int merged = 0;

	handle = thread->handle;
	timer = thread->timer;
	block_size = (snd_pcm_uframes_t)thread->block_size;

	if (alsa_helper_prepare(timer, handle) < 0)
		exit(EXIT_FAILURE);

	/* Get device buffer size. */
	snd_pcm_get_params(handle, &buffer_size, &period_size);

	/* Check block_size is available. */
	if (block_size == 0 || block_size > buffer_size / 2) {
		fprintf(stderr,
			"Block size %lu and buffer size %lu is not supported\n",
			block_size, buffer_size);
		exit(EXIT_FAILURE);
	}

	/* We need to allocate a zero buffer which will be written into device. */
	buf = (uint8_t *)calloc(block_size * 2,
				snd_pcm_format_physical_width(thread->format) /
					8 * thread->channels);
	if (!buf) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	/* Calculate how many frames we need to write by duration * rate. */
	frames_to_write = (snd_pcm_uframes_t)round(thread->duration *
						   (double)thread->rate);

	/* First, we write 2 blocks into buffer. */
	alsa_helper_write(handle, buf, 2 * block_size);
	frames_written = 2 * block_size;
	frames_played = 0;

	alsa_helper_start(timer, handle);

	/* Get the timestamp of beginning. */
	clock_gettime(CLOCK_MONOTONIC_RAW, &ori);

	if (DEBUG_MODE) {
		prev = ori;
		logger("%-13s %10s %10s %10s %18s %10s\n", "TIME_DIFF(s)",
		       "HW_LEVEL", "PLAYED", "DIFF", "RATE", "DELAY");
	}

	/*
     * Get available frames without sleep. It's more accurate but consume
     * more cpu time. Maybe we can choose other method in order to handle
     * multithread test in the future.
     */
	while (1) {
		if (alsa_helper_avail_delay(timer, handle, &frames_avail, &frames_delay)) {
			exit(EXIT_FAILURE);
		}

		frames_left = buffer_size - frames_avail;

		/*
         * Add a point into recorder when number of frames been played changes.
         */
		if (frames_played != frames_written - frames_left) {
			frames_diff =
				frames_written - frames_left - frames_played;
			frames_played = frames_written - frames_left;
			clock_gettime(CLOCK_MONOTONIC_RAW, &now);
			relative_ts = now;
			subtract_timespec(&relative_ts, &ori);
			merged = recorder_add(recorder, relative_ts,
					      frames_played);
			/* In debug mode, print each point in details. */
			if (DEBUG_MODE) {
				time_diff = now;
				subtract_timespec(&time_diff, &prev);
				time_str = timespec_to_str(&time_diff);
				rate = (double)frames_diff /
				       timespec_to_s(&time_diff);
				if (!merged) {
					logger("%-13s %10ld %10ld %10ld %18lf %10ld\n",
					       time_str, frames_left,
					       frames_played, frames_diff,
					       rate, frames_delay);
				} else {
					logger("%-13s %10ld %10ld %10ld %18lf %10ld [Merged]\n",
					       time_str, frames_left,
					       frames_played, frames_diff,
					       rate, frames_delay);
				}
				free(time_str);
				prev = now;
			}
		}

		/*
         * Because time of writing frames into buffer is much smaller than
         * time interval of device consuming frames, we can write frames here
         * without affecting result.
         */
		if (frames_left <= (long)block_size) {
			if (frames_written >= frames_to_write)
				break;
			if (frames_left < 0)
				thread->underrun_count++;
			if (alsa_helper_write(handle, buf, block_size) < 0)
				exit(EXIT_FAILURE);
			frames_written += block_size;
		}
	}
	alsa_helper_drop(handle);
	free(buf);
}

/* Returns whether all samples in the buffer are zeros. */
static void update_zero_channels(struct dev_thread *thread, uint8_t *buf,
				 int len)
{
	int width = snd_pcm_format_physical_width(thread->format) / 8;
	int i, j, c;

	for (c = 0; c < thread->channels; c++) {
		if (!thread->zero_channels[c])
			continue;
		for (i = c * width; i < len; i += width * thread->channels) {
			for (j = 0; j < width; j++) {
				if (buf[i + j])
					thread->zero_channels[c] = false;
			}
		}
	}
}

void dev_thread_start_capture(struct dev_thread *thread,
			      struct alsa_conformance_recorder *recorder)
{
	snd_pcm_uframes_t block_size;
	snd_pcm_uframes_t buffer_size;
	snd_pcm_uframes_t period_size;
	snd_pcm_uframes_t frames_to_read;
	snd_pcm_sframes_t frames_avail;
	snd_pcm_sframes_t frames_diff;
	snd_pcm_sframes_t frames_read;
	snd_pcm_sframes_t old_frames_avail;
	snd_pcm_t *handle;
	struct timespec now;
	struct timespec ori;
	struct timespec relative_ts;
	struct alsa_conformance_timer *timer;
	uint8_t *buf;

	/* These variables are for debug usage. */
	char *time_str;
	struct timespec prev;
	struct timespec time_diff;
	double rate;
	int merged = 0;

	handle = thread->handle;
	timer = thread->timer;
	block_size = (snd_pcm_uframes_t)thread->block_size;
	if (alsa_helper_prepare(timer, handle) < 0)
		exit(EXIT_FAILURE);

	/* Get device buffer size. */
	snd_pcm_get_params(handle, &buffer_size, &period_size);

	/* Check block_size is available. */
	if (block_size == 0 || block_size > buffer_size) {
		fprintf(stderr,
			"Block size %lu and buffer size %lu is not supported\n",
			block_size, buffer_size);
		exit(EXIT_FAILURE);
	}

	/* We need to allocate buffer which will save data from device. */
	buf = (uint8_t *)calloc(buffer_size,
				snd_pcm_format_physical_width(thread->format) /
					8 * thread->channels);
	if (!buf) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	/* Calculate how many frames we need to read by duration * rate. */
	frames_to_read = (snd_pcm_uframes_t)round(thread->duration *
						  (double)thread->rate);

	frames_read = 0;
	old_frames_avail = 0;

	alsa_helper_start(timer, handle);

	/* Get the timestamp of beginning. */
	clock_gettime(CLOCK_MONOTONIC_RAW, &ori);

	if (DEBUG_MODE) {
		prev = ori;
		logger("%-13s %10s %10s%18s\n", "TIME_DIFF(s)", "HW_LEVEL",
		       "READ", "RATE");
	}

	while (frames_read < frames_to_read) {
		frames_avail = alsa_helper_avail(timer, handle);

		/* Check overrun. */
		if (frames_avail > buffer_size)
			thread->overrun_count++;

		if (frames_avail != old_frames_avail) {
			frames_diff = frames_avail - old_frames_avail;
			old_frames_avail = frames_avail;
			clock_gettime(CLOCK_MONOTONIC_RAW, &now);
			relative_ts = now;
			subtract_timespec(&relative_ts, &ori);
			merged = recorder_add(recorder, relative_ts,
					      frames_read + frames_avail);
			/* Read blocks if there are enough frames in a device. */
			while (old_frames_avail >= block_size) {
				if (alsa_helper_read(handle, buf, block_size) <
				    0)
					exit(EXIT_FAILURE);
				update_zero_channels(thread, buf, block_size);
				frames_read += block_size;
				old_frames_avail -= block_size;
			}

			if (DEBUG_MODE) {
				time_diff = now;
				subtract_timespec(&time_diff, &prev);
				time_str = timespec_to_str(&time_diff);
				rate = (double)frames_diff /
				       timespec_to_s(&time_diff);
				if (!merged) {
					logger("%-13s %10ld %10ld %18lf\n",
					       time_str, frames_avail,
					       frames_read, rate);
				} else {
					logger("%-13s %10ld %10ld %18lf [Merged]\n",
					       time_str, frames_avail,
					       frames_read, rate);
				}
				free(time_str);
				prev = now;
			}
		}
	}
	alsa_helper_drop(handle);
	free(buf);
}

/* Start device thread for playback or capture. */
struct alsa_conformance_recorder *dev_thread_run_once(struct dev_thread *thread,
						      int dryrun)
{
	struct alsa_conformance_recorder *recorder;
	recorder = recorder_create(thread->merge_threshold_t,
				   thread->merge_threshold_sz);
	if (thread->stream == SND_PCM_STREAM_PLAYBACK)
		dev_thread_start_playback(thread, recorder);
	else if (thread->stream == SND_PCM_STREAM_CAPTURE)
		dev_thread_start_capture(thread, recorder);

	if (!dryrun)
		recorder_list_add_recorder(thread->recorder_list, recorder);
	return recorder;
}

struct alsa_conformance_recorder *
dev_thread_run_one_iteration(struct dev_thread *thread, int dryrun)
{
	struct alsa_conformance_recorder *recorder = NULL;
	dev_thread_open_device(thread);
	dev_thread_set_params(thread);
	/* If duration is zero, it won't run playback or capture. */
	if (thread->duration)
		recorder = dev_thread_run_once(thread, dryrun);
	dev_thread_close_device(thread);
	return recorder;
}

/* Dry run the test to get step_median */
void dev_thread_set_merge_threshold_sz(struct dev_thread *thread)
{
	int old_debug_mode = DEBUG_MODE;
	double old_merge_threshold_t = thread->merge_threshold_t;

	/* Skip when the merge_threshold_t is 0. */
	if (!thread->merge_threshold_t) {
		thread->merge_threshold_sz = 0;
		return;
	}
	/* Skip if user set */
	if (thread->merge_threshold_sz)
		return;

	/* Skip if the duration is 0. */
	if (!thread->duration)
		return;

	DEBUG_MODE = 0;
	thread->merge_threshold_t = 0;
	struct alsa_conformance_recorder *recorder =
		dev_thread_run_one_iteration(thread, 1);

	recorder_compute_step_median(recorder);
	thread->merge_threshold_sz = get_step_median(recorder);
	recorder_destroy(recorder);
	DEBUG_MODE = old_debug_mode;
	thread->merge_threshold_t = old_merge_threshold_t;
}

void *dev_thread_run_iterations(void *arg)
{
	struct dev_thread *thread = arg;
	int i;

	dev_thread_set_merge_threshold_sz(thread);
	for (i = 0; i < thread->iterations; i++) {
		if (SINGLE_THREAD && thread->iterations != 1)
			printf("Run %d iteration...\n", i + 1);
		dev_thread_run_one_iteration(thread, 0);
	}
	return 0;
}

void dev_thread_print_device_information(struct dev_thread *thread)
{
	int rc;
	assert(thread->handle);
	rc = print_device_information(thread->handle, thread->params);
	if (rc < 0)
		exit(EXIT_FAILURE);
}

void dev_thread_print_params(struct dev_thread *thread)
{
	int rc;
	assert(thread->params_record);
	printf("PCM name: %s\n", thread->dev_name);
	print_card_information(thread->pcm_info, thread->card_info);
	printf("stream: %s\n", snd_pcm_stream_name(thread->stream));
	printf("merge_threshold_t: %lf\n", thread->merge_threshold_t);
	printf("merge_threshold_sz: %ld\n", thread->merge_threshold_sz);
	rc = print_params(thread->params_record);
	if (rc < 0)
		exit(EXIT_FAILURE);
}

void dev_thread_print_result(struct dev_thread *thread)
{
	int i;
	if (thread->params_record == NULL) {
		puts("No data.");
		return;
	}

	puts("---------PRINT PARAMS---------");
	dev_thread_print_params(thread);

	puts("---------TIMER RESULT---------");
	conformance_timer_print_result(thread->timer);

	if (thread->duration == 0)
		return;

	puts("----------RUN RESULT----------");
	recorder_list_print_result(thread->recorder_list);

	if (thread->stream == SND_PCM_STREAM_CAPTURE) {
		printf("zero channels:");
		for (i = 0; i < thread->channels; i++)
			printf(" %d", thread->zero_channels[i]);
		puts("");
	}

	printf("number of underrun: %u\n", thread->underrun_count);
	printf("number of overrun: %u\n", thread->overrun_count);
}
