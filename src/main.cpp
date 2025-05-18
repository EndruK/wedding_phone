#include <Arduino.h>
#include "FS.h"
#include "SPI.h"
#include "AudioTools.h"
#include "SD.h"
#include "AudioTools/Disk/AudioSourceSDFAT.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"

#define SD_MISO 19
#define SD_MOSI 21
#define SD_CLK 18
#define SD_CS 5

#define MICRO_ADC 35

#define BUTTON_PIN 15

#define OUT_I2S_BCK 14
#define OUT_I2S_WS 16
#define OUT_I2S_DATA 32

void record_start(bool pinStatus, int pin, void *ref);
void record_end(bool pinStatus, int pin, void *ref);
void writeFile(fs::FS &fs, const char * path, const char * message);
String getNextAvailableFilename(const char* extension = ".wav");
void startAudioPlayback();
void stopAudioPlayback();

SPIClass sdSPI(VSPI);
AnalogAudioStream in;
AudioInfo info(8000, 1, 16);
AudioInfo wavencodercfg(44100, 1, 16);

AudioInfo info_output(44100, 1, 16);
WAVDecoder wav;
I2SStream out;
EncodedAudioOutput output_decoder(&out, &wav);
StreamCopy output_copier;

SdFat32 sdf;
File file;
File outputFile;
WAVEncoder encoder;
EncodedAudioStream encoderOutWAV(&file, &encoder);
StreamCopy copier;

unsigned long start = 0;
bool recording = false;
bool playing = false;

int situation = LOW;
int prevsituation = LOW;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  // AudioLogger::instance().begin(Serial, AudioLogger::Info);



  sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
  if(!SD.begin(SD_CS, sdSPI)) {
    Serial.println("Card Mount Failed");
    return;
  }
  Serial.println("1");
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }
  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR - SD card initialization failed!");
    return;    // init failed
  }
  Serial.println("Done initializing SD card");
}

void loop() {
  situation = digitalRead(BUTTON_PIN);
  // stop after 5min to avoid SD overflow
  if(recording && millis() - start >= 300000) {
    record_end(0, 0, NULL);
  }
  if (situation != prevsituation) {
    prevsituation = situation;
    delay(20);
    if (situation == LOW)  {
      Serial.println("Button released");
      startAudioPlayback();
    }

    if (situation == HIGH) {
      if(playing) {
        stopAudioPlayback();
      }
      if(recording) {
        record_end(0, 0, NULL);
      }
    }
  }
  if(playing) {
    output_copier.copy();
    if(!output_copier.available()) {
      stopAudioPlayback();
      Serial.println("starting the recording");
      start = millis();
      record_start(0, 0, NULL);
    }
  }
  if(recording) {
    copier.copy();
  }
}

void startAudioPlayback() {
  Serial.println("start of playback");
  auto config = out.defaultConfig(TX_MODE);
  config.copyFrom(info_output);
  config.pin_bck = 14;
  config.pin_ws = 16;
  config.pin_data = 23;
  config.port_no = 1;
  out.begin(config);
  outputFile = SD.open("/0001.wav", FILE_READ);
  output_decoder.begin();
  output_copier.begin(output_decoder, outputFile);
  playing = true;
}

void stopAudioPlayback() {
  // volume.end();
  output_copier.end();
  output_decoder.end();
  outputFile.close();
  out.end();
  Serial.println("end of playback");
  playing = false;
}

void record_start(bool pinStatus, int pin, void *ref) {
  // setup input
  auto cfg = in.defaultConfig(RX_MODE);
  cfg.setInputPin1(MICRO_ADC);
  cfg.is_auto_center_read = true;
  cfg.copyFrom(info);
  in.begin(cfg);
  String newFileName = getNextAvailableFilename();
  Serial.println("Recording to " + newFileName + "...");
  file = SD.open(newFileName.c_str(), FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for recording");
    return;
  }
  encoderOutWAV.begin(wavencodercfg);
  copier.begin(encoderOutWAV, in);
  recording = true;
}

void record_end(bool pinStatus, int pin, void *ref) {
  Serial.println("Stop recording");
  copier.end();
  file.close();
  encoderOutWAV.end();
  encoderOutWAV.flush();
  in.end();
  recording = false;
  Serial.println("release button to continue");
}

String getNextAvailableFilename(const char* extension) {
  int fileNumber = 2;
  char filename[13]; // 8.3 filename format: "0000.wav" + null terminator

  while (fileNumber < 10000) { // 4-digit limit: 0000 to 9999
      sprintf(filename, "/%04d%s", fileNumber, extension);
      if (!SD.exists((String)filename)) {
          return String(filename);
      }
      fileNumber++;
  }

  // If all file names are used
  return String(""); // or handle the overflow as needed
}