#ifndef MUSIC_H
#define MUSIC_H

#include <Arduino.h>
#include "pitches.h"

const int melody[] = {
  NOTE_C4, NOTE_G3, NOTE_G3, NOTE_A3, NOTE_G3, 0, NOTE_B3, NOTE_C4
};
const int noteDurations[] = {
  4, 8, 8, 4, 4, 4, 4, 4
};

// This class allows us to play the end-of-cooking melody asynchronously.
// By avoiding blocking delay() calls, the ESP32 can continue polling
// the encoder dial and button smoothly while the speaker is active.
class NonBlockingMelody {
private:
  int _speakerPin;            // GPIO pin connected to the buzzer
  int _currentNote;           // Tracks our current note in the melody array
  unsigned long _noteStartTime; // Timestamp when the current note started playing
  unsigned long _noteDuration;  // How long the current note should sound (ms)
  bool _isPlaying;            // Keeps track of whether the buzzer is active

public:
  // Constructor: Initialize the buzzer with the pin number
  NonBlockingMelody(int pin) : _speakerPin(pin), _currentNote(0), _noteStartTime(0), _noteDuration(0), _isPlaying(false) {}

  // Starts playback of the melody from the first note
  void start() {
    _currentNote = 0;
    _isPlaying = true;
    _noteStartTime = 0; // Set to 0 to trigger immediate playing of the first note in update()
  }

  // Stops playback immediately and turns off the buzzer pin tone
  void stop() {
    _isPlaying = false;
    noTone(_speakerPin);
  }

  // Returns true if the melody is currently playing
  bool isPlaying() const {
    return _isPlaying;
  }

  // This needs to be called in loop() to update notes in a non-blocking way
  void update() {
    if (!_isPlaying) return;

    unsigned long currentMillis = millis();
    
    // Check if it is time to move on to the next note in the sequence
    if (_noteStartTime == 0 || (currentMillis - _noteStartTime >= _noteDuration)) {
      if (_currentNote >= 8) {
        // We reached the end of the melody; stop playing
        stop();
        return;
      }

      // Calculate the duration of the current note (noteDurations holds the type, e.g. 4 for quarter note)
      int duration = 1000 / noteDurations[_currentNote];
      _noteDuration = duration;
      _noteStartTime = currentMillis;

      // Play the tone if it's not a rest (0)
      if (melody[_currentNote] != 0) {
        tone(_speakerPin, melody[_currentNote], duration);
      } else {
        noTone(_speakerPin); // It's a rest note
      }

      _currentNote++; // Advance to the next note index
    }
  }
};

#endif // MUSIC_H
