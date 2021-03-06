//
//  libavg - Media Playback Engine. 
//  Copyright (C) 2003-2014 Ulrich von Zadow
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

#ifndef _GPUFilter_H_
#define _GPUFilter_H_

#include "../api.h"
#include "Filter.h"

namespace avg {

class ImagingProjection;
typedef boost::shared_ptr<ImagingProjection> ImagingProjectionPtr;
class OGLShader;
typedef boost::shared_ptr<OGLShader> OGLShaderPtr;
class FBO;
typedef boost::shared_ptr<FBO> FBOPtr;
class MCFBO;
typedef boost::shared_ptr<MCFBO> MCFBOPtr;
class MCTexture;
typedef boost::shared_ptr<MCTexture> MCTexturePtr;
class GLTexture;
typedef boost::shared_ptr<GLTexture> GLTexturePtr;
class TextureMover;
typedef boost::shared_ptr<TextureMover> TextureMoverPtr;

class AVG_API GPUFilter: public Filter
{
public:
    GPUFilter(const std::string& sShaderID, bool bUseAlpha, bool bStandalone=false,
            unsigned numTextures=1, bool bMipmap=false);
    GPUFilter(PixelFormat pfSrc, PixelFormat pfDest, bool bStandalone,
            const std::string& sShaderID, unsigned numTextures=1, bool bMipmap=false);
    virtual ~GPUFilter();

    virtual BitmapPtr apply(BitmapPtr pBmpSource);
    virtual void apply(GLTexturePtr pSrcTex);
    virtual void applyOnGPU(GLTexturePtr pSrcTex) = 0;
    GLTexturePtr getDestTex(int i=0) const;
    BitmapPtr getImage() const;
    FBOPtr getFBO(int i=0);

    const IntRect& getDestRect() const;
    const IntPoint& getSrcSize() const;
    FRect getRelDestRect() const;
    
protected:
    void setDimensions(const IntPoint& srcSize);
    void setDimensions(const IntPoint& srcSize, const IntRect& destRect,
            unsigned texMode);
    OGLShaderPtr getShader() const;

    void draw(GLTexturePtr pTex);
    int getBlurKernelRadius(float stdDev) const;
    MCTexturePtr calcBlurKernelTex(float stdDev, float opacity, bool bUseFloat) const;

private:
    PixelFormat m_PFSrc;
    PixelFormat m_PFDest;
    bool m_bStandalone;
    std::string m_sShaderID;
    unsigned m_NumTextures;
    bool m_bMipmap;

    MCTexturePtr m_pSrcTex;
    TextureMoverPtr m_pSrcMover;
    std::vector<MCFBOPtr> m_pFBOs;
    IntPoint m_SrcSize;
    IntRect m_DestRect;
    ImagingProjectionPtr m_pProjection;

    bool m_bIsInitialized;
};

typedef boost::shared_ptr<GPUFilter> GPUFilterPtr;

}
#endif

