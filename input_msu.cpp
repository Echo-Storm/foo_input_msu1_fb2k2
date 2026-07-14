#include "stdafx.h"
#include "advconfig_impl.h"
#include "input_msu.h"
#include <algorithm>

#define MSU_MAGIC "MSU1"

// GUIDs for Advanced Preferences entries
static const GUID guid_cfg_loop_count =
    { 0xc1a2b3c4, 0xd5e6, 0xf7a8, { 0xb9, 0xc0, 0xd1, 0xe2, 0xf3, 0xa4, 0xb5, 0xc6 } };
static const GUID guid_cfg_fade_secs =
    { 0xe1f2a3b4, 0xc5d6, 0xe7f8, { 0xa9, 0xb0, 0xc1, 0xd2, 0xe3, 0xf4, 0xa5, 0xb6 } };

// 0 = infinite loop (no fade), 1+ = loop N times then fade out
static advconfig_integer_factory_cached cfg_loop_count(
    "MSU-1 PCM: Loop count (0 = infinite)",
    "msu1_loop_count",
    guid_cfg_loop_count,
    advconfig_entry::guid_branch_decoding,
    0.0, 2, 0, 100);

// Fade duration in seconds (only used when loop count > 0)
static advconfig_integer_factory_cached cfg_fade_secs(
    "MSU-1 PCM: Fade-out duration (seconds)",
    "msu1_fade_secs",
    guid_cfg_fade_secs,
    advconfig_entry::guid_branch_decoding,
    1.0, 8, 1, 60);


input_msu::input_msu()
    : m_fileSize(0), m_loopStartSample(0), m_loopStartByte(0), m_hasLoop(false),
      m_currentByte(0), m_loopsRemaining(0),
      m_fading(false), m_fadeBytesRead(0), m_done(false)
{}

void input_msu::open(service_ptr_t<file> p_filehint, const char* p_path,
                     t_input_open_reason p_reason, abort_callback& p_abort)
{
    m_file = p_filehint;
    m_path = p_path;
    input_open_file_helper(m_file, p_path, p_reason, p_abort);

    m_fileSize = m_file->get_size(p_abort);
    if (m_fileSize < HEADER_SIZE)
        throw exception_io_unsupported_format();

    uint8_t header[HEADER_SIZE];
    m_file->read(header, HEADER_SIZE, p_abort);

    if (::memcmp(header, MSU_MAGIC, 4) != 0)
        throw exception_io_unsupported_format();

    m_loopStartSample = header[4] | (header[5] << 8) | (header[6] << 16) | (header[7] << 24);
    m_loopStartByte   = HEADER_SIZE + (uint64_t)m_loopStartSample * BYTES_PER_SAMPLE;

    // A loop point past or at EOF means no looping
    m_hasLoop = (m_loopStartByte < m_fileSize);
}

void input_msu::get_info(file_info& p_info, abort_callback& p_abort)
{
    p_info.set_length(totalSecs());
    p_info.info_set_int("samplerate",    SAMPLE_RATE);
    p_info.info_set_int("channels",      2);
    p_info.info_set_int("bitspersample", 16);
    p_info.info_set("codec", "MSU-1 PCM");
    if (m_hasLoop) {
        p_info.info_set_int("loop_start", m_loopStartSample);
    }

    // Extract track number from the filename (e.g., "chrono_msu-2.pcm" -> "2")
    const char* path = m_path;
    const char* sep  = strrchr(path, '\\');
    const char* fwd  = strrchr(path, '/');
    if (fwd > sep) sep = fwd;
    const char* fname = sep ? sep + 1 : path;

    const char* dash = strrchr(fname, '-');
    const char* dot  = strrchr(fname, '.');
    if (dash && dot && dot > dash + 1) {
        pfc::string8 trackNum;
        trackNum.set_string(dash + 1, (t_size)(dot - dash - 1));
        p_info.meta_set("tracknumber", trackNum);
    }
}

void input_msu::decode_initialize(unsigned /*p_flags*/, abort_callback& p_abort)
{
    m_file->seek(HEADER_SIZE, p_abort);
    m_currentByte = HEADER_SIZE;

    uint64_t count = cfg_loop_count.get();
    m_loopsRemaining = (count == 0) ? -1 : (int)count;

    m_fading       = false;
    m_fadeBytesRead = 0;
    m_done         = false;
}

bool input_msu::handle_loop_end(abort_callback& p_abort)
{
    if (!m_hasLoop) {
        m_done = true;
        return false;
    }
    if (m_fading) {
        // Hit EOF while fading; wrap to keep fade going
        m_file->seek(m_loopStartByte, p_abort);
        m_currentByte = m_loopStartByte;
        return true;
    }
    if (m_loopsRemaining < 0) {
        // Infinite: just loop forever
        m_file->seek(m_loopStartByte, p_abort);
        m_currentByte = m_loopStartByte;
        return true;
    }
    // Finite: count down
    m_loopsRemaining--;
    if (m_loopsRemaining == 0) {
        m_fading        = true;
        m_fadeBytesRead = 0;
    }
    m_file->seek(m_loopStartByte, p_abort);
    m_currentByte = m_loopStartByte;
    return true;
}

bool input_msu::decode_run(audio_chunk& p_chunk, abort_callback& p_abort)
{
    if (m_done) return false;

    uint64_t bytesLeft = m_fileSize - m_currentByte;
    if (bytesLeft == 0) {
        if (!handle_loop_end(p_abort)) return false;
        bytesLeft = m_fileSize - m_currentByte;
        if (bytesLeft == 0) return false;
    }

    uint32_t toRead = (uint32_t)std::min<uint64_t>(
        (uint64_t)CHUNK_SAMPLES * BYTES_PER_SAMPLE, bytesLeft);

    int16_t buf[CHUNK_SAMPLES * 2];
    m_file->read(buf, toRead, p_abort);
    m_currentByte += toRead;

    uint32_t numSamples = toRead / BYTES_PER_SAMPLE;

    if (m_fading) {
        uint64_t fadeTotalBytes = (uint64_t)cfg_fade_secs.get() * SAMPLE_RATE * BYTES_PER_SAMPLE;
        uint32_t numChannelSamples = numSamples * 2; // stereo interleaved shorts
        for (uint32_t i = 0; i < numChannelSamples; i++) {
            uint64_t sampleFadePos = m_fadeBytesRead + (i / 2) * (uint64_t)BYTES_PER_SAMPLE;
            if (sampleFadePos >= fadeTotalBytes) {
                buf[i] = 0;
            } else {
                float vol = 1.0f - (float)sampleFadePos / (float)fadeTotalBytes;
                buf[i] = (int16_t)((float)buf[i] * vol);
            }
        }
        m_fadeBytesRead += toRead;
        if (m_fadeBytesRead >= fadeTotalBytes) {
            m_done = true;
        }
    }

    p_chunk.set_data_int16(buf, numSamples, 2, SAMPLE_RATE,
                           audio_chunk::channel_config_stereo);
    return !m_done;
}

void input_msu::decode_seek(double p_seconds, abort_callback& p_abort)
{
    m_done         = false;
    m_fading       = false;
    m_fadeBytesRead = 0;

    uint64_t cfgCount = cfg_loop_count.get();
    int loopTotal = (cfgCount == 0) ? -1 : (int)cfgCount;

    if (!m_hasLoop) {
        uint64_t byteOff = HEADER_SIZE + (uint64_t)(p_seconds * SAMPLE_RATE * BYTES_PER_SAMPLE);
        m_currentByte    = std::min(byteOff, m_fileSize);
        m_loopsRemaining = 0;
    } else {
        uint64_t loopBytes = m_fileSize - m_loopStartByte;
        uint64_t targetByte = HEADER_SIZE + (uint64_t)(p_seconds * SAMPLE_RATE * BYTES_PER_SAMPLE);

        if (targetByte <= m_loopStartByte || loopBytes == 0) {
            m_currentByte    = std::min(targetByte, m_loopStartByte);
            m_loopsRemaining = loopTotal;
        } else {
            uint64_t pastIntro    = targetByte - m_loopStartByte;
            uint64_t whichLoop    = pastIntro / loopBytes;
            uint64_t offsetInLoop = pastIntro % loopBytes;
            m_currentByte = m_loopStartByte + offsetInLoop;

            if (loopTotal < 0) {
                m_loopsRemaining = -1;
            } else {
                int loopsLeft = loopTotal - (int)whichLoop;
                if (loopsLeft <= 0) {
                    m_fading = true;
                    uint64_t fadeTotalBytes = (uint64_t)cfg_fade_secs.get() * SAMPLE_RATE * BYTES_PER_SAMPLE;
                    uint64_t inFade = pastIntro - (uint64_t)loopTotal * loopBytes;
                    m_fadeBytesRead = std::min(inFade, fadeTotalBytes);
                    m_loopsRemaining = 0;
                    if (m_fadeBytesRead >= fadeTotalBytes)
                        m_done = true;
                } else {
                    m_loopsRemaining = loopsLeft;
                }
            }
        }
    }

    if (!m_done)
        m_file->seek(m_currentByte, p_abort);
}

double input_msu::introSecs() const
{
    return (double)(m_loopStartByte - HEADER_SIZE) / (SAMPLE_RATE * BYTES_PER_SAMPLE);
}

double input_msu::loopSecs() const
{
    return (double)(m_fileSize - m_loopStartByte) / (SAMPLE_RATE * BYTES_PER_SAMPLE);
}

double input_msu::totalSecs() const
{
    if (!m_hasLoop)
        return (double)(m_fileSize - HEADER_SIZE) / (SAMPLE_RATE * BYTES_PER_SAMPLE);

    uint64_t cfgCount = cfg_loop_count.get();
    if (cfgCount == 0)
        return introSecs() + 2.0 * loopSecs(); // display estimate for infinite
    return introSecs() + (double)cfgCount * loopSecs() + (double)cfg_fade_secs.get();
}


static input_singletrack_factory_t<input_msu> g_input_msu_factory;

DECLARE_COMPONENT_VERSION(
    "MSU-1 PCM Input",
    "1.0-fb2k2",
    "MSU-1 PCM audio playback for foobar2000 v2 x64.\n"
    "Supports looping, configurable loop count, and fade-out.\n"
    "Settings: File > Preferences > Advanced > Decoding\n"
    "Based on foo_input_msu by qwertymodo (2017).\n"
    "Updated for foobar2000 SDK v2 by Echo-Storm (2026).")

DECLARE_FILE_TYPE("MSU-1 audio files", "*.pcm");

VALIDATE_COMPONENT_FILENAME("foo_input_msu1.dll");
