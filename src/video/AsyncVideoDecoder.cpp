//
//  libavg - Media Playback Engine. 
//  Copyright (C) 2003-2008 Ulrich von Zadow
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//  Current versions can be found at www.libavg.de
//

#include "AsyncVideoDecoder.h"
#include "EOFVideoMsg.h"
#include "ErrorVideoMsg.h"
#include "SeekDoneVideoMsg.h"

#include "../base/ObjectCounter.h"

#include <boost/thread/thread.hpp>
#include <boost/bind.hpp>

#include <math.h>
#include <iostream>

using namespace boost;
using namespace std;

namespace avg {

AsyncVideoDecoder::AsyncVideoDecoder(VideoDecoderPtr pSyncDecoder)
    : m_pSyncDecoder(pSyncDecoder),
      m_pVDecoderThread(0),
      m_pADecoderThread(0),
      m_PF(NO_PIXELFORMAT),
      m_bAudioEOF(false),
      m_bVideoEOF(false),
      m_bSeekPending(false),
      m_Volume(1.0),
      m_LastVideoFrameTime(-1000),
      m_LastAudioFrameTime(-1000)
{
    ObjectCounter::get()->incRef(&typeid(*this));
}

AsyncVideoDecoder::~AsyncVideoDecoder()
{
    if (m_pVDecoderThread || m_pADecoderThread) {
        close();
    }
    ObjectCounter::get()->decRef(&typeid(*this));
}

void AsyncVideoDecoder::open(const std::string& sFilename, const AudioParams* pAP,
        bool bDeliverYCbCr, bool bThreadedDemuxer)
{
    m_bAudioEOF = false;
    m_bVideoEOF = false;
    m_bSeekPending = false;
    m_sFilename = sFilename;

    m_pSyncDecoder->open(m_sFilename, pAP, bDeliverYCbCr, bThreadedDemuxer);
    m_VideoInfo = m_pSyncDecoder->getVideoInfo();

    if (m_VideoInfo.m_bHasVideo) {
        m_LastVideoFrameTime = -1000;
        m_PF = m_pSyncDecoder->getPixelFormat();
        m_pVCmdQ = VideoDecoderThread::CmdQueuePtr(new VideoDecoderThread::CmdQueue);
        m_pVMsgQ = VideoMsgQueuePtr(new VideoMsgQueue(8));
        m_pVDecoderThread = new boost::thread(
                 VideoDecoderThread(*m_pVCmdQ, *m_pVMsgQ, m_pSyncDecoder));
    }
    
    if (m_VideoInfo.m_bHasAudio) {
        m_pACmdQ = AudioDecoderThread::CmdQueuePtr(new AudioDecoderThread::CmdQueue);
        m_pAMsgQ = VideoMsgQueuePtr(new VideoMsgQueue(8));
        m_pADecoderThread = new boost::thread(
                 AudioDecoderThread(*m_pACmdQ, *m_pAMsgQ, m_pSyncDecoder, *pAP));
        m_AudioMsgData = 0;
        m_AudioMsgSize = 0;
        m_LastAudioFrameTime = 0;
        setVolume(m_Volume);
    }
}

void AsyncVideoDecoder::close()
{
    if (m_pVDecoderThread) {
        m_pVCmdQ->push(Command<VideoDecoderThread>(boost::bind(
                &VideoDecoderThread::stop, _1)));
        getNextBmps(false); // If the Queue is full, this breaks the lock in the thread.
        m_pVDecoderThread->join();
        delete m_pVDecoderThread;
        m_pVDecoderThread = 0;
    }
    {
        scoped_lock Lock1(m_AudioMutex);
        if (m_pADecoderThread) {
            m_pACmdQ->push(Command<AudioDecoderThread>(boost::bind(
                    &AudioDecoderThread::stop, _1)));
            try {
                m_pAMsgQ->pop(false);
                m_pAMsgQ->pop(false);
            } catch(Exception&) {}
            m_pADecoderThread->join();
            delete m_pADecoderThread;
            m_pADecoderThread = 0;
        }
        m_pSyncDecoder->close();
    }        
}

VideoInfo AsyncVideoDecoder::getVideoInfo() const
{
    return m_VideoInfo;
}

void AsyncVideoDecoder::seek(long long DestTime)
{
    waitForSeekDone();
    scoped_lock Lock1(m_AudioMutex);
    scoped_lock Lock2(m_SeekMutex);
    m_bAudioEOF = false;
    m_bVideoEOF = false;
    m_bSeekPending = false;
    m_LastVideoFrameTime = -1000;
    m_bSeekPending = true;
    if (m_pVCmdQ) {
        m_pVCmdQ->push(Command<VideoDecoderThread>(boost::bind(
                    &VideoDecoderThread::seek, _1, DestTime)));
    } else {
        m_pACmdQ->push(Command<AudioDecoderThread>(boost::bind(
                    &AudioDecoderThread::seek, _1, DestTime)));
    }
    try {
        while (m_bSeekPending) {
            VideoMsgPtr pMsg;
            if (m_pVCmdQ) {
                pMsg = m_pVMsgQ->pop(false);
            } else {
                pMsg = m_pAMsgQ->pop(false);
            }
            SeekDoneVideoMsgPtr pSeekDoneMsg = 
                    dynamic_pointer_cast<SeekDoneVideoMsg>(pMsg);
            if (pSeekDoneMsg) {
                m_bSeekPending = false;
                m_LastVideoFrameTime = pSeekDoneMsg->getVideoFrameTime();
                m_LastAudioFrameTime = pSeekDoneMsg->getAudioFrameTime();
            }
            FrameVideoMsgPtr pFrameMsg = dynamic_pointer_cast<FrameVideoMsg>(pMsg);
            if (pFrameMsg) {
                returnFrame(pFrameMsg);
            }
        }
    } catch (Exception&) {
    }
}

IntPoint AsyncVideoDecoder::getSize() const
{
    return m_VideoInfo.m_Size;
}

int AsyncVideoDecoder::getCurFrame() const
{
    return int(getCurTime(SS_VIDEO)*m_VideoInfo.m_StreamFPS/1000.0+0.5);
}

int AsyncVideoDecoder::getNumFramesQueued() const
{
    return m_pVMsgQ->size();
}

long long AsyncVideoDecoder::getCurTime(StreamSelect Stream) const
{
    switch(Stream) {
        case SS_DEFAULT:
        case SS_VIDEO:
            assert(m_VideoInfo.m_bHasVideo);
            return m_LastVideoFrameTime;
            break;
        case SS_AUDIO:
            assert(m_VideoInfo.m_bHasAudio);
            return m_LastAudioFrameTime;
            break;
        default:
            assert(false);
    }
    return -1;
}

double AsyncVideoDecoder::getNominalFPS() const
{
    return m_VideoInfo.m_StreamFPS;
}

double AsyncVideoDecoder::getFPS() const
{
    assert(m_pVDecoderThread);
    return m_VideoInfo.m_FPS;
}

void AsyncVideoDecoder::setFPS(double FPS)
{
    assert(!m_pADecoderThread);
    m_pVCmdQ->push(Command<VideoDecoderThread>(boost::bind(
            &VideoDecoderThread::setFPS, _1, FPS)));
    if (FPS != 0) {
        m_VideoInfo.m_FPS = FPS;
    }
}

double AsyncVideoDecoder::getVolume() const
{
    return m_Volume;
}

void AsyncVideoDecoder::setVolume(double Volume)
{
    m_Volume = Volume;
    if (m_VideoInfo.m_bHasAudio && m_pACmdQ) {
        m_pACmdQ->push(Command<AudioDecoderThread>(boost::bind(
                &AudioDecoderThread::setVolume, _1, Volume)));
    }
}

PixelFormat AsyncVideoDecoder::getPixelFormat() const
{
    assert(m_pVDecoderThread);
    return m_PF;
}

FrameAvailableCode AsyncVideoDecoder::renderToBmp(BitmapPtr pBmp, long long timeWanted)
{
    FrameAvailableCode FrameAvailable;
    FrameVideoMsgPtr pFrameMsg = getBmpsForTime(timeWanted, FrameAvailable);
    if (FrameAvailable == FA_NEW_FRAME) {
        pBmp->copyPixels(*(pFrameMsg->getBitmap(0)));
        returnFrame(pFrameMsg);
    }
    return FrameAvailable;
}

FrameAvailableCode AsyncVideoDecoder::renderToYCbCr420p(BitmapPtr pBmpY, BitmapPtr pBmpCb, 
       BitmapPtr pBmpCr, long long timeWanted)
{
    FrameAvailableCode FrameAvailable;
    FrameVideoMsgPtr pFrameMsg = getBmpsForTime(timeWanted, FrameAvailable);
    if (FrameAvailable == FA_NEW_FRAME) {
        pBmpY->copyPixels(*(pFrameMsg->getBitmap(0)));
        pBmpCb->copyPixels(*(pFrameMsg->getBitmap(1)));
        pBmpCr->copyPixels(*(pFrameMsg->getBitmap(2)));
        returnFrame(pFrameMsg);
    }
    return FrameAvailable;
}

bool AsyncVideoDecoder::isEOF(StreamSelect Stream) const
{
    switch(Stream) {
        case SS_AUDIO:
            return (!m_VideoInfo.m_bHasAudio || m_bAudioEOF);
        case SS_VIDEO:
            return (!m_VideoInfo.m_bHasVideo || m_bVideoEOF);
        case SS_ALL:
            return isEOF(SS_VIDEO) && isEOF(SS_AUDIO);
        default:
            return false;
    }
}

void AsyncVideoDecoder::throwAwayFrame(long long timeWanted)
{
    FrameAvailableCode FrameAvailable;
    FrameVideoMsgPtr pFrameMsg = getBmpsForTime(timeWanted, FrameAvailable);
}

int AsyncVideoDecoder::fillAudioBuffer(AudioBufferPtr pBuffer)
{
    assert (m_pADecoderThread);
    if (m_bAudioEOF) {
        return 0;
    }
    scoped_lock Lock(m_AudioMutex);
    waitForSeekDone();

    unsigned char* audioBuffer = (unsigned char *)(pBuffer->getData());
    int audioBufferSize = pBuffer->getNumBytes();

    int bufferLeftToFill = audioBufferSize;
    while (bufferLeftToFill > 0) {
        while (m_AudioMsgSize > 0 && bufferLeftToFill > 0) {
            int copyBytes = min(bufferLeftToFill, m_AudioMsgSize);
            memcpy(audioBuffer, m_AudioMsgData, copyBytes);
            m_AudioMsgSize -= copyBytes;
            m_AudioMsgData += copyBytes;
            bufferLeftToFill -= copyBytes;
            audioBuffer += copyBytes;

            m_LastAudioFrameTime += (long long)(1000.0 * copyBytes / 
                    (pBuffer->getFrameSize() * pBuffer->getRate()));
        }
        if (bufferLeftToFill != 0) {
            try {
                VideoMsgPtr pMsg = m_pAMsgQ->pop(false);

                EOFVideoMsgPtr pEOFMsg(dynamic_pointer_cast<EOFVideoMsg>(pMsg));
                if (pEOFMsg) {
                    m_bAudioEOF = true;
                    return pBuffer->getNumFrames()-bufferLeftToFill/pBuffer->getFrameSize();
                }

                m_pAudioMsg = dynamic_pointer_cast<AudioVideoMsg>(pMsg);
                assert(m_pAudioMsg);

                m_AudioMsgSize = m_pAudioMsg->getBuffer()->getNumFrames()
                        *pBuffer->getFrameSize();
                m_AudioMsgData = (unsigned char *)(m_pAudioMsg->getBuffer()->getData());
                m_LastAudioFrameTime = m_pAudioMsg->getTime();
            } catch (Exception &) {
                return pBuffer->getNumFrames()-bufferLeftToFill/pBuffer->getFrameSize();
            }
        }
    }
    return pBuffer->getNumFrames();
}
        
FrameVideoMsgPtr AsyncVideoDecoder::getBmpsForTime(long long timeWanted, 
        FrameAvailableCode& FrameAvailable)
{
    // XXX: This code is sort-of duplicated in FFMpegDecoder::readFrameForTime()
    long long FrameTime = -1000;
    FrameVideoMsgPtr pFrameMsg;
    if (timeWanted == -1) {
        pFrameMsg = getNextBmps(true);
        FrameAvailable = FA_NEW_FRAME;
    } else {
        double TimePerFrame = 1000.0/getFPS();
        if (fabs(double(timeWanted-m_LastVideoFrameTime)) < 0.5*TimePerFrame || 
                m_LastVideoFrameTime > timeWanted+TimePerFrame) {
            // The last frame is still current. Display it again.
            FrameAvailable = FA_USE_LAST_FRAME;
            return FrameVideoMsgPtr();
        } else {
            if (m_bVideoEOF) {
                FrameAvailable = FA_USE_LAST_FRAME;
                return FrameVideoMsgPtr();
            }
            while (FrameTime-timeWanted < -0.5*TimePerFrame && !m_bVideoEOF) {
                returnFrame(pFrameMsg);
                pFrameMsg = getNextBmps(false);
                if (pFrameMsg) {
                    FrameTime = pFrameMsg->getFrameTime();
                } else {
                    FrameAvailable = FA_STILL_DECODING;
                    return FrameVideoMsgPtr();
                }
            }
            FrameAvailable = FA_NEW_FRAME;
        }
    }
    if (pFrameMsg) {
        m_LastVideoFrameTime = pFrameMsg->getFrameTime();
    }
    return pFrameMsg;
}

FrameVideoMsgPtr AsyncVideoDecoder::getNextBmps(bool bWait)
{
    try {
        waitForSeekDone();
        VideoMsgPtr pMsg = m_pVMsgQ->pop(bWait);
        FrameVideoMsgPtr pFrameMsg = dynamic_pointer_cast<FrameVideoMsg>(pMsg);
        while (!pFrameMsg) {
            EOFVideoMsgPtr pEOFMsg(dynamic_pointer_cast<EOFVideoMsg>(pMsg));
            ErrorVideoMsgPtr pErrorMsg(dynamic_pointer_cast<ErrorVideoMsg>(pMsg));
            if (pEOFMsg) {
                m_bVideoEOF = true;
                return FrameVideoMsgPtr();
            } else if (pErrorMsg) {
                m_bVideoEOF = true;
                return FrameVideoMsgPtr();
            } else {
                // Unhandled message type.
                assert(false);
            }
            pMsg = m_pVMsgQ->pop(bWait);
            pFrameMsg = dynamic_pointer_cast<FrameVideoMsg>(pMsg);
        }
        return pFrameMsg;
    } catch (Exception&) {
        return FrameVideoMsgPtr();
    }
}

void AsyncVideoDecoder::waitForSeekDone()
{
    scoped_lock Lock(m_SeekMutex);
    if (m_bSeekPending) {
        do {
            VideoMsgPtr pMsg;
            if (m_pVCmdQ) {
                pMsg = m_pVMsgQ->pop(true);
            } else {
                pMsg = m_pAMsgQ->pop(true);
            }
            SeekDoneVideoMsgPtr pSeekDoneMsg = dynamic_pointer_cast<SeekDoneVideoMsg>(pMsg);
            if (pSeekDoneMsg) {
                m_bSeekPending = false;
                m_LastVideoFrameTime = pSeekDoneMsg->getVideoFrameTime();
                m_LastAudioFrameTime = pSeekDoneMsg->getAudioFrameTime();
            }
            FrameVideoMsgPtr pFrameMsg = dynamic_pointer_cast<FrameVideoMsg>(pMsg);
            if (pFrameMsg) {
                returnFrame(pFrameMsg);
            }
        } while (m_bSeekPending);
    }
}

void AsyncVideoDecoder::returnFrame(FrameVideoMsgPtr& pFrameMsg)
{
    if (pFrameMsg) {
        m_pVCmdQ->push(Command<VideoDecoderThread>(boost::bind(
                    &VideoDecoderThread::returnFrame, _1, pFrameMsg)));
    }
}

}
