/**
 * @file Mp3Writer.cpp
 * @brief The MP3 file through Media Foundation's sink writer: an MP3 container (MFTranscodeContainerType_MP3), the
 *        encoder's output type MFAudioFormat_MP3 at the bit rate asked, 16-bit PCM in. Elsewhere than on Windows every
 *        call says no.
 * @author Rene Weller
 */
#include "Mp3Writer.h"

#if defined( _WIN32 )

  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>

  #include <mfapi.h>
  #include <mferror.h>
  #include <mfidl.h>
  #include <mfreadwrite.h>

  #include <algorithm>
  #include <cmath>
  #include <cstdio>
  #include <cstring>

namespace renderkit
{

namespace
{

/**
 * @brief A Media Foundation call's failure as text.
 * @param[in] what  what was attempted
 * @param[in] hr    its result
 * @return "what (0x...)"
 */
std::string failed( const char *what, HRESULT hr )
{
    char text[160];
    std::snprintf( text, sizeof text, "%s (0x%08lx)", what, static_cast<unsigned long>( hr ) );
    return text;
}

/**
 * @brief UTF-8 as Windows' UTF-16.
 * @param[in] s  the text
 * @return it
 */
std::wstring wide( const std::string &s )
{
    if ( s.empty() ) return {};
    const int    n = MultiByteToWideChar( CP_UTF8, 0, s.data(), static_cast<int>( s.size() ), nullptr, 0 );
    std::wstring w( static_cast<size_t>( n ), L'\0' );
    MultiByteToWideChar( CP_UTF8, 0, s.data(), static_cast<int>( s.size() ), w.data(), n );
    return w;
}

}   // namespace

Mp3Writer::~Mp3Writer()
{
    close();
}

bool Mp3Writer::available()
{
    return true;
}

bool Mp3Writer::open( const std::string &path, int sampleRate, int kbps, std::string &why )
{
    close();
    const HRESULT co = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
    m_com = SUCCEEDED( co );   // RPC_E_CHANGED_MODE: the thread has COM already, apartment as it is
    HRESULT hr = MFStartup( MF_VERSION, MFSTARTUP_LITE );
    if ( FAILED( hr ) )
    {
        why = failed( "Media Foundation would not start", hr );
        close();
        return false;
    }
    m_started = true;
    m_rate = sampleRate;
    IMFAttributes *attributes = nullptr;
    hr = MFCreateAttributes( &attributes, 1 );
    if ( SUCCEEDED( hr ) ) hr = attributes->SetGUID( MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MP3 );
    if ( SUCCEEDED( hr ) ) hr = MFCreateSinkWriterFromURL( wide( path ).c_str(), nullptr, attributes, &m_writer );
    if ( attributes ) attributes->Release();
    if ( FAILED( hr ) )
    {
        why = failed( "no MP3 writer (Windows' MP3 encoder)", hr );
        m_writer = nullptr;
        close();
        return false;
    }
    IMFMediaType *out = nullptr, *in = nullptr;
    hr = MFCreateMediaType( &out );
    if ( SUCCEEDED( hr ) ) hr = out->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
    if ( SUCCEEDED( hr ) ) hr = out->SetGUID( MF_MT_SUBTYPE, MFAudioFormat_MP3 );
    if ( SUCCEEDED( hr ) ) hr = out->SetUINT32( MF_MT_AUDIO_NUM_CHANNELS, 2 );
    if ( SUCCEEDED( hr ) ) hr = out->SetUINT32( MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>( sampleRate ) );
    if ( SUCCEEDED( hr ) )
        hr = out->SetUINT32( MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>( kbps * 1000 / 8 ) );
    if ( SUCCEEDED( hr ) ) hr = m_writer->AddStream( out, &m_stream );
    if ( SUCCEEDED( hr ) ) hr = MFCreateMediaType( &in );
    if ( SUCCEEDED( hr ) ) hr = in->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
    if ( SUCCEEDED( hr ) ) hr = in->SetGUID( MF_MT_SUBTYPE, MFAudioFormat_PCM );
    if ( SUCCEEDED( hr ) ) hr = in->SetUINT32( MF_MT_AUDIO_NUM_CHANNELS, 2 );
    if ( SUCCEEDED( hr ) ) hr = in->SetUINT32( MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>( sampleRate ) );
    if ( SUCCEEDED( hr ) ) hr = in->SetUINT32( MF_MT_AUDIO_BITS_PER_SAMPLE, 16 );
    if ( SUCCEEDED( hr ) ) hr = in->SetUINT32( MF_MT_AUDIO_BLOCK_ALIGNMENT, 4 );
    if ( SUCCEEDED( hr ) )
        hr = in->SetUINT32( MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>( sampleRate * 4 ) );
    if ( SUCCEEDED( hr ) ) hr = m_writer->SetInputMediaType( m_stream, in, nullptr );
    if ( SUCCEEDED( hr ) ) hr = m_writer->BeginWriting();
    if ( out ) out->Release();
    if ( in ) in->Release();
    if ( FAILED( hr ) )
    {
        why = failed( "the MP3 encoder would not take 16-bit stereo at this rate", hr );
        m_writer->Release();
        m_writer = nullptr;
        close();
        return false;
    }
    m_time = 0;
    return true;
}

bool Mp3Writer::write( const float *L, const float *R, int n )
{
    if ( !m_writer || n <= 0 ) return m_writer != nullptr;
    m_pcm.resize( static_cast<size_t>( 2 * n ) );
    auto dither = [this]()
    {   // triangular, one least significant bit either way
        m_dither = m_dither * 1664525u + 1013904223u;
        const float a = static_cast<float>( m_dither >> 8 ) * ( 1.0f / 16777216.0f );
        m_dither = m_dither * 1664525u + 1013904223u;
        const float b = static_cast<float>( m_dither >> 8 ) * ( 1.0f / 16777216.0f );
        return a - b;
    };
    for ( int i = 0; i < n; ++i )
        for ( int c = 0; c < 2; ++c )
        {
            const float x = std::clamp( ( c == 0 ? L[i] : R[i] ) * 32767.0f + dither(), -32768.0f, 32767.0f );
            m_pcm[static_cast<size_t>( 2 * i + c )] = static_cast<std::int16_t>( std::lround( x ) );
        }
    const DWORD     bytes = static_cast<DWORD>( m_pcm.size() * sizeof( std::int16_t ) );
    IMFMediaBuffer *buffer = nullptr;
    IMFSample      *sample = nullptr;
    HRESULT         hr = MFCreateMemoryBuffer( bytes, &buffer );
    BYTE           *data = nullptr;
    if ( SUCCEEDED( hr ) ) hr = buffer->Lock( &data, nullptr, nullptr );
    if ( SUCCEEDED( hr ) )
    {
        std::memcpy( data, m_pcm.data(), bytes );
        hr = buffer->Unlock();
    }
    if ( SUCCEEDED( hr ) ) hr = buffer->SetCurrentLength( bytes );
    if ( SUCCEEDED( hr ) ) hr = MFCreateSample( &sample );
    if ( SUCCEEDED( hr ) ) hr = sample->AddBuffer( buffer );
    // in 100 ns units from the samples written, so the blocks' times never drift
    const long long start = m_time * 10000000LL / m_rate, end = ( m_time + n ) * 10000000LL / m_rate;
    if ( SUCCEEDED( hr ) ) hr = sample->SetSampleTime( start );
    if ( SUCCEEDED( hr ) ) hr = sample->SetSampleDuration( end - start );
    if ( SUCCEEDED( hr ) ) hr = m_writer->WriteSample( m_stream, sample );
    if ( sample ) sample->Release();
    if ( buffer ) buffer->Release();
    m_time += n;
    return SUCCEEDED( hr );
}

bool Mp3Writer::close()
{
    HRESULT hr = S_OK;
    if ( m_writer )
    {
        hr = m_writer->Finalize();
        m_writer->Release();
        m_writer = nullptr;
    }
    if ( m_started ) MFShutdown();
    m_started = false;
    if ( m_com ) CoUninitialize();
    m_com = false;
    return SUCCEEDED( hr );
}

}   // namespace renderkit

#else

namespace renderkit
{

Mp3Writer::~Mp3Writer() = default;

bool Mp3Writer::available()
{
    return false;
}

bool Mp3Writer::open( const std::string &, int, int, std::string &why )
{
    why = "MP3 is written on Windows only";
    return false;
}

bool Mp3Writer::write( const float *, const float *, int )
{
    return false;
}

bool Mp3Writer::close()
{
    return true;
}

}   // namespace renderkit

#endif
