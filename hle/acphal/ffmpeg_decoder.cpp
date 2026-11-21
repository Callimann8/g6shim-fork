// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "xma_decoder.hpp"

#if defined(G6SHIM_HAVE_FFMPEG)

#include <windows.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
}

#include <g6shim/log.hpp>

#include <cstdio>
#include <cstring>
#include <mutex>

namespace g6shim::acphal {

namespace {

constexpr int kXma2ExtradataBytes = 34;
constexpr std::uint32_t kMonoMask = 0x4;
constexpr std::uint32_t kStereoMask = 0x3;

struct Api {
    decltype(&avcodec_version) version = nullptr;
    decltype(&avcodec_find_decoder) find_decoder = nullptr;
    decltype(&avcodec_alloc_context3) alloc_context = nullptr;
    decltype(&avcodec_open2) open = nullptr;
    decltype(&avcodec_free_context) free_context = nullptr;
    decltype(&avcodec_send_packet) send_packet = nullptr;
    decltype(&avcodec_receive_frame) receive_frame = nullptr;
    decltype(&avcodec_flush_buffers) flush = nullptr;
    decltype(&av_packet_alloc) packet_alloc = nullptr;
    decltype(&av_packet_free) packet_free = nullptr;
    decltype(&av_frame_alloc) frame_alloc = nullptr;
    decltype(&av_frame_free) frame_free = nullptr;
    decltype(&av_mallocz) mallocz = nullptr;
    decltype(&av_channel_layout_default) default_layout = nullptr;
    bool ok = false;
};

template <typename T>
bool resolve(HMODULE module, const char* name, T& out) {
    out = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, name)));
    return out != nullptr;
}

const Api& api() {
    static Api a;
    static std::once_flag once;
    std::call_once(once, [] {
        char codec_name[32], util_name[32];
        std::snprintf(codec_name, sizeof(codec_name), "avcodec-%d.dll", LIBAVCODEC_VERSION_MAJOR);
        std::snprintf(util_name, sizeof(util_name), "avutil-%d.dll", LIBAVUTIL_VERSION_MAJOR);
        HMODULE util = LoadLibraryA(util_name);
        HMODULE codec = util ? LoadLibraryA(codec_name) : nullptr;
        if (!codec) {
            G6SHIM_INFO("acphal: %s / %s not found", codec_name, util_name);
            return;
        }
        a.ok = resolve(codec, "avcodec_version", a.version) && resolve(codec, "avcodec_find_decoder", a.find_decoder) &&
               resolve(codec, "avcodec_alloc_context3", a.alloc_context) && resolve(codec, "avcodec_open2", a.open) &&
               resolve(codec, "avcodec_free_context", a.free_context) &&
               resolve(codec, "avcodec_send_packet", a.send_packet) &&
               resolve(codec, "avcodec_receive_frame", a.receive_frame) &&
               resolve(codec, "avcodec_flush_buffers", a.flush) && resolve(codec, "av_packet_alloc", a.packet_alloc) &&
               resolve(codec, "av_packet_free", a.packet_free) && resolve(util, "av_frame_alloc", a.frame_alloc) &&
               resolve(util, "av_frame_free", a.frame_free) && resolve(util, "av_mallocz", a.mallocz) &&
               resolve(util, "av_channel_layout_default", a.default_layout);
        if (a.ok && (a.version() >> 16) != LIBAVCODEC_VERSION_MAJOR) a.ok = false;
        G6SHIM_INFO("acphal: FFmpeg %s", a.ok ? "loaded" : "unusable (missing symbols or version mismatch)");
    });
    return a;
}

void put_le16(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

void put_le32(std::uint8_t* p, std::uint32_t v) {
    put_le16(p, v);
    put_le16(p + 2, v >> 16);
}

class FfmpegDecoder final : public XmaDecoder {
public:
    ~FfmpegDecoder() override { close(); }

    void reset() override {
        if (ctx_) api().flush(ctx_);
    }

    bool decode(const std::uint8_t* packet, unsigned channels, std::uint32_t rate, PcmPlanes& out) override {
        if (!open(channels, rate)) return false;
        std::memcpy(buffer_, packet, kXmaPacketBytes);
        pkt_->data = buffer_;
        pkt_->size = static_cast<int>(kXmaPacketBytes);
        if (api().send_packet(ctx_, pkt_) < 0) return false;
        while (api().receive_frame(ctx_, frame_) >= 0) {
            if (frame_->format != AV_SAMPLE_FMT_FLTP) continue;
            for (unsigned c = 0; c < channels && c < static_cast<unsigned>(frame_->ch_layout.nb_channels); ++c) {
                const auto* s = reinterpret_cast<const float*>(frame_->extended_data[c]);
                out.channel[c].insert(out.channel[c].end(), s, s + frame_->nb_samples);
            }
        }
        return true;
    }

private:
    bool open(unsigned channels, std::uint32_t rate) {
        if (ctx_ && channels == channels_ && rate == rate_) return true;
        close();
        const Api& a = api();
        const AVCodec* codec = a.find_decoder(AV_CODEC_ID_XMA2);
        if (!codec) return false;
        ctx_ = a.alloc_context(codec);
        pkt_ = a.packet_alloc();
        frame_ = a.frame_alloc();
        if (!ctx_ || !pkt_ || !frame_) return false;
        ctx_->sample_rate = static_cast<int>(rate);
        a.default_layout(&ctx_->ch_layout, static_cast<int>(channels));
        ctx_->block_align = static_cast<int>(kXmaPacketBytes);
        auto* extra = static_cast<std::uint8_t*>(a.mallocz(kXma2ExtradataBytes + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!extra) return false;
        put_le16(extra, 1);
        put_le32(extra + 2, channels == 2 ? kStereoMask : kMonoMask);
        ctx_->extradata = extra;
        ctx_->extradata_size = kXma2ExtradataBytes;
        if (a.open(ctx_, codec, nullptr) < 0) {
            close();
            return false;
        }
        channels_ = channels;
        rate_ = rate;
        return true;
    }

    void close() {
        const Api& a = api();
        if (ctx_) a.free_context(&ctx_);
        if (pkt_) a.packet_free(&pkt_);
        if (frame_) a.frame_free(&frame_);
        channels_ = rate_ = 0;
    }

    AVCodecContext* ctx_ = nullptr;
    AVPacket* pkt_ = nullptr;
    AVFrame* frame_ = nullptr;
    unsigned channels_ = 0;
    std::uint32_t rate_ = 0;
    alignas(16) std::uint8_t buffer_[kXmaPacketBytes + AV_INPUT_BUFFER_PADDING_SIZE] = {};
};

}  // namespace

std::unique_ptr<XmaDecoder> make_ffmpeg_decoder() {
    if (!api().ok) return nullptr;
    return std::make_unique<FfmpegDecoder>();
}

}  // namespace g6shim::acphal

#else

namespace g6shim::acphal {

std::unique_ptr<XmaDecoder> make_ffmpeg_decoder() { return nullptr; }

}  // namespace g6shim::acphal

#endif
