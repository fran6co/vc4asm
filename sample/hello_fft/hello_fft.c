/*
BCM2835 "GPU_FFT" release 3.0
Copyright (c) 2015, Andrew Holme.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <memory.h>
#include <math.h>
#include <time.h>

#include "mailbox.h"
#include "gpu_fft.h"

char Usage[] =
    "Usage: hello_fft.bin log2_N [jobs [loops]]\n"
    "log2_N = log2(FFT_length),       log2_N = 8...22\n"
    "jobs   = transforms per batch,   jobs>0,        default 1\n"
    "loops  = number of test repeats, loops>0,       default 1\n";

unsigned Microseconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec*1000000 + ts.tv_nsec/1000;
}

int main(int argc, char *argv[]) {
    int i, j, k, ret, loops, fbias, finc, freq, log2_N, jobs, N, dir, bad, mb = mbox_open();
    unsigned t[3];
    double tsq[4];

    struct GPU_FFT_COMPLEX *base;
    struct GPU_FFT *fft;

    log2_N = argc>1? atoi(argv[1]) : 12; // 8 <= log2_N <= 22
    jobs   = argc>2? atoi(argv[2]) : 1;  // transforms per batch
    loops  = argc>3? atoi(argv[3]) : 1;  // test repetitions
    fbias  = argc>4? atoi(argv[4]) : 1;  // frequency bias
    finc   = argc>5? atoi(argv[5]) : 1;  // frequency increment

    dir = GPU_FFT_REV;
    if (log2_N < 0)
    {   dir = GPU_FFT_FWD;
        log2_N = -log2_N;
    }
    if (argc<2 || jobs<1 || loops<1) {
        printf(Usage);
        return -1;
    }

    N = 1<<log2_N; // FFT length
    ret = gpu_fft_prepare(mb, log2_N, dir, jobs, &fft); // call once

    switch(ret) {
        case -1: printf("Unable to enable V3D. Please check your firmware is up to date.\n"); return -1;
        case -2: printf("log2_N=%d not supported.  Try between 8 and 22.\n", log2_N);         return -1;
        case -3: printf("Out of memory.  Try a smaller batch or increase GPU memory.\n");     return -1;
        case -4: printf("Unable to map Videocore peripherals into ARM memory space.\n");      return -1;
        case -5: printf("Can't open libbcm_host.\n");                                         return -1;
    }

    t[2]=0;
    for (k=0; k<loops; k++) {

        for (j=0; j<jobs; j++) {
            base = fft->in + j*fft->step; // input buffer
            if (dir == GPU_FFT_REV)
            {   freq = j*finc+fbias & (N-1);
                memset(base, 0, N * sizeof(struct GPU_FFT_COMPLEX));
                base[freq].re = 0.5;
                base[N-freq & N-1].re += 0.5;
            } else
            {   freq = j*finc+fbias & (N-1);
                for (i=0; i<N; i++) {
                    base[i].re = cos(2*GPU_FFT_PI*freq*i/N);
                    base[i].im = sin(2*GPU_FFT_PI*freq*i/N);
                }
            }
        }

        usleep(1); // Yield to OS
        t[0] = Microseconds();
        gpu_fft_execute(fft); // call one or many times
        t[1] = Microseconds();
        if (k)
            t[2] += t[1] - t[0];

        tsq[2]=tsq[3]=0;
        bad = 0;
        for (j=0; j<jobs; j++) {
            tsq[0]=tsq[1]=0;
            base = fft->out + j*fft->step; // output buffer
            if (dir == GPU_FFT_REV) {
              freq = j*finc+fbias & (N-1);
              for (i=0; i<N; i++) {
                  double re = cos(2*GPU_FFT_PI*freq*i/N);
                  tsq[0] += pow(re, 2);
                  tsq[1] += pow(re - base[i].re, 2) + pow(base[i].im, 2);
                  //fprintf(stderr, "%10g\t%10g\t%10g\t%10g\t%10g\t%10g\n", base[i].re, base[i].im, fft->out[i-fft->step].re, fft->out[i-fft->step].im, fft->out[i-2*fft->step].re, fft->out[i-2*fft->step].im);
                  //fprintf(stderr, "%g\t%g\t%g\t%g\n", base[i].re, base[i].im, base[i-fft->step].re, base[i-fft->step].im);
              }
            } else {
              freq = j*finc+fbias & (N-1);
              tsq[0] += 2*N;
              for (i=0; i<N; i++) {
                  double amp = (i == freq) * N;
                  amp = pow(amp - base[i].re, 2) + pow(base[i].im, 2);
                  tsq[1] += amp;
                  //fprintf(stderr, "%10g\t%10g\t%10g\t%10g\t%10g\n", base[i].re, base[i].im, fft->in[i].re, fft->in[i].im, amp);
                  //fprintf(stderr, "%10g\t%10g\t%10g\t%10g\t%10g\t%10g\t%10g\n", base[i].re, base[i].im, fft->out[i-fft->step].re, fft->out[i-fft->step].im, fft->out[i-2*fft->step].re, fft->out[i-2*fft->step].im, amp);
              }
            }
            //fprintf(stderr, "step_rms_err = %.5e, j = %d\n", sqrt(tsq[1]/tsq[0]), j);
            tsq[2] += tsq[0];
            tsq[3] += tsq[1];
            if (tsq[1]/tsq[0] > 2E-6)
                ++bad;
        }

        printf("rel_rms_err = %.5e, usecs = %f, k = %d\n",
            sqrt(tsq[3]/tsq[2]), (double)(t[1]-t[0])/jobs, k);
        if (bad)
            printf("failed: %i = %f %%\n", bad, 100.*bad/jobs);
    }

    gpu_fft_release(fft); // Videocore memory lost if not freed !

    if (loops > 1)
        printf("average: usecs = %f\n", (double)t[2]/jobs/(loops-1));
    return 0;
}
