//
//  libavg - Media Playback Engine. 
//  Copyright (C) 2003-2011 Ulrich von Zadow
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

#include "AsyncDemuxer.h"

#include "../base/ScopeTimer.h"
#include "../base/Exception.h"
#include "../base/ObjectCounter.h"

#include <boost/bind.hpp>

#include <iostream>

#define PACKET_QUEUE_LENGTH 50

using namespace std;

typedef boost::mutex::scoped_lock scoped_lock;

namespace avg {

AsyncDemuxer::AsyncDemuxer(AVFormatContext * pFormatContext, vector<int> streamIndexes)
    : m_pCmdQ(new VideoDemuxerThread::CQueue),
      m_bSeekPending(false),
      m_pFormatContext(pFormatContext)
{
    ObjectCounter::get()->incRef(&typeid(*this));
    for (unsigned i = 0; i < streamIndexes.size(); ++i) {
        enableStream(streamIndexes[i]);
    }
    m_pDemuxThread = new boost::thread(VideoDemuxerThread(*m_pCmdQ, m_pFormatContext,
            m_PacketQs));
}

AsyncDemuxer::~AsyncDemuxer()
{
    if (m_pDemuxThread) {
        m_pCmdQ->pushCmd(boost::bind(&VideoDemuxerThread::stop, _1));
        map<int, VideoPacketQueuePtr>::iterator it;
        for (it = m_PacketQs.begin(); it != m_PacketQs.end(); ++it) {
            // If the Queue is full, this breaks the lock in the thread.
            PacketVideoMsgPtr pPacketMsg;
            pPacketMsg = it->second->pop(false);
            if (pPacketMsg) {
                pPacketMsg->freePacket();
            }
        }
        m_pDemuxThread->join();
        delete m_pDemuxThread;
        m_pDemuxThread = 0;
        for (it = m_PacketQs.begin(); it != m_PacketQs.end(); it++) {
            VideoPacketQueuePtr pPacketQ = it->second;
            PacketVideoMsgPtr pPacketMsg;
            pPacketMsg = pPacketQ->pop(false);
            while (pPacketMsg) {
                pPacketMsg->freePacket();
                pPacketMsg = pPacketQ->pop(false);
            }
        }
    }
    ObjectCounter::get()->decRef(&typeid(*this));
}

AVPacket * AsyncDemuxer::getPacket(int streamIndex, bool& bSeekDone)
{
//    cerr << "  AsyncDemuxer::getPacket" << endl;
    PacketVideoMsgPtr pPacketMsg = m_PacketQs[streamIndex]->pop(true);
    bSeekDone = pPacketMsg->isSeekDone();
    if (bSeekDone) {
        pPacketMsg = m_PacketQs[streamIndex]->pop(true);
    }
    return pPacketMsg->getPacket();
}

void AsyncDemuxer::seek(float destTime)
{
//    cerr << "  AsyncDemuxer::seek" << endl;
    map<int, VideoPacketQueuePtr>::iterator it;
    for (it = m_PacketQs.begin(); it != m_PacketQs.end(); it++) {
        VideoPacketQueuePtr pPacketQ = it->second;
        pPacketQ->clear();
    }
    m_pCmdQ->pushCmd(boost::bind(&VideoDemuxerThread::seek, _1, destTime));
//    cerr << "  AsyncDemuxer::seek end" << endl;
}

void AsyncDemuxer::enableStream(int streamIndex)
{
    VideoPacketQueuePtr pPacketQ(new VideoPacketQueue(PACKET_QUEUE_LENGTH));
    m_PacketQs[streamIndex] = pPacketQ;
    m_bSeekDoneMap[streamIndex] = false;
}

}
