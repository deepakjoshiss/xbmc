/*
 *  Copyright (C) 2005-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "DVDDemux.h"
#include "cores/VideoPlayer/Interface/TimingConstants.h"
#include "threads/CriticalSection.h"
#include "threads/SystemClock.h"
#include <deque>
#include <map>
#include <memory>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

class CDVDDemuxFFmpeg;
class CDVDInputStream;
class CURL;
struct ChapterFFmpeg;

enum class TRANSPORT_STREAM_STATE
{
  NONE,
  READY,
  NOTREADY,
};

class CDemuxStreamVideoFFmpeg : public CDemuxStreamVideo
{
public:
  explicit CDemuxStreamVideoFFmpeg(AVStream* stream) : m_stream(stream) {}
  std::string GetStreamName() override;

  std::string m_description;
protected:
  AVStream* m_stream = nullptr;
};

class CDemuxStreamAudioFFmpeg : public CDemuxStreamAudio
{
public:
  explicit CDemuxStreamAudioFFmpeg(AVStream* stream) : m_stream(stream) {}
  std::string GetStreamName() override;

  std::string m_description;
protected:
  CDVDDemuxFFmpeg* m_parent;
  AVStream* m_stream  = nullptr;
};

class CDemuxStreamSubtitleFFmpeg
  : public CDemuxStreamSubtitle
{
public:
  explicit CDemuxStreamSubtitleFFmpeg(AVStream* stream) : m_stream(stream) {}
  std::string GetStreamName() override;

  std::string m_description;
protected:
  CDVDDemuxFFmpeg* m_parent;
  AVStream* m_stream = nullptr;
};

class CDemuxParserFFmpeg
{
public:
  ~CDemuxParserFFmpeg();
  AVCodecParserContext* m_parserCtx = nullptr;
  AVCodecContext* m_codecCtx = nullptr;
};

#define FFMPEG_DVDNAV_BUFFER_SIZE 2048  // for dvd's

struct StereoModeConversionMap;

class CDVDDemuxFFmpeg : public CDVDDemux
{
public:
  CDVDDemuxFFmpeg();
  ~CDVDDemuxFFmpeg() override;

  bool Open(const std::shared_ptr<CDVDInputStream>& pInput, bool fileinfo);
  void Dispose();
  bool Reset() override ;
  void Flush() override;
  void Abort() override;
  void SetSpeed(int iSpeed) override;
  std::string GetFileName() override;

  DemuxPacket* Read() override;
  DemuxPacket* ReadInternal(bool keep);

  bool SeekTime(double time, bool backwards = false, double* startpts = NULL) override;
  bool SeekByte(int64_t pos);
  int GetStreamLength() override;
  CDemuxStream* GetStream(int iStreamId) const override;
  std::vector<CDemuxStream*> GetStreams() const override;
  int GetNrOfStreams() const override;
  int GetPrograms(std::vector<ProgramInfo>& programs) override;
  void SetProgram(int progId) override;

  bool SeekChapter(int chapter, double* startpts = NULL) override;
  int GetChapterCount() override;
  int GetChapter() override;
  void GetChapterName(std::string& strChapterName, int chapterIdx=-1) override;
  std::chrono::milliseconds GetChapterPos(int chapterIdx = -1) override;
  std::string GetStreamCodecName(int iStreamId) override;

  bool IsStreaming() const override;

  bool Aborted();

  AVFormatContext* m_pFormatContext;
  std::shared_ptr<CDVDInputStream> m_pInput;

protected:
  friend class CDemuxStreamAudioFFmpeg;
  friend class CDemuxStreamVideoFFmpeg;
  friend class CDemuxStreamSubtitleFFmpeg;

  CDemuxStream* AddStream(int streamIdx);
  void AddStream(int streamIdx, CDemuxStream* stream);
  void CreateStreams(unsigned int program = UINT_MAX);
  void DisposeStreams();
  void ParsePacket(AVPacket* pkt);
  void SaveProbedStreamParameters();
  void RestoreProbedStreamParameters();
  void ClearProbedStreamParameters();
  TRANSPORT_STREAM_STATE TransportStreamAudioState();
  TRANSPORT_STREAM_STATE TransportStreamVideoState();
  bool IsTransportStreamReady();
  void ResetVideoStreams();
  AVDictionary* GetFFMpegOptionsFromInput();
  double ConvertTimestamp(int64_t pts, int den, int num);
  bool IsProgramChange();
  unsigned int HLSSelectProgram();

  std::string GetStereoModeFromMetadata(AVDictionary* pMetadata);
  std::string ConvertCodecToInternalStereoMode(const std::string& mode, const StereoModeConversionMap* conversionMap);

  void GetL16Parameters(int& channels, int& samplerate);
  double SelectAspect(AVStream* st, bool& forced);

  StreamHdrType DetermineHdrType(AVStream* pStream);

  // Dolby Vision with the base layer and the enhancement layer + RPUs in separate tracks (MP4)
  void FindDoviDualTrack();
  void StoreDoviRpu(const AVPacket& pkt, const AVStream* stream);
  void AttachDoviRpu(DemuxPacket* pkt);
  void ClearDoviHeld();
  // dual-layer Dolby Vision on webOS starts at an IDR: SeekTime steps back to one
  int FindIdrSeekStream() const;
  void SeekBackToIdr(int64_t seekPts, bool backwards);

  CCriticalSection m_critSection;
  std::map<int, CDemuxStream*> m_streams;
  std::map<int, std::unique_ptr<CDemuxParserFFmpeg>> m_parsers;

  AVIOContext* m_ioContext;

  double   m_currentPts; // used for stream length estimation
  bool     m_bMatroska;
  bool     m_bAVI;
  bool     m_bSup;
  int      m_speed;
  unsigned int m_program;
  unsigned int m_streamsInProgram;
  unsigned int m_newProgram;
  unsigned int m_initialProgramNumber;
  int m_seekStream;

  XbmcThreads::EndTime<> m_timeout;

  // Due to limitations of ffmpeg, we only can detect a program change
  // with a packet. This struct saves the packet for the next read and
  // signals STREAMCHANGE to player
  struct
  {
    AVPacket pkt;       // packet ffmpeg returned
    int      result;    // result from av_read_packet
  }m_pkt;

  struct AVCodecParametersDeleter
  {
    void operator()(AVCodecParameters* codecpar) const { avcodec_parameters_free(&codecpar); }
  };

  struct ProbedStream
  {
    std::unique_ptr<AVCodecParameters, AVCodecParametersDeleter> codecpar;
    AVRational rFrameRate{0, 0};
    AVRational avgFrameRate{0, 0};
    AVRational sampleAspectRatio{0, 0};
  };
  // What avformat_find_stream_info() established, kept across the transport stream re-open.
  std::map<int, ProbedStream> m_probedStreams;

  bool m_streaminfo;
  bool m_reopen = false;
  bool m_checkTransportStream;
  int m_displayTime = 0;
  double m_dtsAtDisplayTime;
  bool m_seekToKeyFrame = false;

  int m_doviBlIndex = -1; // played stream that gets the RPUs
  int m_doviElIndex = -1; // hidden stream they come from
  int m_doviLengthSize = 4; // NAL length size of the base layer's hvcC, 0 for Annex B
  int m_doviElLengthSize = 4; // the same for the enhancement-layer track
  bool m_doviWithEl = false; // merge the whole EL, not only the RPUs (no P7 -> 8.1 conversion)
  AVDOVIDecoderConfigurationRecord m_doviConf{};
  std::map<double, std::vector<uint8_t>> m_doviRpus; // dts -> EL NAL units to append, prefixed
  double m_doviElDts = DVD_NOPTS_VALUE; // last enhancement-layer dts read
  bool m_doviElRead = false; // ReadInternal consumed an enhancement-layer packet
  int m_idrSeekStream = -1; // video stream SeekTime moves back to an IDR on, -1 for none
  std::deque<DemuxPacket*> m_doviHeld; // base-layer packets waiting for their RPU
  double m_startTime = 0;
  std::vector<ChapterFFmpeg> m_chapters;
};
