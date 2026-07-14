#pragma once
#include "stdafx.h"
#include <stdint.h>

class input_msu : public input_stubs
{
public:
    typedef input_decoder_v4     interface_decoder_t;
    typedef input_info_reader_v2 interface_info_reader_t;
    typedef input_info_writer_v2 interface_info_writer_t;

    input_msu();

    static bool g_is_our_path(const char* p_path, const char* p_extension) {
        return ::stricmp_utf8(p_extension, "pcm") == 0;
    }
    static bool g_is_our_content_type(const char*) { return false; }
    static bool g_fallback_is_our_payload(const void*, size_t, t_filesize) { return false; }
    static bool g_is_low_merit() { return false; }
    static GUID g_get_guid() {
        return { 0x7d36abc1, 0x4e5f, 0x4b29, { 0x93, 0xd8, 0x5f, 0x3a, 0x2b, 0x8c, 0x1d, 0xe4 } };
    }
    static const char* g_get_name() { return "MSU-1 PCM Input"; }
    static GUID g_get_preferences_guid() { return pfc::guid_null; }

    void open(service_ptr_t<file> p_filehint, const char* p_path,
              t_input_open_reason p_reason, abort_callback& p_abort);
    void get_info(file_info& p_info, abort_callback& p_abort);
    t_filestats2 get_stats2(uint32_t f, abort_callback& p_abort) {
        return t_filestats2::from_legacy(m_file->get_stats(p_abort));
    }

    void decode_initialize(unsigned p_flags, abort_callback& p_abort);
    bool decode_run(audio_chunk& p_chunk, abort_callback& p_abort);
    void decode_seek(double p_seconds, abort_callback& p_abort);
    bool decode_can_seek() { return true; }

    void retag(const file_info&, abort_callback&) {}
    void remove_tags(abort_callback&) {}

private:
    static constexpr uint32_t HEADER_SIZE      = 8;
    static constexpr uint32_t BYTES_PER_SAMPLE = 4;    // 16-bit stereo = 4 bytes
    static constexpr uint32_t SAMPLE_RATE      = 44100;
    static constexpr uint32_t CHUNK_SAMPLES    = 2048;  // ~46ms per decode call

    service_ptr_t<file> m_file;
    pfc::string8        m_path;

    uint64_t m_fileSize;
    uint32_t m_loopStartSample; // raw value from header (in samples)
    uint64_t m_loopStartByte;   // absolute file offset of loop point
    bool     m_hasLoop;

    // Decode state (reset on decode_initialize / decode_seek)
    uint64_t m_currentByte;
    int      m_loopsRemaining;  // -1 = infinite, >=0 = loops left before fade
    bool     m_fading;
    uint64_t m_fadeBytesRead;
    bool     m_done;

    bool handle_loop_end(abort_callback& p_abort);

    // Playback time helpers
    double introSecs() const;
    double loopSecs()  const;
    double totalSecs() const;
};
