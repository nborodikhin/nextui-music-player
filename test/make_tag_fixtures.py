#!/usr/bin/env python3
"""Write MP3 files with ID3 tags of fixed content, to check the tag readers on the screen.

Usage: make_tag_fixtures.py <directory>

Each file holds about two seconds of silent MPEG-1 Layer III frames. The expected
title, artist and album of each file:

1 - sample.mp3            双截棍, 周杰伦, 范特西 (ID3v1 holds "???" in each field)
2 - dotted.mp3            Mr. Brightside, The Killers, Hot Fuss (album from ID3v1)
3 - no v2 title.mp3       Song, Artist, no album (title from ID3v1)
01 - Song.mp3             01 - Song, Artist, Album (no title in either tag)
5 - v1 only.mp3           V1 Song, no artist ("???" in ID3v1), no album
6 - latin1.mp3            Café, Latin-1, no album (ISO-8859-1 in encoding 0)
7 - utf8 as latin1.mp3    Café, UTF-8, no album (UTF-8 in encoding 0)
8 - lyrics.mp3            Lyrics, Artist, no album, with the lyrics 第一行 and 第二行
"""

import struct
import sys
from pathlib import Path

# MPEG-1 Layer III, 128 kbit/s, 44100 Hz, no padding: 417 bytes. A frame of zero
# side information decodes as silence.
FRAME_HEADER = bytes([0xFF, 0xFB, 0x90, 0x00])
FRAME_SIZE = 417
FRAME_COUNT = 80

LATIN1 = 0
UTF16 = 1


def syncsafe(value):
    return bytes([(value >> 21) & 0x7F, (value >> 14) & 0x7F, (value >> 7) & 0x7F, value & 0x7F])


def frame(frame_id, body):
    return frame_id.encode("ascii") + struct.pack(">I", len(body)) + b"\x00\x00" + body


def utf16(text):
    return b"\xFF\xFE" + text.encode("utf-16-le")


def text_frame(frame_id, text, encoding=UTF16):
    if isinstance(text, bytes):
        return frame(frame_id, bytes([encoding]) + text)
    if encoding == UTF16:
        return frame(frame_id, bytes([UTF16]) + utf16(text))
    return frame(frame_id, bytes([encoding]) + text.encode("latin-1"))


def lyrics_frame(description, text):
    body = bytes([UTF16]) + b"eng" + utf16(description) + b"\x00\x00" + utf16(text)
    return frame("USLT", body)


def id3v2(*frames):
    body = b"".join(frames)
    return b"ID3\x03\x00\x00" + syncsafe(len(body)) + body


def id3v1(title=b"", artist=b"", album=b""):
    def field(value, size):
        return value.ljust(size, b"\x00")

    return (b"TAG" + field(title, 30) + field(artist, 30) + field(album, 30) +
            field(b"", 4) + field(b"", 30) + b"\xFF")


def audio():
    return (FRAME_HEADER + bytes(FRAME_SIZE - len(FRAME_HEADER))) * FRAME_COUNT


FIXTURES = {
    "1 - sample.mp3": (
        id3v2(text_frame("TPE1", "周杰伦"), text_frame("TIT2", "双截棍"), text_frame("TALB", "范特西")),
        id3v1(b"???", b"???", b"???"),
    ),
    "2 - dotted.mp3": (
        id3v2(text_frame("TIT2", "Mr. Brightside"), text_frame("TPE1", "The Killers")),
        id3v1(b"Mr Brightside", b"The Killers", b"Hot Fuss"),
    ),
    "3 - no v2 title.mp3": (
        id3v2(text_frame("TPE1", "Artist")),
        id3v1(b"Song"),
    ),
    "01 - Song.mp3": (
        id3v2(text_frame("TPE1", "Artist")),
        id3v1(album=b"Album"),
    ),
    "5 - v1 only.mp3": (
        b"",
        id3v1(b"V1 Song", b"???"),
    ),
    "6 - latin1.mp3": (
        id3v2(text_frame("TIT2", "Café", LATIN1), text_frame("TPE1", "Latin-1", LATIN1)),
        b"",
    ),
    "7 - utf8 as latin1.mp3": (
        id3v2(text_frame("TIT2", "Café".encode("utf-8"), LATIN1), text_frame("TPE1", "UTF-8", LATIN1)),
        b"",
    ),
    "8 - lyrics.mp3": (
        id3v2(text_frame("TIT2", "Lyrics"), text_frame("TPE1", "Artist"),
              lyrics_frame("desc", "第一行\n第二行")),
        b"",
    ),
}


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2

    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    for name, (head, tail) in FIXTURES.items():
        (directory / name).write_bytes(head + audio() + tail)
        print(directory / name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
