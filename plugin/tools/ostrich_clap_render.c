// SPDX-License-Identifier: GPL-3.0-or-later
/*
    Load Ostrich7765.clap without a GUI, play notes through it, save its state and write the
    stereo mix to a WAV -- the smoke test in CI, and a way to render without a host.

        ostrich_clap_render Ostrich7765.clap out.wav [notes] [channel] [rate] [seconds]

    `notes`: "note@sec,..." (default "36@0.5,38@1.0,42@1.5"), each at velocity 100 and held
    for 0.25 s, on `channel` (1-16, default 10).  Renders `seconds` (default 3) at `rate`
    (default 48000).  Every output bus gets buffers of its own (all switched on).  Prints
    the plugin's name, its output buses, the state's size and the peak of each output.
*/

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clap/entry.h"
#include "clap/plugin-factory.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/state.h"

#define FRAMES 256
#define MAX_BUSES 32
#define MAX_CHANNELS 64

static const void *host_get_extension(const clap_host_t *h, const char *id) { (void)h; (void)id; return NULL; }
static void host_noop(const clap_host_t *h) { (void)h; }
static const clap_host_t host = {
	CLAP_VERSION, NULL, "ostrich_clap_render", "Ostrich 7765", "", "0",
	host_get_extension, host_noop, host_noop, host_noop
};

typedef struct { clap_event_note_t ev[64]; uint32_t n; } events_t;
static uint32_t in_size(const clap_input_events_t *l) { return ((const events_t *)l->ctx)->n; }
static const clap_event_header_t *in_get(const clap_input_events_t *l, uint32_t i)
{ return &((const events_t *)l->ctx)->ev[i].header; }
static bool out_push(const clap_output_events_t *l, const clap_event_header_t *e) { (void)l; (void)e; return true; }

typedef struct { uint8_t *p; size_t n, cap; } buf_t;
static int64_t ostream_write(const clap_ostream_t *s, const void *d, uint64_t n)
{
	buf_t *b = (buf_t *)s->ctx;
	if (b->n + n > b->cap) { b->cap = (b->n + n) * 2; b->p = realloc(b->p, b->cap); }
	memcpy(b->p + b->n, d, n); b->n += n; return (int64_t)n;
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: %s plugin.clap out.wav [notes] [channel] [rate] [seconds]\n", argv[0]);
		return 2;
	}
	const char *notes_arg = argc > 3 ? argv[3] : "36@0.5,38@1.0,42@1.5";
	const int channel = argc > 4 ? atoi(argv[4]) - 1 : 9;
	const double rate = argc > 5 ? atof(argv[5]) : 48000.0;
	const double seconds = argc > 6 ? atof(argv[6]) : 3.0;

	struct { int key; double at; } notes[32]; int nnotes = 0;
	for (char *t = strtok(strdup(notes_arg), ","); t && nnotes < 32; t = strtok(NULL, ","))
	{
		char *at = strchr(t, '@');
		notes[nnotes].key = atoi(t);
		notes[nnotes ++].at = at ? atof(at + 1) : 0.5;
	}

	void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
	const clap_plugin_entry_t *entry = dlsym(lib, "clap_entry");
	if (!entry) { fprintf(stderr, "no clap_entry\n"); return 1; }
	entry->init(argv[1]);
	const clap_plugin_factory_t *f = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	const clap_plugin_descriptor_t *d = f->get_plugin_descriptor(f, 0);
	printf("plugin: %s (%s) version %s\n", d->name, d->id, d->version);
	const clap_plugin_t *p = f->create_plugin(f, &host, d->id);
	if (!p->init(p)) { fprintf(stderr, "init failed\n"); return 1; }

	// The output buses, as the host would see them.
	const clap_plugin_audio_ports_t *ap = p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
	uint32_t nbus = ap ? ap->count(p, false) : 0;
	if (nbus > MAX_BUSES) nbus = MAX_BUSES;
	uint32_t width[MAX_BUSES], nchan = 0;
	for (uint32_t b = 0; b < nbus; b ++)
	{
		clap_audio_port_info_t info;
		memset(&info, 0, sizeof(info));
		ap->get(p, b, false, &info);
		width[b] = info.channel_count;
		nchan += info.channel_count;
		printf("output bus %u: %s, %u channel%s\n", b, info.name, info.channel_count, info.channel_count == 1 ? "" : "s");
	}
	printf("outputs: %u buses, %u channels\n", nbus, nchan);
	if (nbus == 0 || nchan > MAX_CHANNELS) { fprintf(stderr, "unexpected outputs\n"); return 1; }

	if (!p->activate(p, rate, 1, FRAMES)) { fprintf(stderr, "activate failed\n"); return 1; }
	p->start_processing(p);

	const uint32_t total = (uint32_t)(rate * seconds);
	float *out[MAX_CHANNELS];
	for (uint32_t c = 0; c < nchan; c ++)
		out[c] = calloc(total + FRAMES, sizeof(float));
	for (uint32_t pos = 0; pos < total; pos += FRAMES)
	{
		events_t evs; memset(&evs, 0, sizeof(evs));
		for (int j = 0; j < nnotes; j ++)
			for (int k = 0; k < 2; k ++)
			{
				const uint32_t at = (uint32_t)(rate * (notes[j].at + (k ? 0.25 : 0.0)));
				if (at >= pos && at < pos + FRAMES && evs.n < 64)
				{
					clap_event_note_t *e = &evs.ev[evs.n ++];
					e->header.size = sizeof(*e);
					e->header.time = at - pos;
					e->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
					e->header.type = k ? CLAP_EVENT_NOTE_OFF : CLAP_EVENT_NOTE_ON;
					e->note_id = -1; e->port_index = 0; e->channel = (int16_t)channel;
					e->key = (int16_t)notes[j].key;
					e->velocity = 100.0 / 127.0;
				}
			}
		// CLAP wants events sorted by time.
		for (uint32_t a = 1; a < evs.n; a ++)
			for (uint32_t b = a; b > 0 && evs.ev[b].header.time < evs.ev[b - 1].header.time; b --)
			{
				clap_event_note_t t = evs.ev[b]; evs.ev[b] = evs.ev[b - 1]; evs.ev[b - 1] = t;
			}
		clap_input_events_t in = { &evs, in_size, in_get };
		clap_output_events_t oev = { NULL, out_push };
		float *chans[MAX_CHANNELS];
		clap_audio_buffer_t ob[MAX_BUSES];
		uint32_t c = 0;
		for (uint32_t b = 0; b < nbus; b ++)
		{
			ob[b] = (clap_audio_buffer_t){ chans + c, NULL, width[b], 0, 0 };
			for (uint32_t k = 0; k < width[b]; k ++, c ++)
				chans[c] = out[c] + pos;
		}
		clap_process_t proc = { (int64_t)pos, FRAMES, NULL, NULL, ob, 0, nbus, &in, &oev };
		p->process(p, &proc);
	}

	const clap_plugin_state_t *st = p->get_extension(p, CLAP_EXT_STATE);
	if (st)
	{
		buf_t b = { 0 };
		clap_ostream_t os = { &b, ostream_write };
		const bool ok = st->save(p, &os);
		printf("state: %s, %zu bytes\n", ok ? "saved" : "FAILED", b.n);
		free(b.p);
	}
	p->stop_processing(p);
	p->deactivate(p);
	p->destroy(p);
	entry->deinit();

	// The mix (the first bus, stereo) to the WAV; the peak of every channel to stdout.
	FILE *w = fopen(argv[2], "wb");
	if (!w) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
	const uint32_t mixc = width[0] >= 2 ? 2 : 1;
	const uint32_t bytes = total * 2 * mixc;
	uint32_t v; uint16_t s;
	fwrite("RIFF", 1, 4, w); v = 36 + bytes; fwrite(&v, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w);
	v = 16; fwrite(&v, 4, 1, w); s = 1; fwrite(&s, 2, 1, w); s = (uint16_t)mixc; fwrite(&s, 2, 1, w);
	v = (uint32_t)rate; fwrite(&v, 4, 1, w); v = (uint32_t)rate * 2 * mixc; fwrite(&v, 4, 1, w);
	s = (uint16_t)(2 * mixc); fwrite(&s, 2, 1, w); s = 16; fwrite(&s, 2, 1, w);
	fwrite("data", 1, 4, w); fwrite(&bytes, 4, 1, w);
	for (uint32_t i = 0; i < total; i ++)
		for (uint32_t c = 0; c < mixc; c ++)
		{
			int q = (int)(out[c][i] * 32767.0f); if (q > 32767) q = 32767; if (q < -32768) q = -32768;
			int16_t q16 = (int16_t)q; fwrite(&q16, 2, 1, w);
		}
	fclose(w);
	printf("wrote %s\npeaks:", argv[2]);
	for (uint32_t c = 0; c < nchan; c ++)
	{
		float peak = 0;
		for (uint32_t i = 0; i < total; i ++)
		{
			const float x = out[c][i] < 0 ? -out[c][i] : out[c][i];
			if (x > peak) peak = x;
		}
		printf(" %.3f", peak);
	}
	printf("\n");
	return 0;
}
