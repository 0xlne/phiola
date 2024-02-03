/** phiola: MPEG1 encode
2015, Simon Zolin */

#include <acodec/alib3-bridge/mp3lame.h>

void* mpeg_enc_open(phi_track *t)
{
	if (!ffsz_eq(t->data_type, "pcm")) {
		errlog(t, "unsupported input data format: %s", t->data_type);
		return PHI_OPEN_ERR;
	}

	mpeg_enc *m = ffmem_new(mpeg_enc);
	mpeg_enc_conf.qual = 2;
	t->mpg_lametag = 0;
	return m;
}

void mpeg_enc_close(void *ctx, phi_track *t)
{
	mpeg_enc *m = ctx;
	ffmpg_enc_close(&m->mpg);
	ffmem_free(m);
}

int mpeg_enc_process(void *ctx, phi_track *t)
{
	mpeg_enc *m = ctx;
	ffpcm pcm;
	int r, qual;

	switch (m->state) {
	case 0:
	case 1:
		ffpcm_fmtcopy(&pcm, &t->audio.convfmt);
		m->mpg.interleaved = t->audio.convfmt.interleaved;

		qual = (t->mpeg.quality != -1) ? t->mpeg.quality : (int)mpeg_enc_conf.qual;
		if (0 != (r = ffmpg_create(&m->mpg, &pcm, qual))) {

			if (r == FFMPG_EFMT && m->state == 0) {
				t->audio.convfmt.format = pcm.format;
				m->state = 1;
				return PHI_MORE;
			}

			errlog(t, "ffmpg_create() failed: %s", ffmpg_enc_errstr(&m->mpg));
			return PHI_ERR;
		}

		if (t->audio.total != ~0ULL) {
			uint64 total = (t->audio.total - t->audio.pos) * t->audio.convfmt.sample_rate / t->audio.format.sample_rate;
			t->output.size = ffmpg_enc_size(&m->mpg, total);
		}
		t->data_type = "mpeg";

		m->state = 2;
		// fallthrough

	case 2:
		m->mpg.pcm = (void*)t->data_in.ptr;
		m->mpg.pcmlen = t->data_in.len;
		m->state = 3;
		// fallthrough

	case 3:
		break;
	}

	for (;;) {
		r = ffmpg_encode(&m->mpg);
		switch (r) {

		case FFMPG_RDATA:
			goto data;

		case FFMPG_RMORE:
			if (!(t->chain_flags & PHI_FFIRST)) {
				m->state = 2;
				return PHI_MORE;
			}
			m->mpg.fin = 1;
			break;

		case FFMPG_RDONE:
			t->mpg_lametag = 1;
			goto data;

		default:
			errlog(t, "ffmpg_encode() failed: %s", ffmpg_enc_errstr(&m->mpg));
			return PHI_ERR;
		}
	}

data:
	ffstr_set(&t->data_out, m->mpg.data, m->mpg.datalen);

	dbglog(t, "output: %L bytes"
		, m->mpg.datalen);
	return (r == FFMPG_RDONE) ? PHI_DONE : PHI_DATA;
}

const phi_filter phi_mpeg_enc = {
	mpeg_enc_open, mpeg_enc_process, mpeg_enc_close,
	"mpeg-encode"
};
