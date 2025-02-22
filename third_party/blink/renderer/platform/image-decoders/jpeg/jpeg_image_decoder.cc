/*
 * Copyright (C) 2006 Apple Computer, Inc.
 *
 * Portions are Copyright (C) 2001-6 mozilla.org
 *
 * Other contributors:
 *   Stuart Parmenter <stuart@mozilla.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Alternatively, the contents of this file may be used under the terms
 * of either the Mozilla Public License Version 1.1, found at
 * http://www.mozilla.org/MPL/ (the "MPL") or the GNU General Public
 * License Version 2.0, found at http://www.fsf.org/copyleft/gpl.html
 * (the "GPL"), in which case the provisions of the MPL or the GPL are
 * applicable instead of those above.  If you wish to allow use of your
 * version of this file only under the terms of one of those two
 * licenses (the MPL or the GPL) and not to allow others to use your
 * version of this file under the LGPL, indicate your decision by
 * deletingthe provisions above and replace them with the notice and
 * other provisions required by the MPL or the GPL, as the case may be.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under any of the LGPL, the MPL or the GPL.
 */

#include "third_party/blink/renderer/platform/image-decoders/jpeg/jpeg_image_decoder.h"

#include <memory>
#include "build/build_config.h"
#include "third_party/blink/renderer/platform/instrumentation/platform_instrumentation.h"

extern "C" {
#include <stdio.h>  // jpeglib.h needs stdio FILE.
#include "jpeglib.h"
#include "iccjpeg.h"
#include <setjmp.h>
}

#if defined(ARCH_CPU_BIG_ENDIAN)
#error Blink assumes a little-endian target.
#endif

#if defined(JCS_ALPHA_EXTENSIONS)
#define TURBO_JPEG_RGB_SWIZZLE
#if SK_B32_SHIFT  // Output little-endian RGBA pixels (Android).
inline J_COLOR_SPACE rgbOutputColorSpace() {
  return JCS_EXT_RGBA;
}
#else  // Output little-endian BGRA pixels.
inline J_COLOR_SPACE rgbOutputColorSpace() {
  return JCS_EXT_BGRA;
}
#endif
inline bool turboSwizzled(J_COLOR_SPACE colorSpace) {
  return colorSpace == JCS_EXT_RGBA || colorSpace == JCS_EXT_BGRA;
}
#else
inline J_COLOR_SPACE rgbOutputColorSpace() {
  return JCS_RGB;
}
#endif

namespace {

const int exifMarker = JPEG_APP0 + 1;

// JPEG only supports a denominator of 8.
const unsigned g_scale_denomiator = 8;

}  // namespace

namespace blink {

struct decoder_error_mgr {
  DISALLOW_NEW();
  struct jpeg_error_mgr pub;  // "public" fields for IJG library
  int num_corrupt_warnings;   // Counts corrupt warning messages
  jmp_buf setjmp_buffer;      // For handling catastropic errors
};

struct decoder_source_mgr {
  DISALLOW_NEW();
  struct jpeg_source_mgr pub;  // "public" fields for IJG library
  JPEGImageReader* reader;
};

enum jstate {
  JPEG_HEADER,  // Reading JFIF headers
  JPEG_START_DECOMPRESS,
  JPEG_DECOMPRESS_PROGRESSIVE,  // Output progressive pixels
  JPEG_DECOMPRESS_SEQUENTIAL,   // Output sequential pixels
  JPEG_DONE
};

enum yuv_subsampling {
  YUV_UNKNOWN,
  YUV_410,
  YUV_411,
  YUV_420,
  YUV_422,
  YUV_440,
  YUV_444
};

void init_source(j_decompress_ptr jd);
boolean fill_input_buffer(j_decompress_ptr jd);
void skip_input_data(j_decompress_ptr jd, long num_bytes);
void term_source(j_decompress_ptr jd);
void error_exit(j_common_ptr cinfo);
void emit_message(j_common_ptr cinfo, int msg_level);

static unsigned ReadUint16(JOCTET* data, bool is_big_endian) {
  if (is_big_endian)
    return (GETJOCTET(data[0]) << 8) | GETJOCTET(data[1]);
  return (GETJOCTET(data[1]) << 8) | GETJOCTET(data[0]);
}

static unsigned ReadUint32(JOCTET* data, bool is_big_endian) {
  if (is_big_endian)
    return (GETJOCTET(data[0]) << 24) | (GETJOCTET(data[1]) << 16) |
           (GETJOCTET(data[2]) << 8) | GETJOCTET(data[3]);
  return (GETJOCTET(data[3]) << 24) | (GETJOCTET(data[2]) << 16) |
         (GETJOCTET(data[1]) << 8) | GETJOCTET(data[0]);
}

static bool CheckExifHeader(jpeg_saved_marker_ptr marker,
                            bool& is_big_endian,
                            unsigned& ifd_offset) {
  // For exif data, the APP1 block is followed by 'E', 'x', 'i', 'f', '\0',
  // then a fill byte, and then a tiff file that contains the metadata.
  // A tiff file starts with 'I', 'I' (intel / little endian byte order) or
  // 'M', 'M' (motorola / big endian byte order), followed by (uint16_t)42,
  // followed by an uint32_t with the offset to the tag block, relative to the
  // tiff file start.
  const unsigned kExifHeaderSize = 14;
  if (!(marker->marker == exifMarker &&
        marker->data_length >= kExifHeaderSize && marker->data[0] == 'E' &&
        marker->data[1] == 'x' && marker->data[2] == 'i' &&
        marker->data[3] == 'f' &&
        marker->data[4] == '\0'
        // data[5] is a fill byte
        && ((marker->data[6] == 'I' && marker->data[7] == 'I') ||
            (marker->data[6] == 'M' && marker->data[7] == 'M'))))
    return false;

  is_big_endian = marker->data[6] == 'M';
  if (ReadUint16(marker->data + 8, is_big_endian) != 42)
    return false;

  ifd_offset = ReadUint32(marker->data + 10, is_big_endian);
  return true;
}

static ImageOrientation ReadImageOrientation(jpeg_decompress_struct* info) {
  // The JPEG decoder looks at EXIF metadata.
  // FIXME: Possibly implement XMP and IPTC support.
  const unsigned kOrientationTag = 0x112;
  const unsigned kShortType = 3;
  for (jpeg_saved_marker_ptr marker = info->marker_list; marker;
       marker = marker->next) {
    bool is_big_endian;
    unsigned ifd_offset;
    if (!CheckExifHeader(marker, is_big_endian, ifd_offset))
      continue;
    const unsigned kOffsetToTiffData =
        6;  // Account for 'Exif\0<fill byte>' header.
    if (marker->data_length < kOffsetToTiffData ||
        ifd_offset >= marker->data_length - kOffsetToTiffData)
      continue;
    ifd_offset += kOffsetToTiffData;

    // The jpeg exif container format contains a tiff block for metadata.
    // A tiff image file directory (ifd) consists of a uint16_t describing
    // the number of ifd entries, followed by that many entries.
    // When touching this code, it's useful to look at the tiff spec:
    // http://partners.adobe.com/public/developer/en/tiff/TIFF6.pdf
    JOCTET* ifd = marker->data + ifd_offset;
    JOCTET* end = marker->data + marker->data_length;
    if (end - ifd < 2)
      continue;
    unsigned tag_count = ReadUint16(ifd, is_big_endian);
    ifd += 2;  // Skip over the uint16 that was just read.

    // Every ifd entry is 2 bytes of tag, 2 bytes of contents datatype,
    // 4 bytes of number-of-elements, and 4 bytes of either offset to the
    // tag data, or if the data is small enough, the inlined data itself.
    const int kIfdEntrySize = 12;
    for (unsigned i = 0; i < tag_count && end - ifd >= kIfdEntrySize;
         ++i, ifd += kIfdEntrySize) {
      unsigned tag = ReadUint16(ifd, is_big_endian);
      unsigned type = ReadUint16(ifd + 2, is_big_endian);
      unsigned count = ReadUint32(ifd + 4, is_big_endian);
      if (tag == kOrientationTag && type == kShortType && count == 1)
        return ImageOrientation::FromEXIFValue(
            ReadUint16(ifd + 8, is_big_endian));
    }
  }

  return ImageOrientation();
}

static IntSize ComputeYUVSize(const jpeg_decompress_struct* info,
                              int component) {
  return IntSize(info->cur_comp_info[component]->downsampled_width,
                 info->cur_comp_info[component]->downsampled_height);
}

static size_t ComputeYUVWidthBytes(const jpeg_decompress_struct* info,
                                   int component) {
  return info->cur_comp_info[component]->width_in_blocks * DCTSIZE;
}

static yuv_subsampling YuvSubsampling(const jpeg_decompress_struct& info) {
  if ((DCTSIZE == 8) && (info.num_components == 3) && (info.scale_denom <= 8) &&
      (info.cur_comp_info[0]) && (info.cur_comp_info[1]) &&
      (info.cur_comp_info[2]) && (info.cur_comp_info[1]->h_samp_factor == 1) &&
      (info.cur_comp_info[1]->v_samp_factor == 1) &&
      (info.cur_comp_info[2]->h_samp_factor == 1) &&
      (info.cur_comp_info[2]->v_samp_factor == 1)) {
    int h = info.cur_comp_info[0]->h_samp_factor;
    int v = info.cur_comp_info[0]->v_samp_factor;
    // 4:4:4 : (h == 1) && (v == 1)
    // 4:4:0 : (h == 1) && (v == 2)
    // 4:2:2 : (h == 2) && (v == 1)
    // 4:2:0 : (h == 2) && (v == 2)
    // 4:1:1 : (h == 4) && (v == 1)
    // 4:1:0 : (h == 4) && (v == 2)
    if (v == 1) {
      switch (h) {
        case 1:
          return YUV_444;
        case 2:
          return YUV_422;
        case 4:
          return YUV_411;
        default:
          break;
      }
    } else if (v == 2) {
      switch (h) {
        case 1:
          return YUV_440;
        case 2:
          return YUV_420;
        case 4:
          return YUV_410;
        default:
          break;
      }
    }
  }

  return YUV_UNKNOWN;
}

static void ProgressMonitor(j_common_ptr info) {
  int scan = ((j_decompress_ptr)info)->input_scan_number;
  // Progressive images with a very large number of scans can cause the
  // decoder to hang.  Here we use the progress monitor to abort on
  // a very large number of scans.  100 is arbitrary, but much larger
  // than the number of scans we might expect in a normal image.
  if (scan >= 100) {
    error_exit(info);
  }
}

class JPEGImageReader final {
  USING_FAST_MALLOC(JPEGImageReader);
  WTF_MAKE_NONCOPYABLE(JPEGImageReader);

 public:
  JPEGImageReader(JPEGImageDecoder* decoder)
      : decoder_(decoder),
        needs_restart_(false),
        restart_position_(0),
        next_read_position_(0),
        last_set_byte_(nullptr),
        state_(JPEG_HEADER),
        samples_(nullptr) {
    memset(&info_, 0, sizeof(jpeg_decompress_struct));

    // Set up the normal JPEG error routines, then override error_exit.
    info_.err = jpeg_std_error(&err_.pub);
    err_.pub.error_exit = error_exit;

    // Allocate and initialize JPEG decompression object.
    jpeg_create_decompress(&info_);

    // Initialize source manager.
    memset(&src_, 0, sizeof(decoder_source_mgr));
    info_.src = reinterpret_cast_ptr<jpeg_source_mgr*>(&src_);

    // Set up callback functions.
    src_.pub.init_source = init_source;
    src_.pub.fill_input_buffer = fill_input_buffer;
    src_.pub.skip_input_data = skip_input_data;
    src_.pub.resync_to_restart = jpeg_resync_to_restart;
    src_.pub.term_source = term_source;
    src_.reader = this;

    // Set up a progress monitor.
    info_.progress = &progress_mgr_;
    progress_mgr_.progress_monitor = ProgressMonitor;

    // Retain ICC color profile markers for color management.
    setup_read_icc_profile(&info_);

    // Keep APP1 blocks, for obtaining exif data.
    jpeg_save_markers(&info_, exifMarker, 0xFFFF);
  }

  ~JPEGImageReader() { jpeg_destroy_decompress(&info_); }

  void SkipBytes(long num_bytes) {
    if (num_bytes <= 0)
      return;

    size_t bytes_to_skip = static_cast<size_t>(num_bytes);

    if (bytes_to_skip < info_.src->bytes_in_buffer) {
      // The next byte needed is in the buffer. Move to it.
      info_.src->bytes_in_buffer -= bytes_to_skip;
      info_.src->next_input_byte += bytes_to_skip;
    } else {
      // Move beyond the buffer and empty it.
      next_read_position_ =
          next_read_position_ + bytes_to_skip - info_.src->bytes_in_buffer;
      info_.src->bytes_in_buffer = 0;
      info_.src->next_input_byte = nullptr;
    }

    // This is a valid restart position.
    restart_position_ = next_read_position_ - info_.src->bytes_in_buffer;
    // We updated |next_input_byte|, so we need to update |last_byte_set_|
    // so we know not to update |restart_position_| again.
    last_set_byte_ = info_.src->next_input_byte;
  }

  bool FillBuffer() {
    if (needs_restart_) {
      needs_restart_ = false;
      next_read_position_ = restart_position_;
    } else {
      UpdateRestartPosition();
    }

    const char* segment;
    const size_t bytes = data_->GetSomeData(segment, next_read_position_);
    if (bytes == 0) {
      // We had to suspend. When we resume, we will need to start from the
      // restart position.
      needs_restart_ = true;
      ClearBuffer();
      return false;
    }

    next_read_position_ += bytes;
    info_.src->bytes_in_buffer = bytes;
    const JOCTET* next_byte = reinterpret_cast_ptr<const JOCTET*>(segment);
    info_.src->next_input_byte = next_byte;
    last_set_byte_ = next_byte;
    return true;
  }

  void SetData(SegmentReader* data) {
    if (data_.get() == data)
      return;

    data_ = data;

    // If a restart is needed, the next call to fillBuffer will read from the
    // new SegmentReader.
    if (needs_restart_)
      return;

    // Otherwise, empty the buffer, and leave the position the same, so
    // FillBuffer continues reading from the same position in the new
    // SegmentReader.
    next_read_position_ -= info_.src->bytes_in_buffer;
    ClearBuffer();
  }

  // Decode the JPEG data. If |only_size| is specified, then only the size
  // information will be decoded.
  bool Decode(bool only_size) {
    // We need to do the setjmp here. Otherwise bad things will happen
    if (setjmp(err_.setjmp_buffer))
      return decoder_->SetFailed();

    J_COLOR_SPACE override_color_space = JCS_UNKNOWN;
    switch (state_) {
      case JPEG_HEADER: {
        // Read file parameters with jpeg_read_header().
        if (jpeg_read_header(&info_, true) == JPEG_SUSPENDED)
          return false;  // I/O suspension.

        switch (info_.jpeg_color_space) {
          case JCS_YCbCr:
            // libjpeg can convert YCbCr image pixels to RGB.
            info_.out_color_space = rgbOutputColorSpace();
            if (decoder_->HasImagePlanes() &&
                (YuvSubsampling(info_) != YUV_UNKNOWN))
              override_color_space = JCS_YCbCr;
            break;
          case JCS_GRAYSCALE:
          case JCS_RGB:
            // libjpeg can convert GRAYSCALE image pixels to RGB.
            info_.out_color_space = rgbOutputColorSpace();
            break;
          case JCS_CMYK:
          case JCS_YCCK:
            // libjpeg can convert YCCK to CMYK, but neither to RGB, so we
            // manually convert CMKY to RGB.
            info_.out_color_space = JCS_CMYK;
            break;
          default:
            return decoder_->SetFailed();
        }

        state_ = JPEG_START_DECOMPRESS;

        // We can fill in the size now that the header is available.
        if (!decoder_->SetSize(info_.image_width, info_.image_height))
          return false;

        // Calculate and set decoded size.
        int max_numerator = decoder_->DesiredScaleNumerator();
        info_.scale_denom = g_scale_denomiator;

        if (decoder_->ShouldGenerateAllSizes()) {
          std::vector<SkISize> sizes;
          sizes.reserve(max_numerator);
          for (int numerator = 1; numerator <= max_numerator; ++numerator) {
            info_.scale_num = numerator;
            jpeg_calc_output_dimensions(&info_);
            sizes.push_back(
                SkISize::Make(info_.output_width, info_.output_height));
          }
          decoder_->SetSupportedDecodeSizes(std::move(sizes));
        }

        info_.scale_num = max_numerator;
        // Scaling caused by running low on memory isn't supported by YUV
        // decoding since YUV decoding is performed on full sized images. At
        // this point, buffers and various image info structs have already been
        // set up for the scaled size after reading the image header using this
        // decoder, so using the full size is no longer possible.
        if (info_.scale_num != info_.scale_denom)
          override_color_space = JCS_UNKNOWN;
        jpeg_calc_output_dimensions(&info_);
        decoder_->SetDecodedSize(info_.output_width, info_.output_height);

        decoder_->SetOrientation(ReadImageOrientation(Info()));

        // Allow color management of the decoded RGBA pixels if possible.
        if (!decoder_->IgnoresColorSpace()) {
          JOCTET* profile_buf = nullptr;
          unsigned profile_length = 0;
          if (read_icc_profile(Info(), &profile_buf, &profile_length)) {
            std::unique_ptr<ColorProfile> profile =
                ColorProfile::Create(profile_buf, profile_length);
            if (profile) {
              uint32_t data_color_space =
                  profile->GetProfile()->data_color_space;
              switch (info_.jpeg_color_space) {
                case JCS_CMYK:
                case JCS_YCCK:
                  if (data_color_space != skcms_Signature_CMYK)
                    profile = nullptr;
                  break;
                case JCS_GRAYSCALE:
                  if (data_color_space != skcms_Signature_Gray &&
                      data_color_space != skcms_Signature_RGB)
                    profile = nullptr;
                  break;
                default:
                  if (data_color_space != skcms_Signature_RGB)
                    profile = nullptr;
                  break;
              }
              Decoder()->SetEmbeddedColorProfile(std::move(profile));
            } else {
              DLOG(ERROR) << "Failed to parse image ICC profile";
            }
            free(profile_buf);
          }
          if (Decoder()->ColorTransform()) {
            override_color_space = JCS_UNKNOWN;
          }
        }
        if (override_color_space == JCS_YCbCr) {
          info_.out_color_space = JCS_YCbCr;
          info_.raw_data_out = TRUE;
          uv_size_ = ComputeYUVSize(
              &info_,
              1);  // U size and V size have to be the same if we got here
        }

        // Don't allocate a giant and superfluous memory buffer when the
        // image is a sequential JPEG.
        info_.buffered_image = jpeg_has_multiple_scans(&info_);
        if (info_.buffered_image) {
          err_.pub.emit_message = emit_message;
          err_.num_corrupt_warnings = 0;
        }

        if (only_size) {
          // This exits the function while there is still potentially
          // data in the buffer. Before this function is called again,
          // the SharedBuffer may be collapsed (by a call to
          // MergeSegmentsIntoBuffer), invalidating the "buffer" (which
          // in reality is a pointer into the SharedBuffer's data).
          // Defensively empty the buffer, but first find the latest
          // restart position and signal to restart, so the next call to
          // FillBuffer will resume from the correct point.
          needs_restart_ = true;
          UpdateRestartPosition();
          ClearBuffer();
          return true;
        }
      }
      FALLTHROUGH;
      case JPEG_START_DECOMPRESS:
        // Set parameters for decompression.
        // FIXME -- Should reset dct_method and dither mode for final pass
        // of progressive JPEG.
        info_.dct_method = JDCT_ISLOW;
        info_.dither_mode = JDITHER_FS;
        info_.do_fancy_upsampling = true;
        info_.do_block_smoothing = true;
        info_.enable_2pass_quant = false;
        // FIXME: should we just assert these?
        info_.enable_external_quant = false;
        info_.enable_1pass_quant = false;
        info_.quantize_colors = false;
        info_.colormap = nullptr;

        // Make a one-row-high sample array that will go away when done with
        // image. Always make it big enough to hold one RGBA row. Since this
        // uses the IJG memory manager, it must be allocated before the call
        // to jpeg_start_decompress().
        samples_ = AllocateSampleArray();

        // Start decompressor.
        if (!jpeg_start_decompress(&info_))
          return false;  // I/O suspension.

        // If this is a progressive JPEG ...
        state_ = (info_.buffered_image) ? JPEG_DECOMPRESS_PROGRESSIVE
                                        : JPEG_DECOMPRESS_SEQUENTIAL;
        FALLTHROUGH;

      case JPEG_DECOMPRESS_SEQUENTIAL:
        if (state_ == JPEG_DECOMPRESS_SEQUENTIAL) {
          if (!decoder_->OutputScanlines())
            return false;  // I/O suspension.

          // If we've completed image output...
          DCHECK_EQ(info_.output_scanline, info_.output_height);
          state_ = JPEG_DONE;
        }
        FALLTHROUGH;

      case JPEG_DECOMPRESS_PROGRESSIVE:
        if (state_ == JPEG_DECOMPRESS_PROGRESSIVE) {
          int status = 0;
          do {
            decoder_error_mgr* err =
                reinterpret_cast_ptr<decoder_error_mgr*>(info_.err);
            if (err->num_corrupt_warnings)
              break;
            status = jpeg_consume_input(&info_);
          } while ((status != JPEG_SUSPENDED) && (status != JPEG_REACHED_EOI));

          for (;;) {
            if (!info_.output_scanline) {
              int scan = info_.input_scan_number;

              // If we haven't displayed anything yet
              // (output_scan_number == 0) and we have enough data for
              // a complete scan, force output of the last full scan.
              if (!info_.output_scan_number && (scan > 1) &&
                  (status != JPEG_REACHED_EOI))
                --scan;

              if (!jpeg_start_output(&info_, scan))
                return false;  // I/O suspension.
            }

            if (info_.output_scanline == 0xffffff)
              info_.output_scanline = 0;

            if (!decoder_->OutputScanlines()) {
              if (decoder_->Failed())
                return false;
              // If no scan lines were read, flag it so we don't call
              // jpeg_start_output() multiple times for the same scan.
              if (!info_.output_scanline)
                info_.output_scanline = 0xffffff;

              return false;  // I/O suspension.
            }

            if (info_.output_scanline == info_.output_height) {
              if (!jpeg_finish_output(&info_))
                return false;  // I/O suspension.

              if (jpeg_input_complete(&info_) &&
                  (info_.input_scan_number == info_.output_scan_number))
                break;

              info_.output_scanline = 0;
            }
          }

          state_ = JPEG_DONE;
        }
        FALLTHROUGH;

      case JPEG_DONE:
        // Finish decompression.
        return jpeg_finish_decompress(&info_);
    }

    return true;
  }

  jpeg_decompress_struct* Info() { return &info_; }
  JSAMPARRAY Samples() const { return samples_; }
  JPEGImageDecoder* Decoder() { return decoder_; }
  IntSize UvSize() const { return uv_size_; }

 private:
#if defined(USE_SYSTEM_LIBJPEG)
  NO_SANITIZE_CFI_ICALL
#endif
  JSAMPARRAY AllocateSampleArray() {
// Some output color spaces don't need the sample array: don't allocate in that
// case.
#if defined(TURBO_JPEG_RGB_SWIZZLE)
    if (turboSwizzled(info_.out_color_space))
      return nullptr;
#endif

    if (info_.out_color_space != JCS_YCbCr)
      return (*info_.mem->alloc_sarray)(
          reinterpret_cast_ptr<j_common_ptr>(&info_), JPOOL_IMAGE,
          4 * info_.output_width, 1);

    // Compute the width of the Y plane in bytes.  This may be larger than the
    // output width, since the jpeg library requires that the allocated width be
    // a multiple of DCTSIZE.  Note that this buffer will be used as garbage
    // memory for rows that extend below the actual height of the image.  We can
    // reuse the same memory for the U and V planes, since we are guaranteed
    // that the Y plane width is at least as large as the U and V plane widths.
    int width_bytes = ComputeYUVWidthBytes(&info_, 0);
    return (*info_.mem->alloc_sarray)(
        reinterpret_cast_ptr<j_common_ptr>(&info_), JPOOL_IMAGE, width_bytes,
        1);
  }

  void UpdateRestartPosition() {
    if (last_set_byte_ != info_.src->next_input_byte) {
      // next_input_byte was updated by jpeg, meaning that it found a restart
      // position.
      restart_position_ = next_read_position_ - info_.src->bytes_in_buffer;
    }
  }

  void ClearBuffer() {
    // Let libjpeg know that the buffer needs to be refilled.
    info_.src->bytes_in_buffer = 0;
    info_.src->next_input_byte = nullptr;
    last_set_byte_ = nullptr;
  }

  scoped_refptr<SegmentReader> data_;
  JPEGImageDecoder* decoder_;

  // Input reading: True if we need to back up to restart_position_.
  bool needs_restart_;
  // If libjpeg needed to restart, this is the position to restart from.
  size_t restart_position_;
  // This is the position where we will read from, unless there is a restart.
  size_t next_read_position_;
  // This is how we know to update the restart position. It is the last value
  // we set to next_input_byte. libjpeg will update next_input_byte when it
  // has found the next restart position, so if it no longer matches this
  // value, we know we've reached the next restart position.
  const JOCTET* last_set_byte_;

  jpeg_decompress_struct info_;
  decoder_error_mgr err_;
  decoder_source_mgr src_;
  jpeg_progress_mgr progress_mgr_;
  jstate state_;

  JSAMPARRAY samples_;
  IntSize uv_size_;
};

void error_exit(
    j_common_ptr cinfo)  // Decoding failed: return control to the setjmp point.
{
  longjmp(reinterpret_cast_ptr<decoder_error_mgr*>(cinfo->err)->setjmp_buffer,
          -1);
}

void emit_message(j_common_ptr cinfo, int msg_level) {
  if (msg_level >= 0)
    return;

  decoder_error_mgr* err = reinterpret_cast_ptr<decoder_error_mgr*>(cinfo->err);
  err->pub.num_warnings++;

  // Detect and count corrupt JPEG warning messages.
  const char* warning = nullptr;
  int code = err->pub.msg_code;
  if (code > 0 && code <= err->pub.last_jpeg_message)
    warning = err->pub.jpeg_message_table[code];
  if (warning && !strncmp("Corrupt JPEG", warning, 12))
    err->num_corrupt_warnings++;
}

void init_source(j_decompress_ptr) {}

void skip_input_data(j_decompress_ptr jd, long num_bytes) {
  reinterpret_cast_ptr<decoder_source_mgr*>(jd->src)->reader->SkipBytes(
      num_bytes);
}

boolean fill_input_buffer(j_decompress_ptr jd) {
  return reinterpret_cast_ptr<decoder_source_mgr*>(jd->src)
      ->reader->FillBuffer();
}

void term_source(j_decompress_ptr jd) {
  reinterpret_cast_ptr<decoder_source_mgr*>(jd->src)
      ->reader->Decoder()
      ->Complete();
}

JPEGImageDecoder::JPEGImageDecoder(AlphaOption alpha_option,
                                   const ColorBehavior& color_behavior,
                                   size_t max_decoded_bytes)
    : ImageDecoder(alpha_option,
                   ImageDecoder::kDefaultBitDepth,
                   color_behavior,
                   max_decoded_bytes) {}

JPEGImageDecoder::~JPEGImageDecoder() = default;

bool JPEGImageDecoder::SetSize(unsigned width, unsigned height) {
  if (!ImageDecoder::SetSize(width, height))
    return false;

  if (!DesiredScaleNumerator())
    return SetFailed();

  SetDecodedSize(width, height);
  return true;
}

void JPEGImageDecoder::OnSetData(SegmentReader* data) {
  if (reader_)
    reader_->SetData(data);
}

void JPEGImageDecoder::SetDecodedSize(unsigned width, unsigned height) {
  decoded_size_ = IntSize(width, height);
}

IntSize JPEGImageDecoder::DecodedYUVSize(int component) const {
  DCHECK_GE(component, 0);
  DCHECK_LE(component, 2);
  DCHECK(reader_);
  const jpeg_decompress_struct* info = reader_->Info();

  DCHECK_EQ(info->out_color_space, JCS_YCbCr);
  return ComputeYUVSize(info, component);
}

size_t JPEGImageDecoder::DecodedYUVWidthBytes(int component) const {
  DCHECK_GE(component, 0);
  DCHECK_LE(component, 2);
  DCHECK(reader_);
  const jpeg_decompress_struct* info = reader_->Info();

  DCHECK_EQ(info->out_color_space, JCS_YCbCr);
  return ComputeYUVWidthBytes(info, component);
}

unsigned JPEGImageDecoder::DesiredScaleNumerator() const {
  size_t original_bytes = Size().Width() * Size().Height() * 4;

  if (original_bytes <= max_decoded_bytes_)
    return g_scale_denomiator;

  // Downsample according to the maximum decoded size.
  unsigned scale_numerator = static_cast<unsigned>(floor(sqrt(
      // MSVC needs explicit parameter type for sqrt().
      static_cast<float>(max_decoded_bytes_ * g_scale_denomiator *
                         g_scale_denomiator / original_bytes))));

  return scale_numerator;
}

bool JPEGImageDecoder::ShouldGenerateAllSizes() const {
  return supported_decode_sizes_.empty();
}

bool JPEGImageDecoder::CanDecodeToYUV() {
  // Calling IsSizeAvailable() ensures the reader is created and the output
  // color space is set.
  return IsSizeAvailable() && reader_->Info()->out_color_space == JCS_YCbCr;
}

bool JPEGImageDecoder::DecodeToYUV() {
  if (!HasImagePlanes())
    return false;

  PlatformInstrumentation::WillDecodeImage("JPEG");
  Decode(false);
  PlatformInstrumentation::DidDecodeImage();
  return !Failed();
}

void JPEGImageDecoder::SetImagePlanes(
    std::unique_ptr<ImagePlanes> image_planes) {
  image_planes_ = std::move(image_planes);
}

void JPEGImageDecoder::SetSupportedDecodeSizes(std::vector<SkISize> sizes) {
  supported_decode_sizes_ = std::move(sizes);
}

std::vector<SkISize> JPEGImageDecoder::GetSupportedDecodeSizes() const {
  // DCHECK IsDecodedSizeAvailable instead of IsSizeAvailable, since the latter
  // has side effects of actually doing the decode.
  DCHECK(IsDecodedSizeAvailable());
  return supported_decode_sizes_;
}

// At the moment we support only JCS_RGB and JCS_CMYK values of the
// J_COLOR_SPACE enum.
// If you need a specific implementation for other J_COLOR_SPACE values,
// please add a full template specialization for this function below.
template <J_COLOR_SPACE colorSpace>
void SetPixel(ImageFrame::PixelData*, JSAMPARRAY samples, int column) = delete;

// Used only for debugging with libjpeg (instead of libjpeg-turbo).
template <>
void SetPixel<JCS_RGB>(ImageFrame::PixelData* pixel,
                       JSAMPARRAY samples,
                       int column) {
  JSAMPLE* jsample = *samples + column * 3;
  ImageFrame::SetRGBARaw(pixel, jsample[0], jsample[1], jsample[2], 255);
}

template <>
void SetPixel<JCS_CMYK>(ImageFrame::PixelData* pixel,
                        JSAMPARRAY samples,
                        int column) {
  JSAMPLE* jsample = *samples + column * 4;

  // Source is 'Inverted CMYK', output is RGB.
  // See: http://www.easyrgb.com/math.php?MATH=M12#text12
  // Or: http://www.ilkeratalay.com/colorspacesfaq.php#rgb
  // From CMYK to CMY:
  // X =   X    * (1 -   K   ) +   K  [for X = C, M, or Y]
  // Thus, from Inverted CMYK to CMY is:
  // X = (1-iX) * (1 - (1-iK)) + (1-iK) => 1 - iX*iK
  // From CMY (0..1) to RGB (0..1):
  // R = 1 - C => 1 - (1 - iC*iK) => iC*iK  [G and B similar]
  unsigned k = jsample[3];
  ImageFrame::SetRGBARaw(pixel, jsample[0] * k / 255, jsample[1] * k / 255,
                         jsample[2] * k / 255, 255);
}

// Used only for JCS_CMYK and JCS_RGB output.  Note that JCS_RGB is used only
// for debugging with libjpeg (instead of libjpeg-turbo).
template <J_COLOR_SPACE colorSpace>
bool OutputRows(JPEGImageReader* reader, ImageFrame& buffer) {
  JSAMPARRAY samples = reader->Samples();
  jpeg_decompress_struct* info = reader->Info();
  int width = info->output_width;

  while (info->output_scanline < info->output_height) {
    // jpeg_read_scanlines will increase the scanline counter, so we
    // save the scanline before calling it.
    int y = info->output_scanline;
    // Request one scanline: returns 0 or 1 scanlines.
    if (jpeg_read_scanlines(info, samples, 1) != 1)
      return false;

    ImageFrame::PixelData* pixel = buffer.GetAddr(0, y);
    for (int x = 0; x < width; ++pixel, ++x)
      SetPixel<colorSpace>(pixel, samples, x);

    ColorProfileTransform* xform = reader->Decoder()->ColorTransform();
    if (xform) {
      ImageFrame::PixelData* row = buffer.GetAddr(0, y);
      skcms_AlphaFormat alpha_format = skcms_AlphaFormat_Unpremul;
      bool color_conversion_successful = skcms_Transform(
          row, XformColorFormat(), alpha_format, xform->SrcProfile(), row,
          XformColorFormat(), alpha_format, xform->DstProfile(), width);
      DCHECK(color_conversion_successful);
    }
  }

  buffer.SetPixelsChanged(true);
  return true;
}

static bool OutputRawData(JPEGImageReader* reader, ImagePlanes* image_planes) {
  JSAMPARRAY samples = reader->Samples();
  jpeg_decompress_struct* info = reader->Info();

  JSAMPARRAY bufferraw[3];
  JSAMPROW bufferraw2[32];
  bufferraw[0] = &bufferraw2[0];   // Y channel rows (8 or 16)
  bufferraw[1] = &bufferraw2[16];  // U channel rows (8)
  bufferraw[2] = &bufferraw2[24];  // V channel rows (8)
  int y_height = info->output_height;
  int v = info->comp_info[0].v_samp_factor;
  IntSize uv_size = reader->UvSize();
  int uv_height = uv_size.Height();
  JSAMPROW output_y = static_cast<JSAMPROW>(image_planes->Plane(0));
  JSAMPROW output_u = static_cast<JSAMPROW>(image_planes->Plane(1));
  JSAMPROW output_v = static_cast<JSAMPROW>(image_planes->Plane(2));
  size_t row_bytes_y = image_planes->RowBytes(0);
  size_t row_bytes_u = image_planes->RowBytes(1);
  size_t row_bytes_v = image_planes->RowBytes(2);

  // Request 8 or 16 scanlines: returns 0 or more scanlines.
  int y_scanlines_to_read = DCTSIZE * v;
  JSAMPROW dummy_row = *samples;
  while (info->output_scanline < info->output_height) {
    // Assign 8 or 16 rows of memory to read the Y channel.
    for (int i = 0; i < y_scanlines_to_read; ++i) {
      int scanline = info->output_scanline + i;
      if (scanline < y_height) {
        bufferraw2[i] = &output_y[scanline * row_bytes_y];
      } else {
        bufferraw2[i] = dummy_row;
      }
    }

    // Assign 8 rows of memory to read the U and V channels.
    int scaled_scanline = info->output_scanline / v;
    for (int i = 0; i < 8; ++i) {
      int scanline = scaled_scanline + i;
      if (scanline < uv_height) {
        bufferraw2[16 + i] = &output_u[scanline * row_bytes_u];
        bufferraw2[24 + i] = &output_v[scanline * row_bytes_v];
      } else {
        bufferraw2[16 + i] = dummy_row;
        bufferraw2[24 + i] = dummy_row;
      }
    }

    JDIMENSION scanlines_read =
        jpeg_read_raw_data(info, bufferraw, y_scanlines_to_read);
    if (!scanlines_read)
      return false;
  }

  info->output_scanline = std::min(info->output_scanline, info->output_height);
  return true;
}

bool JPEGImageDecoder::OutputScanlines() {
  if (HasImagePlanes())
    return OutputRawData(reader_.get(), image_planes_.get());

  if (frame_buffer_cache_.IsEmpty())
    return false;

  jpeg_decompress_struct* info = reader_->Info();

  // Initialize the framebuffer if needed.
  ImageFrame& buffer = frame_buffer_cache_[0];
  if (buffer.GetStatus() == ImageFrame::kFrameEmpty) {
    DCHECK_EQ(info->output_width,
              static_cast<JDIMENSION>(decoded_size_.Width()));
    DCHECK_EQ(info->output_height,
              static_cast<JDIMENSION>(decoded_size_.Height()));

    if (!buffer.AllocatePixelData(info->output_width, info->output_height,
                                  ColorSpaceForSkImages()))
      return SetFailed();

    buffer.ZeroFillPixelData();
    // The buffer is transparent outside the decoded area while the image is
    // loading. The image will be marked fully opaque in Complete().
    buffer.SetStatus(ImageFrame::kFramePartial);
    buffer.SetHasAlpha(true);

    // For JPEGs, the frame always fills the entire image.
    buffer.SetOriginalFrameRect(IntRect(IntPoint(), Size()));
  }

#if defined(TURBO_JPEG_RGB_SWIZZLE)
  if (turboSwizzled(info->out_color_space)) {
    while (info->output_scanline < info->output_height) {
      unsigned char* row = reinterpret_cast_ptr<unsigned char*>(
          buffer.GetAddr(0, info->output_scanline));
      if (jpeg_read_scanlines(info, &row, 1) != 1)
        return false;

      ColorProfileTransform* xform = ColorTransform();
      if (xform) {
        skcms_AlphaFormat alpha_format = skcms_AlphaFormat_Unpremul;
        bool color_conversion_successful = skcms_Transform(
            row, XformColorFormat(), alpha_format, xform->SrcProfile(), row,
            XformColorFormat(), alpha_format, xform->DstProfile(),
            info->output_width);
        DCHECK(color_conversion_successful);
      }
    }
    buffer.SetPixelsChanged(true);
    return true;
  }
#endif

  switch (info->out_color_space) {
    case JCS_RGB:
      return OutputRows<JCS_RGB>(reader_.get(), buffer);
    case JCS_CMYK:
      return OutputRows<JCS_CMYK>(reader_.get(), buffer);
    default:
      NOTREACHED();
  }

  return SetFailed();
}

void JPEGImageDecoder::Complete() {
  if (frame_buffer_cache_.IsEmpty())
    return;

  frame_buffer_cache_[0].SetHasAlpha(false);
  frame_buffer_cache_[0].SetStatus(ImageFrame::kFrameComplete);
}

inline bool IsComplete(const JPEGImageDecoder* decoder, bool only_size) {
  if (decoder->HasImagePlanes() && !only_size)
    return true;

  return decoder->FrameIsDecodedAtIndex(0);
}

void JPEGImageDecoder::Decode(bool only_size) {
  if (Failed())
    return;

  if (!reader_) {
    reader_ = std::make_unique<JPEGImageReader>(this);
    reader_->SetData(data_.get());
  }

  // If we couldn't decode the image but have received all the data, decoding
  // has failed.
  if (!reader_->Decode(only_size) && IsAllDataReceived())
    SetFailed();

  // If decoding is done or failed, we don't need the JPEGImageReader anymore.
  if (IsComplete(this, only_size) || Failed())
    reader_.reset();
}

}  // namespace blink
