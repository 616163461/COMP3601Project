/** 22T3 COMP3601 Design Project A
 * File name: main.c
 * Description: Example main file for using the audio_i2s driver for your Zynq audio driver.
 *
 * Distributed under the MIT license.
 * Copyright (c) 2022 Elton Shih
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include "audio_i2s.h"
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <termios.h>



#define NUM_CHANNELS 1
#define BPS 32 // bit per sample
#define SAMPLE_RATE  41000// 44100
#define RECORD_DURATION 0.5 /* 0.5 second buffer, because each time step is half a second */
#define BUF_SIZE int(SAMPLE_RATE * RECORD_DURATION)
#define TRANSFER_RUNS int(SAMPLE_RATE * RECORD_DURATION / 128) + 50


typedef struct {
    char chunkId[4];
    uint32_t chunkSize;
    char format[4];
    char subchunk1Id[4];
    uint32_t subchunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char subchunk2Id[4];
    uint32_t subchunk2Size;
    int32_t *data;
} WavFile;


void read_wav_file(const char *filename, WavFile *wav) {
    // Read a new wav file into our WavFile data structure

    printf("read\n");
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "can not open the file\n");
        return;
    }
  
    fread(wav, sizeof(WavFile) - sizeof(int32_t*), 1, file);

    wav->data = (int32_t*)malloc(wav->subchunk2Size);
    if (wav->data == NULL) {
        fprintf(stderr, "Unable to allocate enough memory\n");
        fclose(file);
        return;
    }
    for (uint32_t i = 0; i < wav->subchunk2Size / 4 - 1; i++) {
        if (fread(&wav->data[i], 4, 1, file) != 1) {
            if (feof(file)) {
                printf("%d\n", i);
                fprintf(stderr, "the end of the file\n");
            } else if (ferror(file)) {
                fprintf(stderr, "error during reading\n");
            } else {
                fprintf(stderr, "unknown error\n");
            }
            free(wav->data);
            wav->data = NULL;
            fclose(file);
            return;
        }
    }
  
    fclose(file);
}

void write_wav_file(const char *filename, WavFile *wav) {
    // Write a new wav file to disk from our WavFile data structure

    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        printf("write\n");
        fprintf(stderr, "can not open the file\n");
        return;
    }
  
    fwrite(wav, sizeof(WavFile) - sizeof(int32_t*), 1, file);

    for (uint32_t i = 0; i < wav->subchunk2Size / 4; i++) {
        int32_t sample = wav->data[i];
        fwrite(&sample, 4, 1, file);
    }
  
    fclose(file);
}

void overlap_wav_files(WavFile *wav1, WavFile *wav2, WavFile *output) {
    // Join 2 wav files together by overlaping

    // calculate the smaller one for twp wav - we do not need
    uint32_t minSize = wav1->subchunk2Size < wav2->subchunk2Size ? wav1->subchunk2Size : wav2->subchunk2Size;

    memcpy(output, wav1, sizeof(WavFile) - sizeof(int32_t*));
    output->subchunk2Size = minSize;

    output->data = (int32_t *)malloc(minSize);
    if (output->data == NULL) {
        fprintf(stderr, "Unable to allocate enough memory\n");
        return;
    }

    // Computational overlap sample
    for (uint32_t i = 0; i < minSize / 4; i++) {
        output->data[i] = (wav1->data[i] + wav2->data[i]) / 2;
    }
}

void append_wav_files(WavFile *wav1, WavFile *wav2, WavFile *output) {
    // Join 2 wav files together by appending

    memcpy(output, wav1, sizeof(WavFile) - sizeof(int32_t*));
    output->subchunk2Size = wav1->subchunk2Size + wav2->subchunk2Size;

    // Allocate space to store additional samples
    output->data = (int32_t *)malloc(output->subchunk2Size);
    if (output->data == NULL) {
        fprintf(stderr, "Unable to allocate enough memory\n");
        return;
    }

    // copy
    memcpy(output->data, wav1->data, wav1->subchunk2Size);
    memcpy(output->data + wav1->subchunk2Size / 4, wav2->data, wav2->subchunk2Size);
}


// get empty sound chunk
void generate_silent_wav(const char *filename, float duration_sec) {
    WavFile silent_wav;

    // Fill the header information
    memcpy(silent_wav.chunkId, "RIFF", 4);
    silent_wav.chunkSize = 36 + 41000 * duration_sec * 4; // Duration is calculated as sampleRate * seconds * bytesPerSample
    memcpy(silent_wav.format, "WAVE", 4);
    memcpy(silent_wav.subchunk1Id, "fmt ", 4);
    silent_wav.subchunk1Size = 16;
    silent_wav.audioFormat = 1;
    silent_wav.numChannels = 1;
    silent_wav.sampleRate = 41000;
    silent_wav.byteRate = 164000;
    silent_wav.blockAlign = 4;
    silent_wav.bitsPerSample = 32;
    memcpy(silent_wav.subchunk2Id, "data", 4);
    silent_wav.subchunk2Size = 41000 * duration_sec * 4; // Duration is calculated as sampleRate * seconds * bytesPerSample

    // Allocate memory for data
    silent_wav.data = (int32_t*)malloc(silent_wav.subchunk2Size);
    if (silent_wav.data == NULL) {
        fprintf(stderr, "Unable to allocate enough memory\n");
        return;
    }

    // Fill the data with zeros
    memset(silent_wav.data, 0, silent_wav.subchunk2Size);

    // Write the silent wav file
    write_wav_file(filename, &silent_wav);

    // Free the allocated memory
    free(silent_wav.data);
}

WavFile renderColumn(bool composition[4][8], int j) {
    //create the sound for one column of the composition using the recorded sounds, depending on which sounds are active in that column of the composition array
    WavFile wav0;
    WavFile wav1;
    WavFile wav2;
    WavFile wav3;
    WavFile output1;
    WavFile output2;
    WavFile output3;



    // i - row max 4
    // j - column max 8
    int i = 0;
    while (i < 4) {
        if (composition[i][j]) {
            // snprintf(test, sizeof(test), "test%d.wav", i);
            if (i == 0) read_wav_file("0.wav", &wav0);
            else if (i == 1) read_wav_file("1.wav", &wav1);
            else if (i == 2) read_wav_file("2.wav", &wav2);
            else if (i == 3) read_wav_file("3.wav", &wav3);
        } else {
            if (i == 0) read_wav_file("none.wav", &wav0);
            else if (i == 1) read_wav_file("none.wav", &wav1);
            else if (i == 2) read_wav_file("none.wav", &wav2);
            else if (i == 3) read_wav_file("none.wav", &wav3);
        }
        i++;
    }
    // overlap them
    overlap_wav_files(&wav0, &wav1, &output1);

    overlap_wav_files(&output1, &wav2, &output2);
    overlap_wav_files(&output2, &wav3, &output3);
    return output3;
}

WavFile joinColumns(WavFile *Cols) {
    //join together all 8 columns

    WavFile wav0;
    WavFile wav1;
    WavFile wav2;
    WavFile wav3;
    WavFile wav4;
    WavFile wav5;
    WavFile wav6;
    WavFile wav7;
    WavFile output1;
    WavFile output2;
    WavFile output3;
    WavFile output4;
    WavFile output11;
    WavFile output12;
    WavFile output;


    // i - row max 4
    // j - column max 8
    int i = 0;
    while (i < 8) {
        if (i == 0) append_wav_files(&Cols[i], &Cols[i + 1], &output1);
        else if (i == 2) append_wav_files(&Cols[i], &Cols[i + 1], &output2);
        else if (i == 4) append_wav_files(&Cols[i], &Cols[i + 1], &output3);
        else if (i == 6) append_wav_files(&Cols[i], &Cols[i + 1], &output4);
        i = i + 2;
    }
    i = 0;
    while (i < 4) {
        if (i == 0) append_wav_files(&output1, &output2, &output11);
        else if (i == 2) append_wav_files(&output3, &output4, &output12);
        i = i + 2;
    }
    // add them
    append_wav_files(&output11, &output12, &output);
    return output;
}

void makeFinalWav(bool composition[4][8]){
    // Renders the final .wav file using the composition array and the 4 recorded sounds
    WavFile output;

    // Generate a silent wav file to use when a sound is not active in a timestep
    generate_silent_wav("none.wav", RECORD_DURATION);

    // loop through the columns of the composition and stack the wav files depending on what sounds are active
    int j = 0;
    WavFile Cols[8] = {};
    while (j < 8) {
        Cols[j] = renderColumn(composition, j);
        j++;
    }

    //join together the columns
    output = joinColumns(Cols);

    //write to output.wav
    write_wav_file("output.wav", &output);
}

void bin(uint8_t n) {
    uint8_t i;
    for (i = 0; i < 8; i++) // LSB first
        (n & (1 << i)) ? printf("1") : printf("0");
}

void parsemem(void* virtual_address, int word_count) {
    uint32_t *p = (uint32_t *)virtual_address;
    char *b = (char*)virtual_address;
    int offset;

    uint32_t sample_count = 0;
    uint32_t sample_value = 0;
    for (offset = 0; offset < word_count; offset++) {

        // These two lines are extracting different portions
        // of the 32-bit word at p[offset], with sample_value
        // receiving the least significant 18 bits and
        // sample_count receiving the most significant 14 bits.

        sample_value = p[offset] & ((1<<18)-1);
        sample_count = p[offset] >> 18;

        for (int i = 0; i < 4; i++) {
            bin(b[offset*4+i]);
            printf(" ");
        }

        // the expression in the last parameter of the printf
        // is scaling sample_value from itsoriginal range of
        // [0, 262143] (as it's derived from the least
        // significant 18 bits of a 32-bit word) to a new
        // range of [0, 100].

        printf(" -> [%d]: %02x (%dp)\n", sample_count, sample_value, sample_value*100/((1<<18)-1));
    }
}

// Based on code from https://karplus4arduino.wordpress.com/2011/10/08/making-wav-files-from-c-programs/
void write_little_endian(unsigned int word, int num_bytes, FILE *wav_file)
{
    unsigned buf;
    while(num_bytes>0)
    {   buf = word & 0xff;
        fwrite(&buf, 1,1, wav_file);
        num_bytes--;
    word >>= 8;
    }
}

// Reverse all the bits we receive
unsigned int reverseBits(unsigned int num) {
    unsigned int  numOfBits = sizeof(num) * 8;
    unsigned int reverseNum = 0;
    unsigned int i;

    // looping through num of bits
    for (i = 0; i < numOfBits; i++) {
        if((num & (1 << i)))
           reverseNum |= 1 << ((numOfBits - 1) - i);
    }
    return reverseNum;
}


// Based on code from https://karplus4arduino.wordpress.com/2011/10/08/making-wav-files-from-c-programs/
void write_wav(const char * filename, unsigned long num_samples, uint32_t * data, int s_rate)
{
    // Writes data to a new wav file
    FILE* wav_file;
    unsigned int sample_rate;
    unsigned int num_channels;
    unsigned int bytes_per_sample;
    unsigned int byte_rate;
    unsigned long i;    /* counter for samples */

    num_channels = 1;   /* monoaural */
    bytes_per_sample = 4;

    if (s_rate<=0) sample_rate = 44100;
    else sample_rate = (unsigned int) s_rate;
    //sample_rate *= 2;

    byte_rate = sample_rate*num_channels*bytes_per_sample;

    wav_file = fopen(filename, "wb");
    assert(wav_file);   /* make sure it opened */

    /* write RIFF header */
    fwrite("RIFF", 1, 4, wav_file);
    write_little_endian(36 + bytes_per_sample* num_samples*num_channels, 4, wav_file);
    fwrite("WAVE", 1, 4, wav_file);

    /* write fmt  subchunk */
    fwrite("fmt ", 1, 4, wav_file);
    write_little_endian(16, 4, wav_file);   /* SubChunk1Size is 16 */
    write_little_endian(1, 2, wav_file);    /* PCM is format 1 */
    write_little_endian(num_channels, 2, wav_file);
    write_little_endian(sample_rate, 4, wav_file);
    write_little_endian(byte_rate, 4, wav_file);
    write_little_endian(num_channels*bytes_per_sample, 2, wav_file);  /* block align */
    write_little_endian(8*bytes_per_sample, 2, wav_file);  /* bits/sample */

    /* write data subchunk */
    fwrite("data", 1, 4, wav_file);
    write_little_endian(bytes_per_sample* num_samples*num_channels, 4, wav_file);
    for (i=0; i< num_samples; i++)
    {
        write_little_endian((unsigned int)(data[i]),bytes_per_sample, wav_file);
    }

    fclose(wav_file);
}

int getSound(int num) {
    // This function is used to record a new sound, it is effectively our M3 code, and the parameter is used to decide which sound slot to record to (0.wav, 1.wav, 2.wav, 3.wav)
    printf("Entered main\n");

    // defining frames, contains TRANSFER_RUNS (10) frames, each frame containing (TRANSFER_LEN) int32's (each represents a 32bit sample)
    uint32_t frames[TRANSFER_RUNS][TRANSFER_LEN] = {0};

    // initialising audio_i2s and getting the configuration from it
    audio_i2s_t my_config;
    if (audio_i2s_init(&my_config) < 0) {
        printf("Error initializing audio_i2s\n");
        return -1;
    }

    printf("mmapped address: %p\n", my_config.v_baseaddr);
    printf("Before writing to CR: %08x\n", audio_i2s_get_reg(&my_config, AUDIO_I2S_CR));
    audio_i2s_set_reg(&my_config, AUDIO_I2S_CR, 0x1);
    printf("After writing to CR: %08x\n", audio_i2s_get_reg(&my_config, AUDIO_I2S_CR));
    printf("SR: %08x\n", audio_i2s_get_reg(&my_config, AUDIO_I2S_SR));
    printf("Key: %08x\n", audio_i2s_get_reg(&my_config, AUDIO_I2S_KEY));
    printf("Before writing to gain: %08x\n", audio_i2s_get_reg(&my_config, AUDIO_I2S_GAIN));
    audio_i2s_set_reg(&my_config, AUDIO_I2S_GAIN, 0x1);
    printf("After writing to gain: %08x\n", audio_i2s_get_reg(&my_config, AUDIO_I2S_GAIN));


    printf("Initializesd audio_i2s\n");
    printf("Starting audio_i2s_recv\n");

    // getting the audio data
    for (int i = 0; i < TRANSFER_RUNS; i++) {
        // geting TRANSFER_LEN (256) samples (int32's) from i2s/axi/etc
        int32_t *samples = audio_i2s_recv(&my_config);
        // writing these samples to a frame in frames
        memcpy(frames[i], samples, TRANSFER_LEN*sizeof(uint32_t));
    }

    // cleaning up
    audio_i2s_release(&my_config);

    uint32_t buffer[BUF_SIZE] = {0};

    // i is loop through frames
    int i;
    // t is loop through all bits in each frames
    int t;
    // p is increasing when we save bits to buffer
    int p = 0;
    // check is checking the first line for output is left channel or right channel
    int check = 0;
    // check if the odd line is channel without 0 or nor
    if (frames[0][0] == 0) {
        check = 1;
    }


    int startFade1 = BUF_SIZE/8.5 - 40;
    int endFade1 = BUF_SIZE/8.5;
    int startFade2 = BUF_SIZE*2 - 40;
    int endFade2 = BUF_SIZE*2;
    // save the sound to buffer
    for (i = 0; i < TRANSFER_RUNS; i++) {
        for (t = 0; t < TRANSFER_LEN; t++) {
            if (t % 2 == check) {
                if (p < BUF_SIZE){
                    // when save into buffer, need to be reverse bits first
                    buffer[p] = reverseBits(frames[i][t]);
                    if (buffer[p] == 0){
                        buffer[p] = buffer[p-1];
                    }
                }
                p++;
            }
        }

        // check it works
        if (i % 100 == check) {
            // if it works, the buffer number should not always be 0 or the same value
            printf("buffer[%d] =%u\n", p, buffer[p]);
        }
    }

    // write the test.wav and save as a .wav file
    char filename[20];
    sprintf(filename, "%d.wav", num);
    write_wav(filename, BUF_SIZE, buffer, SAMPLE_RATE);
    // Ensuring proper resource cleanup
    // releasing hardware components upon task completion
    audio_i2s_release(&my_config);
    // check if we update smaple256
    printf("update wave \n");

    return 0;
}


bool sendString(int sock, const char *str) {
    // Send a string to the arduino
    if (send(sock, str, strlen(str), 0) < 0) {
        puts("Send failed");
        return false;
    }
    return true;
}

void sendCompositionToLEDs(int sock, bool composition[][8], int row) {
    // Update the arduino LEDs based on the current composition, by sending a string in the form (xxxxxxxx) to the arduino, where each x represents the state of one of the 8 LEDs
    char message[12];  // "(00000000)\n" + null terminator
    message[0] = '(';
    for (int i = 0; i < 8; i++) {
        message[i + 1] = composition[row][i] ? '1' : '0';
    }
    message[9] = ')';
    message[10] = '\n';
    message[11] = '\0';

    puts("Sending ");
    puts(message);
    sendString(sock, message);
}

void amplify(){
    // Simple amplification of the final wav file
    WavFile old;
    read_wav_file("output.wav", &old);

    for (uint32_t i = 0; i < old.subchunk2Size / 4; i++) {
        old.data[i] = old.data[i]*16;
    }

    write_wav_file("output_amplified.wav", &old);
}

int main() {
    printf("Entered main\n");

    // This stores the current composition, 4 sounds * 8 time steps
    bool composition[4][8] = {};

    // Which row / sound is currently selected
    int row = 0;

    int sock;
    struct sockaddr_in server;

    // To hold data received from the arduino
    char server_reply[2000];

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("Could not create socket");
    }

    // Setting the timeout of recv to 100ms, so we can keep polling for new input from the arduino but also do other things if we haven't received anything
    struct timeval tv;
    tv.tv_sec = 0;  // Timeout in seconds
    tv.tv_usec = 100000;  // Timeout in microseconds

    // Set up socket
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
            perror("setsockopt failed");
    }

    // This is the ip address chosen for the server in the arduino code
    server.sin_addr.s_addr = inet_addr("192.168.1.177");
    server.sin_family = AF_INET;
    server.sin_port = htons(80); // Port 80 like the arduino server

    // Connect to remote server (the arduino)
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect failed. Error");
        return 1;
    }

    printf("connected, now waiting\n");

    //sending all LEDs off as the intialy configuration of the arduino LEDs
    sendCompositionToLEDs(sock, composition, row);

    // Sit in a loop receiving button presses and sending LED updates
    while (true) {

        // Try to receive some data from the arduino
        memset(server_reply, 0, sizeof(server_reply));
        if (recv(sock, server_reply, 2000, 0) != -1){
            puts("Server reply :");
            puts(server_reply);
        }

        // Changing the row based on the row/sound buttons
        bool rowChanged = false;
        if (strcmp(server_reply, "[0]") == 0){
            row = 0;
            rowChanged = true;
        } else if (strcmp(server_reply, "[1]") == 0){
            row = 1;
            rowChanged = true;
        } else if (strcmp(server_reply, "[2]") == 0){
            row = 2;
            rowChanged = true;
        } else if (strcmp(server_reply, "[3]") == 0){
            row = 3;
            rowChanged = true;
        }

        if (strcmp(server_reply, "[4]") == 0){
            // Bottom middle button
            // Renders the final wav file
            makeFinalWav(composition);

            // The microphone recording is usually very quite so we amplify the final file
            amplify();
        }
        if (strcmp(server_reply, "[5]") == 0){
            // Top middle button
            // Records a new sound into the selected sound slot
            getSound(row);
        }

       // Modifying the composition in the selected row with the timeline buttons
        bool compositionChanged = false;
        // data from the arduino comes in the form [x] where x is the button number
        if (server_reply[0] == '[' && server_reply[2] == ']') {
            int index = server_reply[1] - '0';
            if (index >= 6){
                composition[row][index - 6] = !composition[row][index - 6];
                compositionChanged = true;
            }
        } else if (server_reply[0] == '[' && server_reply[3] == ']') {
            int index = (server_reply[1] - '0') * 10 + (server_reply[2] - '0');  // Convert two digit chars to int
            if (index >= 10 && index <= 13) {
                composition[row][index - 6] = !composition[row][index - 6];
                compositionChanged = true;
            }
        }

        // update arduino LEDs if something has changed
        if (compositionChanged || rowChanged){
            sendCompositionToLEDs(sock, composition, row);
        }
    }

    close(sock);

    return 0;
}
