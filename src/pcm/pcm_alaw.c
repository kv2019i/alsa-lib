/*
 *  PCM - A-Law conversion
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
  
#include <byteswap.h>
#include "pcm_local.h"
#include "pcm_plugin.h"

typedef void (*alaw_f)(const snd_pcm_channel_area_t *dst_areas,
		       snd_pcm_uframes_t dst_offset,
		       const snd_pcm_channel_area_t *src_areas,
		       snd_pcm_uframes_t src_offset,
		       unsigned int channels, snd_pcm_uframes_t frames, int getputidx);

typedef struct {
	/* This field need to be the first */
	snd_pcm_plugin_t plug;
	int getput_idx;
	alaw_f func;
	snd_pcm_format_t sformat;
} snd_pcm_alaw_t;

static inline int val_seg(int val)
{
	int r = 1;
	val >>= 8;
	if (val & 0xf0) {
		val >>= 4;
		r += 4;
	}
	if (val & 0x0c) {
		val >>= 2;
		r += 2;
	}
	if (val & 0x02)
		r += 1;
	return r;
}

/*
 * s16_to_alaw() - Convert a 16-bit linear PCM value to 8-bit A-law
 *
 * s16_to_alaw() accepts an 16-bit integer and encodes it as A-law data.
 *
 *		Linear Input Code	Compressed Code
 *	------------------------	---------------
 *	0000000wxyza			000wxyz
 *	0000001wxyza			001wxyz
 *	000001wxyzab			010wxyz
 *	00001wxyzabc			011wxyz
 *	0001wxyzabcd			100wxyz
 *	001wxyzabcde			101wxyz
 *	01wxyzabcdef			110wxyz
 *	1wxyzabcdefg			111wxyz
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */

static unsigned char s16_to_alaw(int pcm_val)
{
	int		mask;
	int		seg;
	unsigned char	aval;

	if (pcm_val >= 0) {
		mask = 0xD5;
	} else {
		mask = 0x55;
		pcm_val = -pcm_val;
		if (pcm_val > 0x7fff)
			pcm_val = 0x7fff;
	}

	if (pcm_val < 256)
		aval = pcm_val >> 4;
	else {
		/* Convert the scaled magnitude to segment number. */
		seg = val_seg(pcm_val);
		aval = (seg << 4) | ((pcm_val >> (seg + 3)) & 0x0f);
	}
	return aval ^ mask;
}

/*
 * alaw_to_s16() - Convert an A-law value to 16-bit linear PCM
 *
 */
static int alaw_to_s16(unsigned char a_val)
{
	int		t;
	int		seg;

	a_val ^= 0x55;
	t = a_val & 0x7f;
	if (t < 16)
		t = (t << 4) + 8;
	else {
		seg = (t >> 4) & 0x07;
		t = ((t & 0x0f) << 4) + 0x108;
		t <<= seg -1;
	}
	return ((a_val & 0x80) ? t : -t);
}

void snd_pcm_alaw_decode(const snd_pcm_channel_area_t *dst_areas,
			 snd_pcm_uframes_t dst_offset,
			 const snd_pcm_channel_area_t *src_areas,
			 snd_pcm_uframes_t src_offset,
			 unsigned int channels, snd_pcm_uframes_t frames, int putidx)
{
#define PUT16_LABELS
#include "plugin_ops.h"
#undef PUT16_LABELS
	void *put = put16_labels[putidx];
	unsigned int channel;
	for (channel = 0; channel < channels; ++channel) {
		const char *src;
		char *dst;
		int src_step, dst_step;
		snd_pcm_uframes_t frames1;
		const snd_pcm_channel_area_t *src_area = &src_areas[channel];
		const snd_pcm_channel_area_t *dst_area = &dst_areas[channel];
		src = snd_pcm_channel_area_addr(src_area, src_offset);
		dst = snd_pcm_channel_area_addr(dst_area, dst_offset);
		src_step = snd_pcm_channel_area_step(src_area);
		dst_step = snd_pcm_channel_area_step(dst_area);
		frames1 = frames;
		while (frames1-- > 0) {
			int16_t sample = alaw_to_s16(*src);
			goto *put;
#define PUT16_END after
#include "plugin_ops.h"
#undef PUT16_END
		after:
			src += src_step;
			dst += dst_step;
		}
	}
}

void snd_pcm_alaw_encode(const snd_pcm_channel_area_t *dst_areas,
			 snd_pcm_uframes_t dst_offset,
			 const snd_pcm_channel_area_t *src_areas,
			 snd_pcm_uframes_t src_offset,
			 unsigned int channels, snd_pcm_uframes_t frames, int getidx)
{
#define GET16_LABELS
#include "plugin_ops.h"
#undef GET16_LABELS
	void *get = get16_labels[getidx];
	unsigned int channel;
	int16_t sample = 0;
	for (channel = 0; channel < channels; ++channel) {
		const char *src;
		char *dst;
		int src_step, dst_step;
		snd_pcm_uframes_t frames1;
		const snd_pcm_channel_area_t *src_area = &src_areas[channel];
		const snd_pcm_channel_area_t *dst_area = &dst_areas[channel];
		src = snd_pcm_channel_area_addr(src_area, src_offset);
		dst = snd_pcm_channel_area_addr(dst_area, dst_offset);
		src_step = snd_pcm_channel_area_step(src_area);
		dst_step = snd_pcm_channel_area_step(dst_area);
		frames1 = frames;
		while (frames1-- > 0) {
			goto *get;
#define GET16_END after
#include "plugin_ops.h"
#undef GET16_END
		after:
			*dst = s16_to_alaw(sample);
			src += src_step;
			dst += dst_step;
		}
	}
}

static int snd_pcm_alaw_hw_refine_cprepare(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	snd_pcm_alaw_t *alaw = pcm->private_data;
	int err;
	snd_pcm_access_mask_t access_mask = { SND_PCM_ACCBIT_SHM };
	err = _snd_pcm_hw_param_set_mask(params, SND_PCM_HW_PARAM_ACCESS,
					 &access_mask);
	if (err < 0)
		return err;
	if (alaw->sformat == SND_PCM_FORMAT_A_LAW) {
		snd_pcm_format_mask_t format_mask = { SND_PCM_FMTBIT_LINEAR };
		err = _snd_pcm_hw_param_set_mask(params, SND_PCM_HW_PARAM_FORMAT,
						 &format_mask);
	} else {
		err = _snd_pcm_hw_params_set_format(params, 
						   SND_PCM_FORMAT_A_LAW);
	}
	if (err < 0)
		return err;
	err = _snd_pcm_hw_params_set_subformat(params, SND_PCM_SUBFORMAT_STD);
	if (err < 0)
		return err;
	params->info &= ~(SND_PCM_INFO_MMAP | SND_PCM_INFO_MMAP_VALID);
	return 0;
}

static int snd_pcm_alaw_hw_refine_sprepare(snd_pcm_t *pcm, snd_pcm_hw_params_t *sparams)
{
	snd_pcm_alaw_t *alaw = pcm->private_data;
	snd_pcm_access_mask_t saccess_mask = { SND_PCM_ACCBIT_MMAP };
	_snd_pcm_hw_params_any(sparams);
	_snd_pcm_hw_param_set_mask(sparams, SND_PCM_HW_PARAM_ACCESS,
				   &saccess_mask);
	_snd_pcm_hw_params_set_format(sparams, alaw->sformat);
	_snd_pcm_hw_params_set_subformat(sparams, SND_PCM_SUBFORMAT_STD);
	return 0;
}

static int snd_pcm_alaw_hw_refine_schange(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params,
					    snd_pcm_hw_params_t *sparams)
{
	int err;
	unsigned int links = (SND_PCM_HW_PARBIT_CHANNELS |
			      SND_PCM_HW_PARBIT_RATE |
			      SND_PCM_HW_PARBIT_PERIOD_SIZE |
			      SND_PCM_HW_PARBIT_BUFFER_SIZE |
			      SND_PCM_HW_PARBIT_PERIODS |
			      SND_PCM_HW_PARBIT_PERIOD_TIME |
			      SND_PCM_HW_PARBIT_BUFFER_TIME |
			      SND_PCM_HW_PARBIT_TICK_TIME);
	err = _snd_pcm_hw_params_refine(sparams, links, params);
	if (err < 0)
		return err;
	return 0;
}
	
static int snd_pcm_alaw_hw_refine_cchange(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params,
					    snd_pcm_hw_params_t *sparams)
{
	int err;
	unsigned int links = (SND_PCM_HW_PARBIT_CHANNELS |
			      SND_PCM_HW_PARBIT_RATE |
			      SND_PCM_HW_PARBIT_PERIOD_SIZE |
			      SND_PCM_HW_PARBIT_BUFFER_SIZE |
			      SND_PCM_HW_PARBIT_PERIODS |
			      SND_PCM_HW_PARBIT_PERIOD_TIME |
			      SND_PCM_HW_PARBIT_BUFFER_TIME |
			      SND_PCM_HW_PARBIT_TICK_TIME);
	err = _snd_pcm_hw_params_refine(params, links, sparams);
	if (err < 0)
		return err;
	return 0;
}

static int snd_pcm_alaw_hw_refine(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	return snd_pcm_hw_refine_slave(pcm, params,
				       snd_pcm_alaw_hw_refine_cprepare,
				       snd_pcm_alaw_hw_refine_cchange,
				       snd_pcm_alaw_hw_refine_sprepare,
				       snd_pcm_alaw_hw_refine_schange,
				       snd_pcm_plugin_hw_refine_slave);
}

static int snd_pcm_alaw_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t * params)
{
	snd_pcm_alaw_t *alaw = pcm->private_data;
	int err = snd_pcm_hw_params_slave(pcm, params,
					  snd_pcm_alaw_hw_refine_cchange,
					  snd_pcm_alaw_hw_refine_sprepare,
					  snd_pcm_alaw_hw_refine_schange,
					  snd_pcm_plugin_hw_params_slave);
	if (err < 0)
		return err;

	if (pcm->stream == SND_PCM_STREAM_PLAYBACK) {
		if (alaw->sformat == SND_PCM_FORMAT_A_LAW) {
			alaw->getput_idx = snd_pcm_linear_get_index(snd_pcm_hw_params_get_format(params), SND_PCM_FORMAT_S16);
			alaw->func = snd_pcm_alaw_encode;
		} else {
			alaw->getput_idx = snd_pcm_linear_put_index(SND_PCM_FORMAT_S16, alaw->sformat);
			alaw->func = snd_pcm_alaw_decode;
		}
	} else {
		if (alaw->sformat == SND_PCM_FORMAT_A_LAW) {
			alaw->getput_idx = snd_pcm_linear_put_index(SND_PCM_FORMAT_S16, snd_pcm_hw_params_get_format(params));
			alaw->func = snd_pcm_alaw_decode;
		} else {
			alaw->getput_idx = snd_pcm_linear_get_index(alaw->sformat, SND_PCM_FORMAT_S16);
			alaw->func = snd_pcm_alaw_encode;
		}
	}
	return 0;
}

static snd_pcm_uframes_t
snd_pcm_alaw_write_areas(snd_pcm_t *pcm,
			 const snd_pcm_channel_area_t *areas,
			 snd_pcm_uframes_t offset,
			 snd_pcm_uframes_t size,
			 const snd_pcm_channel_area_t *slave_areas,
			 snd_pcm_uframes_t slave_offset,
			 snd_pcm_uframes_t *slave_sizep)
{
	snd_pcm_alaw_t *alaw = pcm->private_data;
	if (size > *slave_sizep)
		size = *slave_sizep;
	alaw->func(slave_areas, slave_offset,
		   areas, offset, 
		   pcm->channels, size,
		   alaw->getput_idx);
	*slave_sizep = size;
	return size;
}

static snd_pcm_uframes_t
snd_pcm_alaw_read_areas(snd_pcm_t *pcm,
			const snd_pcm_channel_area_t *areas,
			snd_pcm_uframes_t offset,
			snd_pcm_uframes_t size,
			const snd_pcm_channel_area_t *slave_areas,
			snd_pcm_uframes_t slave_offset,
			snd_pcm_uframes_t *slave_sizep)
{
	snd_pcm_alaw_t *alaw = pcm->private_data;
	if (size > *slave_sizep)
		size = *slave_sizep;
	alaw->func(areas, offset, 
		   slave_areas, slave_offset,
		   pcm->channels, size,
		   alaw->getput_idx);
	*slave_sizep = size;
	return size;
}

static void snd_pcm_alaw_dump(snd_pcm_t *pcm, snd_output_t *out)
{
	snd_pcm_alaw_t *alaw = pcm->private_data;
	snd_output_printf(out, "A-Law conversion PCM (%s)\n", 
		snd_pcm_format_name(alaw->sformat));
	if (pcm->setup) {
		snd_output_printf(out, "Its setup is:\n");
		snd_pcm_dump_setup(pcm, out);
	}
	snd_output_printf(out, "Slave: ");
	snd_pcm_dump(alaw->plug.slave, out);
}

snd_pcm_ops_t snd_pcm_alaw_ops = {
	close: snd_pcm_plugin_close,
	info: snd_pcm_plugin_info,
	hw_refine: snd_pcm_alaw_hw_refine,
	hw_params: snd_pcm_alaw_hw_params,
	hw_free: snd_pcm_plugin_hw_free,
	sw_params: snd_pcm_plugin_sw_params,
	channel_info: snd_pcm_plugin_channel_info,
	dump: snd_pcm_alaw_dump,
	nonblock: snd_pcm_plugin_nonblock,
	async: snd_pcm_plugin_async,
	mmap: snd_pcm_plugin_mmap,
	munmap: snd_pcm_plugin_munmap,
};

int snd_pcm_alaw_open(snd_pcm_t **pcmp, const char *name, snd_pcm_format_t sformat, snd_pcm_t *slave, int close_slave)
{
	snd_pcm_t *pcm;
	snd_pcm_alaw_t *alaw;
	assert(pcmp && slave);
	if (snd_pcm_format_linear(sformat) != 1 &&
	    sformat != SND_PCM_FORMAT_A_LAW)
		return -EINVAL;
	alaw = calloc(1, sizeof(snd_pcm_alaw_t));
	if (!alaw) {
		return -ENOMEM;
	}
	alaw->sformat = sformat;
	alaw->plug.read = snd_pcm_alaw_read_areas;
	alaw->plug.write = snd_pcm_alaw_write_areas;
	alaw->plug.slave = slave;
	alaw->plug.close_slave = close_slave;

	pcm = calloc(1, sizeof(snd_pcm_t));
	if (!pcm) {
		free(alaw);
		return -ENOMEM;
	}
	if (name)
		pcm->name = strdup(name);
	pcm->type = SND_PCM_TYPE_ALAW;
	pcm->stream = slave->stream;
	pcm->mode = slave->mode;
	pcm->ops = &snd_pcm_alaw_ops;
	pcm->op_arg = pcm;
	pcm->fast_ops = &snd_pcm_plugin_fast_ops;
	pcm->fast_op_arg = pcm;
	pcm->private_data = alaw;
	pcm->poll_fd = slave->poll_fd;
	pcm->hw_ptr = &alaw->plug.hw_ptr;
	pcm->appl_ptr = &alaw->plug.appl_ptr;
	*pcmp = pcm;

	return 0;
}

int _snd_pcm_alaw_open(snd_pcm_t **pcmp, const char *name,
			 snd_config_t *conf, 
			 snd_pcm_stream_t stream, int mode)
{
	snd_config_iterator_t i, next;
	const char *sname = NULL;
	int err;
	snd_pcm_t *spcm;
	snd_config_t *slave = NULL;
	snd_pcm_format_t sformat;
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id = snd_config_get_id(n);
		if (strcmp(id, "comment") == 0)
			continue;
		if (strcmp(id, "type") == 0)
			continue;
		if (strcmp(id, "slave") == 0) {
			slave = n;
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}
	if (!slave) {
		SNDERR("slave is not defined");
		return -EINVAL;
	}
	err = snd_pcm_slave_conf(slave, &sname, 1,
				 SND_PCM_HW_PARAM_FORMAT, 1, &sformat);
	if (err < 0)
		return err;
	if (snd_pcm_format_linear(sformat) != 1 &&
	    sformat != SND_PCM_FORMAT_A_LAW) {
		SNDERR("invalid slave format");
		return -EINVAL;
	}
	/* This is needed cause snd_config_update may destroy config */
	sname = strdup(sname);
	if (!sname)
		return  -ENOMEM;
	err = snd_pcm_open(&spcm, sname, stream, mode);
	free((void *) sname);
	if (err < 0)
		return err;
	err = snd_pcm_alaw_open(pcmp, name, sformat, spcm, 1);
	if (err < 0)
		snd_pcm_close(spcm);
	return err;
}
				

