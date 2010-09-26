/*
 *  VobSub2SRT is a simple command line program to convert .idx/.sub subtitles
 *  into .srt text subtitles by using OCR (tesseract). See README.
 *
 *  Copyright (C) 2010 Rüdiger Sonderfeld <ruediger@c-plusplus.de>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// MPlayer stuff
#include "mp_msg.h" // mplayer message framework
#include "vobsub.h"
#include "spudec.h"

// Tesseract OCR
#include "tesseract/baseapi.h"

#include <iostream>
#include <string>
#include <cstdio>
using namespace std;

typedef void* vob_t;

/** Converts time stamp in pts format to a string containing the time stamp for the srt format
 *
 * pts (presentation time stamp) is given with a 90kHz resolution (1/90 ms). srt expects a time stamp as  HH:MM:SS:MSS.
 */
std::string pts2srt(unsigned pts) {
  unsigned ms = pts/90;
  unsigned const h = ms / (3600 * 1000);
  ms -= h * 3600 * 1000;
  unsigned const m = ms / (60 * 1000);
  ms -= m * 60 * 1000;
  unsigned const s = ms / 1000;
  ms %= 1000;

  enum { length = 13 }; // HH:MM:SS:MSS\0
  char buf[length];
  snprintf(buf, length, "%02d:%02d:%02d,%03d", h, m, s, ms);
  return std::string(buf);
}

/// Dumps the image data to <subtitlename>-<subtitleid>.pgm in Netbpm PGM format
void dump_pgm(char const *filename, unsigned counter, unsigned width, unsigned height,
              unsigned char const *image, size_t image_size) {
  char buf[50];
  snprintf(buf, sizeof(buf), "%s-%u.pgm", filename, counter);
  FILE *pgm = fopen(buf, "wb");
  if(pgm) {
    fprintf(pgm, "P5\n%u %u %u\n", width, height, 255u);
    fwrite(image, 1, image_size, pgm);
    fclose(pgm);
  }
}

int main(int argc, char **argv) {
  bool dump_images = false;

  // Handle cmd arguments
  if(argc < 2 or argc > 3) {
    cerr << "usage: " << argv[0] << " <subname> [<ifo>]\n\n\t<subname> ... without .idx/.sub suffix.\n\t<ifo> ... optional path to ifo file\n";
    return 1;
  }

  // Init the mplayer part
  verbose = 1; // mplayer verbose level
  mp_msg_init();

  // Init Tesseract
  TessBaseAPI::SimpleInit("/usr/share/tesseract-ocr/tessdata", "eng", false); // TODO params

  // Open the sub/idx subtitles
  void *spu;
  vob_t vob = vobsub_open(argv[1], argc == 3 ? argv[3] : 0x0, 1, &spu);
  if(not vob) {
    cerr << "Couldn't open VobSub\n";
    return 1;
  }

  // Open srt output file
  string srt_filename = argv[0];
  srt_filename += ".srt";
  FILE *srtout = fopen(srt_filename.c_str(), "w");
  if(not srtout) {
    perror("could not open .srt file");
  }

  // Read subtitles and convert
  void *packet;
  int timestamp; // pts100
  int len;
  unsigned sub_counter = 1;
  while( (len = vobsub_get_next_packet(vob, &packet, &timestamp)) > 0) {
    if(timestamp >= 0) {
      cout << "timestamp: " << timestamp << " -> " << pts2srt(timestamp) << endl; // DEBUG
      spudec_assemble(spu, reinterpret_cast<unsigned char*>(packet), len, timestamp);
      spudec_heartbeat(spu, timestamp);
      unsigned char const *image;
      size_t image_size;
      unsigned width, height, stride, start_pts, end_pts;
      spudec_get_data(spu, &image, &image_size, &width, &height, &stride, &start_pts, &end_pts);
      cout << "start_pts: " << start_pts << " -> " << pts2srt(start_pts) << endl; // DEBUG
      cout << "end_pts: " << end_pts << " -> " << pts2srt(end_pts) << endl; // DEBUG
      cout << "width: " << width << " height: " << height << " stride: " << stride << " size: " << image_size << endl; // DEBUG

      if(dump_images) {
        dump_pgm(argv[1], sub_counter, width, height, image, image_size);
      }

      char *text = TessBaseAPI::TesseractRect(image, 1, stride, 0, 0, width, height);
      if(not text) {
        cerr << "OCR failed\n";
        continue;
      }
      cout << "Text: " << text << endl; // DEBUG
      fprintf(srtout, "%u\n%s --> %s\n%s\n\n", sub_counter, pts2srt(timestamp).c_str(), pts2srt(end_pts).c_str(), text);
      delete[]text;
      ++sub_counter;
    }
  }

  TessBaseAPI::End();
  fclose(srtout);
  cout << "Wrote Subtitles to '" << srt_filename << "'\n";
  vobsub_close(vob);
  spudec_free(spu);
}
