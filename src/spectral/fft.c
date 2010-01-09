/*
  Copyright (C) 2003-2009 Paul Brossier <piem@aubio.org>

  This file is part of aubio.

  aubio is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  aubio is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with aubio.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "aubio_priv.h"
#include "fvec.h"
#include "cvec.h"
#include "mathutils.h"
#include "spectral/fft.h"

/* note that <complex.h> is not included here but only in aubio_priv.h, so that
 * c++ projects can still use their own complex definition. */
#include <fftw3.h>
#include <pthread.h>

#ifdef HAVE_COMPLEX_H
#if HAVE_FFTW3F
/** fft data type with complex.h and fftw3f */
#define FFTW_TYPE fftwf_complex
#else
/** fft data type with complex.h and fftw3 */
#define FFTW_TYPE fftw_complex
#endif
#else
#if HAVE_FFTW3F
/** fft data type without complex.h and with fftw3f */
#define FFTW_TYPE float
#else
/** fft data type without complex.h and with fftw */
#define FFTW_TYPE double
#endif
#endif

/** fft data type */
typedef FFTW_TYPE fft_data_t;

#if HAVE_FFTW3F
#define fftw_malloc            fftwf_malloc
#define fftw_free              fftwf_free
#define fftw_execute           fftwf_execute
#define fftw_plan_dft_r2c_1d   fftwf_plan_dft_r2c_1d
#define fftw_plan_dft_c2r_1d   fftwf_plan_dft_c2r_1d
#define fftw_plan_r2r_1d       fftwf_plan_r2r_1d
#define fftw_plan              fftwf_plan
#define fftw_destroy_plan      fftwf_destroy_plan
#endif

#if HAVE_FFTW3F
#if HAVE_AUBIO_DOUBLE
#warning "Using aubio in double precision with fftw3 in single precision"
#endif /* HAVE_AUBIO_DOUBLE */
#define real_t float 
#else /* HAVE_FFTW3F */
#if !HAVE_AUBIO_DOUBLE
#warning "Using aubio in single precision with fftw3 in double precision"
#endif /* HAVE_AUBIO_DOUBLE */
#define real_t double 
#endif /* HAVE_FFTW3F */

// a global mutex for FFTW thread safety
pthread_mutex_t aubio_fftw_mutex = PTHREAD_MUTEX_INITIALIZER;

struct _aubio_fft_t {
  uint_t winsize;
  uint_t fft_size;
  real_t *in, *out;
  fftw_plan pfw, pbw;
  fft_data_t * specdata;     /* complex spectral data */
  fvec_t * compspec;
};

aubio_fft_t * new_aubio_fft(uint_t winsize) {
  aubio_fft_t * s = AUBIO_NEW(aubio_fft_t);
  uint_t i;
  s->winsize  = winsize;
  /* allocate memory */
  s->in       = AUBIO_ARRAY(real_t,winsize);
  s->out      = AUBIO_ARRAY(real_t,winsize);
  s->compspec = new_fvec(winsize);
  /* create plans */
  pthread_mutex_lock(&aubio_fftw_mutex);
#ifdef HAVE_COMPLEX_H
  s->fft_size = winsize/2 + 1;
  s->specdata = (fft_data_t*)fftw_malloc(sizeof(fft_data_t)*s->fft_size);
  s->pfw = fftw_plan_dft_r2c_1d(winsize, s->in,  s->specdata, FFTW_ESTIMATE);
  s->pbw = fftw_plan_dft_c2r_1d(winsize, s->specdata, s->out, FFTW_ESTIMATE);
#else
  s->fft_size = winsize;
  s->specdata = (fft_data_t*)fftw_malloc(sizeof(fft_data_t)*s->fft_size);
  s->pfw = fftw_plan_r2r_1d(winsize, s->in,  s->specdata, FFTW_R2HC, FFTW_ESTIMATE);
  s->pbw = fftw_plan_r2r_1d(winsize, s->specdata, s->out, FFTW_HC2R, FFTW_ESTIMATE);
#endif
  pthread_mutex_unlock(&aubio_fftw_mutex);
  for (i = 0; i < s->winsize; i++) {
    s->in[i] = 0.;
    s->out[i] = 0.;
  }
  for (i = 0; i < s->fft_size; i++) {
    s->specdata[i] = 0.;
  }
  return s;
}

void del_aubio_fft(aubio_fft_t * s) {
  /* destroy data */
  del_fvec(s->compspec);
  fftw_destroy_plan(s->pfw);
  fftw_destroy_plan(s->pbw);
  fftw_free(s->specdata);
  AUBIO_FREE(s->out);
  AUBIO_FREE(s->in );
  AUBIO_FREE(s);
}

void aubio_fft_do(aubio_fft_t * s, fvec_t * input, cvec_t * spectrum) {
  aubio_fft_do_complex(s, input, s->compspec);
  aubio_fft_get_spectrum(s->compspec, spectrum);
}

void aubio_fft_rdo(aubio_fft_t * s, cvec_t * spectrum, fvec_t * output) {
  aubio_fft_get_realimag(spectrum, s->compspec);
  aubio_fft_rdo_complex(s, s->compspec, output);
}

void aubio_fft_do_complex(aubio_fft_t * s, fvec_t * input, fvec_t * compspec) {
  uint_t j;
  for (j=0; j < s->winsize; j++) {
    s->in[j] = input->data[j];
  }
  fftw_execute(s->pfw);
#ifdef HAVE_COMPLEX_H
  compspec->data[0] = REAL(s->specdata[0]);
  for (j = 1; j < s->fft_size -1 ; j++) {
    compspec->data[j] = REAL(s->specdata[j]);
    compspec->data[compspec->length - j] = IMAG(s->specdata[j]);
  }
  compspec->data[s->fft_size-1] = REAL(s->specdata[s->fft_size-1]);
#else
  for (j = 0; j < s->fft_size; j++) {
    compspec->data[j] = s->specdata[j];
  }
#endif
}

void aubio_fft_rdo_complex(aubio_fft_t * s, fvec_t * compspec, fvec_t * output) {
  uint_t j;
  const smpl_t renorm = 1./(smpl_t)s->winsize;
#ifdef HAVE_COMPLEX_H
  s->specdata[0] = compspec->data[0];
  for (j=1; j < s->fft_size - 1; j++) {
    s->specdata[j] = compspec->data[j] + 
      I * compspec->data[compspec->length - j];
  }
  s->specdata[s->fft_size - 1] = compspec->data[s->fft_size - 1];
#else
  for (j=0; j < s->fft_size; j++) {
    s->specdata[j] = compspec->data[j];
  }
#endif
  fftw_execute(s->pbw);
  for (j = 0; j < output->length; j++) {
    output->data[j] = s->out[j]*renorm;
  }
}

void aubio_fft_get_spectrum(fvec_t * compspec, cvec_t * spectrum) {
  aubio_fft_get_phas(compspec, spectrum);
  aubio_fft_get_norm(compspec, spectrum);
}

void aubio_fft_get_realimag(cvec_t * spectrum, fvec_t * compspec) {
  aubio_fft_get_imag(spectrum, compspec);
  aubio_fft_get_real(spectrum, compspec);
}

void aubio_fft_get_phas(fvec_t * compspec, cvec_t * spectrum) {
  uint_t j;
  if (compspec->data[0] < 0) {
    spectrum->phas[0] = PI;
  } else {
    spectrum->phas[0] = 0.;
  }
  for (j=1; j < spectrum->length - 1; j++) {
    spectrum->phas[j] = ATAN2(compspec->data[compspec->length-j],
        compspec->data[j]);
  }
  if (compspec->data[compspec->length/2] < 0) {
    spectrum->phas[spectrum->length - 1] = PI;
  } else {
    spectrum->phas[spectrum->length - 1] = 0.;
  }
}

void aubio_fft_get_norm(fvec_t * compspec, cvec_t * spectrum) {
  uint_t j = 0;
  spectrum->norm[0] = ABS(compspec->data[0]);
  for (j=1; j < spectrum->length - 1; j++) {
    spectrum->norm[j] = SQRT(SQR(compspec->data[j]) 
        + SQR(compspec->data[compspec->length - j]) );
  }
  spectrum->norm[spectrum->length-1] = 
    ABS(compspec->data[compspec->length/2]);
}

void aubio_fft_get_imag(cvec_t * spectrum, fvec_t * compspec) {
  uint_t j;
  for (j = 1; j < ( compspec->length + 1 ) / 2 /*- 1 + 1*/; j++) {
    compspec->data[compspec->length - j] =
      spectrum->norm[j]*SIN(spectrum->phas[j]);
  }
}

void aubio_fft_get_real(cvec_t * spectrum, fvec_t * compspec) {
  uint_t j;
  for (j = 0; j < compspec->length / 2 + 1; j++) {
    compspec->data[j] = 
      spectrum->norm[j]*COS(spectrum->phas[j]);
  }
}
