
#ifndef MAKE_WAV_H
#define MAKE_WAV_H

void write_wav(char * filename, unsigned long num_samples, short int * data, int s_rate);
    /* open a file named filename, write signed 16-bit values as a
        monoaural WAV file at the specified sampling rate
        and close the file
    */

#endif