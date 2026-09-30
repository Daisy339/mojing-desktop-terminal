import 'dart:typed_data';

import 'package:image/image.dart' as img;

// Same black-bit polarity, stretch resize and threshold as the existing web UI.
List<String> encodeAlbum(List<Uint8List> files) {
  final frames = <String>[];
  for (final bytes in files) {
    final decoded = img.decodeImage(bytes);
    if (decoded == null) {
      throw const FormatException('无法解码图片，请使用 PNG、JPEG 或 GIF');
    }
    for (final frame in decoded.frames) {
      if (frames.length == 8) return frames;
      final resized = img.copyResize(frame, width: 400, height: 300);
      final output = StringBuffer();
      int packed = 0, bits = 0;
      for (final pixel in resized) {
        final a = pixel.a / 255;
        final gray = ((pixel.r + pixel.g + pixel.b) / 3) * a + 255 * (1 - a);
        packed = (packed << 1) | (gray < 150 ? 1 : 0);
        if (++bits == 8) {
          output.write(packed.toRadixString(16).padLeft(2, '0'));
          packed = 0;
          bits = 0;
        }
      }
      frames.add(output.toString());
    }
  }
  return frames;
}
