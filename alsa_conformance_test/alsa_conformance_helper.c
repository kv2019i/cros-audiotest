/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include <alsa/asoundlib.h>
#include <alsa/pcm.h>

#include "include/utlist.h"

#include "alsa_conformance_helper.h"
#include "alsa_conformance_timer.h"
#include "alsa_conformance_mixer.h"

void print_card_information(snd_pcm_info_t *pcm_info,
			    snd_ctl_card_info_t *card_info)
{
	printf("card: %s [%s]\n", snd_ctl_card_info_get_id(card_info),
	       snd_ctl_card_info_get_name(card_info));
	printf("device: %s [%s]\n", snd_pcm_info_get_id(pcm_info),
	       snd_pcm_info_get_name(pcm_info));
}

int alsa_helper_get_card_info(snd_pcm_t *handle, snd_pcm_info_t *pcm_info,
			      snd_ctl_card_info_t *card_info)
{
	int card_idx;
	char name[32];
	snd_ctl_t *ctl_handle = NULL;
	int rc;

	assert(pcm_info);
	assert(card_info);
	rc = snd_pcm_info(handle, pcm_info);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_info: %s\n", snd_strerror(rc));
		return rc;
	}

	card_idx = snd_pcm_info_get_card(pcm_info);
	sprintf(name, "hw:%d", card_idx);

	rc = snd_ctl_open(&ctl_handle, name, 0);
	if (rc < 0) {
		fprintf(stderr, "snd_ctl_open: %s\n", snd_strerror(rc));
		return rc;
	}

	rc = snd_ctl_card_info(ctl_handle, card_info);
	if (rc < 0) {
		fprintf(stderr, "snd_ctl_card_info: %s\n", snd_strerror(rc));
		return rc;
	}

	return 0;
}

int print_usb_mixer_information(snd_pcm_t *handle, snd_pcm_hw_params_t *params,
				const char *card_name)
{
	struct alsa_mixer *amixer;
	struct alsa_mixer_control *c;

	assert(handle);
	assert(params);
	assert(card_name);

	amixer = alsa_usb_mixer_create(card_name);
	assert(amixer);
	if (snd_pcm_stream(handle) == SND_PCM_STREAM_PLAYBACK) {
		DL_FOREACH (amixer->output_controls, c) {
			printf("mixer: name:%s index:%d has_volume:%d db_range:[%ld, %ld]"
			       " volume_range:[%ld, %ld]\n",
			       c->name, c->index, c->has_volume,
			       c->min_volume_dB, c->max_volume_dB,
			       c->volume_range_min, c->volume_range_max);
		}
	} else if (snd_pcm_stream(handle) == SND_PCM_STREAM_CAPTURE) {
		DL_FOREACH (amixer->input_controls, c) {
			printf("mixer: name:%s index:%d has_volume:%d db_range:[%ld, %ld]"
			       " volume_range:[%ld, %ld]\n",
			       c->name, c->index, c->has_volume,
			       c->min_volume_dB, c->max_volume_dB,
			       c->volume_range_min, c->volume_range_max);
		}
	}
	alsa_usb_mixer_control_destroy(amixer);
	return 0;
}

int print_device_information(snd_pcm_t *handle, snd_pcm_hw_params_t *params)
{
	unsigned int min;
	unsigned int max;
	unsigned int i;
	/* The min2 and max2 are for period size and buffer size. The type of
	 * snd_pcm_uframes_t is unsigned long.*/
	snd_pcm_uframes_t min2;
	snd_pcm_uframes_t max2;
	int dir;
	int rc = 0;
	char card_name[32];

	snd_pcm_info_t *pcm_info;
	snd_ctl_card_info_t *card_info;

	printf("PCM handle name: %s\n", snd_pcm_name(handle));

	printf("PCM type: %s\n", snd_pcm_type_name(snd_pcm_type(handle)));

	snd_pcm_info_malloc(&pcm_info);
	snd_ctl_card_info_malloc(&card_info);
	alsa_helper_get_card_info(handle, pcm_info, card_info);
	print_card_information(pcm_info, card_info);

	printf("stream: %s\n", snd_pcm_stream_name(snd_pcm_stream(handle)));

	rc = snd_pcm_hw_params_get_channels_min(params, &min);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_channels_min: %s\n",
			snd_strerror(rc));
		goto clean_up;
	}

	rc = snd_pcm_hw_params_get_channels_max(params, &max);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_channels_max: %s\n",
			snd_strerror(rc));
		goto clean_up;
	}

	printf("available channels:");
	for (i = min; i <= max; i++) {
		rc = snd_pcm_hw_params_test_channels(handle, params, i);
		if (rc == 0)
			printf(" %d", i);
	}
	puts("");

	printf("available formats:");
	for (i = 0; i < SND_PCM_FORMAT_LAST; i++) {
		rc = snd_pcm_hw_params_test_format(handle, params,
						   (snd_pcm_format_t)i);
		if (rc == 0)
			printf(" %s", snd_pcm_format_name((snd_pcm_format_t)i));
	}
	puts("");

	rc = snd_pcm_hw_params_get_rate_min(params, &min, &dir);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_rate_min: %s\n",
			snd_strerror(rc));
		goto clean_up;
	}

	rc = snd_pcm_hw_params_get_rate_max(params, &max, &dir);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_rate_max: %s\n",
			snd_strerror(rc));
		goto clean_up;
	}

	printf("rate range: [%u, %u]\n", min, max);

	printf("available rates:");
	for (i = min; i <= max; i++) {
		rc = snd_pcm_hw_params_test_rate(handle, params, i, 0);
		if (rc == 0)
			printf(" %d", i);
	}
	puts("");

	rc = snd_pcm_hw_params_get_period_size_min(params, &min2, &dir);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_period_size_min: %s\n",
			snd_strerror(rc));
		goto clean_up;
	}

	rc = snd_pcm_hw_params_get_period_size_max(params, &max2, &dir);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_period_size_max: %s\n",
			snd_strerror(rc));
		goto clean_up;
	}

	printf("period size range: [%lu, %lu]\n", min2, max2);

	rc = snd_pcm_hw_params_get_buffer_size_min(params, &min2);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_buffer_size_min: %s\n",
			snd_strerror(rc));
		goto clean_up;
	}

	rc = snd_pcm_hw_params_get_buffer_size_max(params, &max2);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_buffer_size_max: %s\n",
			snd_strerror(rc));
		goto clean_up;
	}

	printf("buffer size range: [%lu, %lu]\n", min2, max2);
	// print usb mixer if the device is USB audio
	if (!strcmp(snd_pcm_info_get_id(pcm_info), "USB Audio")) {
		sprintf(card_name, "hw:%d", snd_pcm_info_get_card(pcm_info));
		rc = print_usb_mixer_information(handle, params, card_name);
		if (rc < 0) {
			goto clean_up;
		}
	}

clean_up:
	snd_ctl_card_info_free(card_info);
	snd_pcm_info_free(pcm_info);
	return rc;
}

int print_params(snd_pcm_hw_params_t *params)
{
	unsigned int val;
	snd_pcm_uframes_t frames;
	int rc;
	int dir;

	rc = snd_pcm_hw_params_get_access(params, (snd_pcm_access_t *)&val);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_access: %s\n", snd_strerror(rc));
		return rc;
	}
	printf("access type: %s\n", snd_pcm_access_name((snd_pcm_access_t)val));

	rc = snd_pcm_hw_params_get_format(params, (snd_pcm_format_t *)&val);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_format: %s\n", snd_strerror(rc));
		return rc;
	}
	printf("format: %s\n", snd_pcm_format_name((snd_pcm_format_t)val));

	rc = snd_pcm_hw_params_get_channels(params, &val);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_channels: %s\n",
			snd_strerror(rc));
		return rc;
	}
	printf("channels: %d\n", val);

	rc = snd_pcm_hw_params_get_rate(params, &val, &dir);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_rate: %s\n", snd_strerror(rc));
		return rc;
	}
	printf("rate: %d fps\n", val);

	rc = snd_pcm_hw_params_get_period_time(params, &val, &dir);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_period_time: %s\n",
			snd_strerror(rc));
		return rc;
	}
	printf("period time: %d us\n", val);

	rc = snd_pcm_hw_params_get_period_size(params, &frames, &dir);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_period_size: %s\n",
			snd_strerror(rc));
		return rc;
	}
	printf("period size: %lu frames\n", frames);

	rc = snd_pcm_hw_params_get_buffer_time(params, &val, &dir);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_buffer_time: %s\n",
			snd_strerror(rc));
		return rc;
	}
	printf("buffer time: %d us\n", val);

	rc = snd_pcm_hw_params_get_buffer_size(params, &frames);
	if (rc < 0) {
		fprintf(stderr, "hw_params_get_buffer_time: %s\n",
			snd_strerror(rc));
		return rc;
	}
	printf("buffer size: %lu frames\n", frames);
	return 0;
}

int alsa_helper_open(struct alsa_conformance_timer *timer, snd_pcm_t **handle,
		     snd_pcm_hw_params_t **params, const char *dev_name,
		     snd_pcm_stream_t stream)
{
	int rc;
	conformance_timer_start(timer, SND_PCM_OPEN);
	rc = snd_pcm_open(handle, dev_name, stream,
			  SND_PCM_NONBLOCK | SND_PCM_NO_AUTO_RESAMPLE |
				  SND_PCM_NO_AUTO_CHANNELS |
				  SND_PCM_NO_AUTO_FORMAT);
	conformance_timer_stop(timer, SND_PCM_OPEN);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_open %s: %s\n", dev_name,
			snd_strerror(rc));
		return rc;
	}

	/* malloc params object*/
	rc = snd_pcm_hw_params_malloc(params);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_hw_params_malloc: %s\n",
			snd_strerror(rc));
		return rc;
	}

	/* set default value */
	conformance_timer_start(timer, SND_PCM_HW_PARAMS_ANY);
	rc = snd_pcm_hw_params_any(*handle, *params);
	conformance_timer_stop(timer, SND_PCM_HW_PARAMS_ANY);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_hw_params_any: %s\n",
			snd_strerror(rc));
		return rc;
	}

	return 0;
}

int alsa_helper_close(snd_pcm_t *handle)
{
	int rc = snd_pcm_close(handle);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_close: %s\n", snd_strerror(rc));
		return rc;
	}
	return 0;
}

int alsa_helper_set_hw_params(struct alsa_conformance_timer *timer,
			      snd_pcm_t *handle, snd_pcm_hw_params_t *params,
			      snd_pcm_format_t format, unsigned int channels,
			      unsigned int *rate,
			      snd_pcm_uframes_t *period_size)
{
	int dir = 0;
	int rc;

	/* Disable hardware resampling. */
	rc = snd_pcm_hw_params_set_rate_resample(handle, params, 0);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_rate_resample: %s\n",
			snd_strerror(rc));
		return rc;
	}

	/* Always interleaved. */
	rc = snd_pcm_hw_params_set_access(handle, params,
					  SND_PCM_ACCESS_MMAP_INTERLEAVED);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_access: %s\n",
			snd_strerror(rc));
		return rc;
	}

	rc = snd_pcm_hw_params_set_format(handle, params, format);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_format %s: %s\n",
			snd_pcm_format_name(format), snd_strerror(rc));
		return rc;
	}

	rc = snd_pcm_hw_params_set_channels(handle, params, channels);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_channels %u: %s\n",
			channels, snd_strerror(rc));
		return rc;
	}

	/* Set rate near, the rate may be changed if it's not supported */
	rc = snd_pcm_hw_params_set_rate_near(handle, params, rate, &dir);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_rate_near %u: %s\n",
			*rate, snd_strerror(rc));
		return rc;
	}

	/* If the period size is not zero, set period size near. The period size may
	 * be changed if it's not supported. */
	if (*period_size) {
		rc = snd_pcm_hw_params_set_period_size_near(handle, params,
							    period_size, &dir);
		if (rc < 0) {
			fprintf(stderr,
				"snd_pcm_hw_params_set_period_size_near %lu: %s\n",
				*period_size, snd_strerror(rc));
			return rc;
		}
	}

	/* TODO(yuhsuan): We should support setting buffer_size in the future.
	 * It's set automatically now.*/

	conformance_timer_start(timer, SND_PCM_HW_PARAMS);
	rc = snd_pcm_hw_params(handle, params);
	conformance_timer_stop(timer, SND_PCM_HW_PARAMS);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_hw_params: %s\n", snd_strerror(rc));
		return rc;
	}

	return 0;
}

int alsa_helper_set_sw_param(struct alsa_conformance_timer *timer,
			     snd_pcm_t *handle)
{
	snd_pcm_sw_params_t *swparams;
	snd_pcm_uframes_t boundary;
	int rc;

	snd_pcm_sw_params_alloca(&swparams);

	rc = snd_pcm_sw_params_current(handle, swparams);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_sw_params_current: %s\n",
			snd_strerror(rc));
		return rc;
	}

	rc = snd_pcm_sw_params_get_boundary(swparams, &boundary);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_sw_params_get_boundary: %s\n",
			snd_strerror(rc));
		return rc;
	}

	/* Don't stop automatically. */
	rc = snd_pcm_sw_params_set_stop_threshold(handle, swparams, boundary);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_sw_params_set_stop_threshold: %s\n",
			snd_strerror(rc));
		return rc;
	}

	/* Don't start automatically. */
	rc = snd_pcm_sw_params_set_start_threshold(handle, swparams, boundary);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_sw_params_set_start_threshold: %s\n",
			snd_strerror(rc));
		return rc;
	}

	/* Disable period events. */
	rc = snd_pcm_sw_params_set_period_event(handle, swparams, 0);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_sw_params_set_period_event: %s\n",
			snd_strerror(rc));
		return rc;
	}

	conformance_timer_start(timer, SND_PCM_SW_PARAMS);
	rc = snd_pcm_sw_params(handle, swparams);
	conformance_timer_stop(timer, SND_PCM_SW_PARAMS);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_sw_params: %s\n", snd_strerror(rc));
		return rc;
	}

	return 0;
}

int alsa_helper_prepare(struct alsa_conformance_timer *timer, snd_pcm_t *handle)
{
	int rc;
	conformance_timer_start(timer, SND_PCM_PREPARE);
	rc = snd_pcm_prepare(handle);
	conformance_timer_stop(timer, SND_PCM_PREPARE);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_prepare: %s\n", snd_strerror(rc));
		return rc;
	}
	return 0;
}

int alsa_helper_start(struct alsa_conformance_timer *timer, snd_pcm_t *handle)
{
	int rc;
	conformance_timer_start(timer, SND_PCM_START);
	rc = snd_pcm_start(handle);
	conformance_timer_stop(timer, SND_PCM_START);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_start: %s\n", snd_strerror(rc));
		return rc;
	}

	return 0;
}

int alsa_helper_drop(snd_pcm_t *handle)
{
	int rc;
	rc = snd_pcm_drop(handle);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_drop: %s\n", snd_strerror(rc));
		return rc;
	}

	return 0;
}

snd_pcm_sframes_t alsa_helper_avail(struct alsa_conformance_timer *timer,
				    snd_pcm_t *handle)
{
	snd_pcm_sframes_t rc;

	conformance_timer_start(timer, SND_PCM_AVAIL);
	rc = snd_pcm_avail(handle);
	conformance_timer_stop(timer, SND_PCM_AVAIL);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_avail: %s\n", snd_strerror(rc));
		return rc;
	}

	return rc;
}

int alsa_helper_avail_delay(struct alsa_conformance_timer *timer, snd_pcm_t *handle,
			    snd_pcm_sframes_t *availp, snd_pcm_sframes_t *delayp)
{
	int rc;

	conformance_timer_start(timer, SND_PCM_AVAIL_DELAY);
	rc = snd_pcm_avail_delay(handle, availp, delayp);
	conformance_timer_stop(timer, SND_PCM_AVAIL_DELAY);
	if (rc < 0) {
		fprintf(stderr, "snd_pcm_avail_delay: %s\n", snd_strerror(rc));
		return rc;
	}

	return rc;
}

int alsa_helper_write(snd_pcm_t *handle, uint8_t *buf, snd_pcm_uframes_t size)
{
	const snd_pcm_channel_area_t *my_areas;
	snd_pcm_uframes_t frames, offset;
	uint8_t *dst;
	int frame_bytes;
	int rc;

	while (size > 0) {
		frames = size;
		rc = snd_pcm_mmap_begin(handle, &my_areas, &offset, &frames);
		if (rc < 0) {
			fprintf(stderr, "snd_pcm_mmap_begin: %s\n",
				snd_strerror(rc));
			return rc;
		}
		frame_bytes = my_areas[0].step / 8;
		dst = (uint8_t *)my_areas[0].addr + offset * frame_bytes;
		memcpy(dst, buf, frames * frame_bytes);
		rc = snd_pcm_mmap_commit(handle, offset, frames);
		if (rc < 0) {
			fprintf(stderr, "snd_pcm_mmap_commit: %s\n",
				snd_strerror(rc));
			return rc;
		}
		size -= frames;
		buf += frames * frame_bytes;
	}
	return 0;
}

int alsa_helper_read(snd_pcm_t *handle, uint8_t *buf, snd_pcm_uframes_t size)
{
	const snd_pcm_channel_area_t *my_areas;
	snd_pcm_uframes_t frames, offset;
	uint8_t *dst;
	int frame_bytes;
	int rc;

	while (size > 0) {
		frames = size;
		rc = snd_pcm_mmap_begin(handle, &my_areas, &offset, &frames);
		if (rc < 0) {
			fprintf(stderr, "snd_pcm_mmap_begin: %s\n",
				snd_strerror(rc));
			return rc;
		}
		frame_bytes = my_areas[0].step / 8;
		dst = (uint8_t *)my_areas[0].addr + offset * frame_bytes;
		memcpy(buf, dst, frames * frame_bytes);
		rc = snd_pcm_mmap_commit(handle, offset, frames);
		if (rc < 0) {
			fprintf(stderr, "snd_pcm_mmap_commit: %s\n",
				snd_strerror(rc));
			return rc;
		}
		size -= frames;
		buf += frames * frame_bytes;
	}
	return 0;
}
